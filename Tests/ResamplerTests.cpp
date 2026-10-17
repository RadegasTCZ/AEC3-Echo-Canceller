#include <JuceHeader.h>
#include "../Source/Resampler.h"
#include "TestSignals.h"
#include "AllocCounter.h"

namespace
{
struct RunResult
{
    std::vector<float> out;
    bool hitLimit = false;        // true if any call returned >= maxOutputFor (output possibly truncated)
};

RunResult runBlocks (aec::Resampler& r, const std::vector<float>& in,
                     const std::vector<int>& blocks, int inRate, int outRate)
{
    int maxBlock = 0;
    for (int b : blocks)
        maxBlock = juce::jmax (maxBlock, b);
    std::vector<float> scratch ((size_t) aec::Resampler::maxOutputFor (maxBlock, inRate, outRate));

    RunResult res;
    res.out.reserve ((size_t) ((double) in.size() * outRate / inRate) + 64);
    size_t pos = 0, b = 0;
    while (pos < in.size())
    {
        const int n = (int) std::min<size_t> ((size_t) blocks[b++ % blocks.size()], in.size() - pos);
        const int maxOut = aec::Resampler::maxOutputFor (n, inRate, outRate);
        const int got = r.process (in.data() + pos, n, scratch.data(), maxOut);
        res.hitLimit = res.hitLimit || got >= maxOut;
        res.out.insert (res.out.end(), scratch.begin(), scratch.begin() + got);
        pos += (size_t) n;
    }
    return res;
}

int zeroCrossings (const float* x, int n)
{
    int c = 0;
    for (int i = 1; i < n; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++c;
    return c;
}

const std::vector<int> hostBlocks { 441, 1000, 37 };
const std::vector<int> internalBlocks { 480, 333, 1024 };

class ResamplerTests final : public juce::UnitTest
{
public:
    ResamplerTests() : juce::UnitTest ("Resampler", "Resampler") {}

    void runTest() override
    {
        beginTest ("passthrough when rates are equal");
        {
            aec::Resampler r;
            expect (r.prepare (48000, 48000));
            expect (r.isPassthrough());
            expectEquals (r.inputLatency(), 0);
            expectEquals (r.outputLatency(), 0);
            const auto in = aec::test::whiteNoise (1000, 0.2f, 1);
            std::vector<float> out (1016, -1.0f);
            expectEquals (r.process (in.data(), 1000, out.data(), (int) out.size()), 1000);
            expect (std::equal (in.begin(), in.end(), out.begin()));
        }

        beginTest ("invalid rates are refused");
        {
            aec::Resampler r;
            expect (! r.prepare (0, 48000));
            expect (! r.isPassthrough());
            float x[4] = {}, y[32] = {};
            expectEquals (r.process (x, 4, y, 32), 0);
        }

        beginTest ("maxOutputFor formula");
        {
            expectEquals (aec::Resampler::maxOutputFor (441, 44100, 48000), 480 + 16);
            expectEquals (aec::Resampler::maxOutputFor (1000, 44100, 48000), 1089 + 16);   // ceil (1088.43)
            expectEquals (aec::Resampler::maxOutputFor (480, 48000, 44100), 441 + 16);
            expectEquals (aec::Resampler::maxOutputFor (37, 96000, 48000), 19 + 16);       // ceil (18.5)
        }

        beginTest ("sample counts over odd-sized blocks match the rate ratio within +-2; maxOutputFor bounds every call");
        {
            struct Pair { int in, out; };
            for (auto p : { Pair { 44100, 48000 }, Pair { 48000, 44100 }, Pair { 96000, 48000 }, Pair { 48000, 96000 } })
            {
                aec::Resampler r;
                expect (r.prepare (p.in, p.out));
                expect (! r.isPassthrough());
                std::vector<float> in ((size_t) 1478 * 300, 0.25f);    // 300 cycles of 441 + 1000 + 37
                const auto res = runBlocks (r, in, hostBlocks, p.in, p.out);
                const double expected = (double) in.size() * p.out / p.in;
                logMessage (juce::String (p.in) + " -> " + juce::String (p.out) + ": got "
                            + juce::String ((int) res.out.size()) + ", expected " + juce::String (expected, 2));
                expect (std::abs ((double) res.out.size() - expected) <= 2.0, "sample count off by more than 2");
                expect (! res.hitLimit, "a call filled maxOutputFor completely (bound too tight)");
            }
        }

        for (int host : { 44100, 88200, 96000 })
        {
            beginTest ("1 kHz sine round trip " + juce::String (host) + " -> 48000 -> " + juce::String (host) + ": level and frequency");
            {
                aec::Resampler up, down;
                expect (up.prepare (host, 48000));
                expect (down.prepare (48000, host));
                const auto in = aec::test::sine (3 * host, 1000.0f, 0.5f, (double) host);
                const auto mid = runBlocks (up, in, hostBlocks, host, 48000).out;
                const auto back = runBlocks (down, mid, internalBlocks, 48000, host).out;

                const int start = host / 10;              // skip the filter transient
                const int len = 2 * host;                 // 2 s steady region
                expect ((int) back.size() >= start + len);
                const float inDb = aec::test::rmsDb (in.data() + start, len);
                const float outDb = aec::test::rmsDb (back.data() + start, len);
                logMessage ("rms in " + juce::String (inDb, 3) + " dB, out " + juce::String (outDb, 3) + " dB");
                expectWithinAbsoluteError (outDb, inDb, 0.1f);
                const int zc = zeroCrossings (back.data() + start, len);
                expect (std::abs (zc - 4000) <= 4, "zero crossings " + juce::String (zc) + " (expected 4000 for 1 kHz over 2 s)");
            }

            beginTest ("round-trip delay " + juce::String (host) + " equals up.inputLatency() + down.outputLatency() within +-1");
            {
                aec::Resampler up, down;
                expect (up.prepare (host, 48000));
                expect (down.prepare (48000, host));
                const auto in = aec::test::whiteNoise (host, 0.1f, 42);
                const auto mid = runBlocks (up, in, hostBlocks, host, 48000).out;
                auto back = runBlocks (down, mid, internalBlocks, 48000, host).out;

                const size_t n = std::min (in.size(), back.size());
                const std::vector<float> inT (in.begin(), in.begin() + (std::ptrdiff_t) n);
                back.resize (n);
                const int lag = aec::test::crossCorrelationLag (inT, back, 400);
                const int expected = up.inputLatency() + down.outputLatency();
                logMessage ("host " + juce::String (host) + ": measured " + juce::String (lag)
                            + ", in " + juce::String (up.inputLatency()) + " + out " + juce::String (down.outputLatency())
                            + " = " + juce::String (expected));
                // If this fails, STOP and report the numbers above; do not change the formula to fit.
                expect (std::abs (lag - expected) <= 1);
            }
        }

        beginTest ("reset() restores fresh behaviour exactly");
        {
            const auto in = aec::test::whiteNoise (44100, 0.1f, 5);
            aec::Resampler a, b;
            expect (a.prepare (44100, 48000));
            expect (b.prepare (44100, 48000));
            const auto first = runBlocks (a, in, hostBlocks, 44100, 48000).out;
            a.reset();
            const auto again = runBlocks (a, in, hostBlocks, 44100, 48000).out;
            const auto fresh = runBlocks (b, in, hostBlocks, 44100, 48000).out;
            expect (again == fresh);
            expect (first == fresh);
        }

        beginTest ("96k to 48k attenuates a 30 kHz tone by at least 60 dB");
        {
            aec::Resampler r;
            expect (r.prepare (96000, 48000));
            const auto in = aec::test::sine (2 * 96000, 30000.0f, 0.5f, 96000.0);
            const auto out = runBlocks (r, in, hostBlocks, 96000, 48000).out;
            const int start = 4800;   // 0.1 s at 48 kHz: filter settled
            expect ((int) out.size() > start + 48000);
            const float inDb = aec::test::rmsDb (in.data() + 9600, 96000);
            const float outDb = aec::test::rmsDb (out.data() + start, 48000);
            logMessage ("30 kHz tone: in " + juce::String (inDb, 2) + " dB, out " + juce::String (outDb, 2) + " dB");
            expect (outDb <= inDb - 60.0f, "30 kHz alias only " + juce::String (inDb - outDb, 2) + " dB down");
        }

        beginTest ("process() and reset() do not allocate");
        {
            aec::Resampler r;
            expect (r.prepare (44100, 48000));
            const auto in = aec::test::whiteNoise (1000, 0.1f, 9);
            std::vector<float> out ((size_t) aec::Resampler::maxOutputFor (1000, 44100, 48000));
            int allocs = -1, total = 0;
            {
                aec::test::ScopedAllocCheck check;
                for (int k = 0; k < 50; ++k)
                {
                    const int n = hostBlocks[(size_t) k % hostBlocks.size()];
                    total += r.process (in.data(), n, out.data(), aec::Resampler::maxOutputFor (n, 44100, 48000));
                }
                r.reset();
                allocs = check.count();
            }
            expectEquals (allocs, 0);
            expect (total > 0);
        }
    }
};

static ResamplerTests resamplerTests;
} // namespace
