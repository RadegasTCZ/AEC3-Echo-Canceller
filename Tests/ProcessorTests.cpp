#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/ParamIds.h"
#include "../Source/AgcPresets.h"
#include "../Source/OutputLimiter.h"
#include "TestSignals.h"

namespace
{
    using Proc = AEC3EchoCancellerAudioProcessor;

    void setRaw (Proc& p, const char* id, float v)
    {
        auto* prm = p.getAPVTS().getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }

    float raw (Proc& p, const char* id)
    {
        return p.getAPVTS().getRawParameterValue (id)->load();
    }

    void prepare (Proc& p, double rate, int block)
    {
        p.startCaptureOnPrepare = false;   // no WASAPI in tests
        p.prepareToPlay (rate, block);
    }

    /** Feeds `in` as identical stereo through the processor in blocks of `block`; returns channel 0.
        `bypassed` selects processBlockBypassed. `toggleEvery` > 0 flips aecEnabled every that many blocks. */
    std::vector<float> run (Proc& p, const std::vector<float>& in, int block, bool bypassed = false,
                            int toggleEvery = 0)
    {
        std::vector<float> out (in.size(), 0.0f);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        int blockIndex = 0;
        bool enabled = raw (p, "aecEnabled") >= 0.5f;

        for (size_t pos = 0; pos + (size_t) block <= in.size(); pos += (size_t) block, ++blockIndex)
        {
            if (toggleEvery > 0 && blockIndex > 0 && blockIndex % toggleEvery == 0)
            {
                enabled = ! enabled;
                setRaw (p, "aecEnabled", enabled ? 1.0f : 0.0f);
            }

            for (int ch = 0; ch < 2; ++ch)
                std::memcpy (buf.getWritePointer (ch), in.data() + pos, sizeof (float) * (size_t) block);

            if (bypassed)
                p.processBlockBypassed (buf, midi);
            else
                p.processBlock (buf, midi);

            std::memcpy (out.data() + pos, buf.getReadPointer (0), sizeof (float) * (size_t) block);
        }

        return out;
    }

    /** True when out[i + latency] == in[i] exactly for every i in [from, in.size() - latency). */
    bool delayedExactly (const std::vector<float>& in, const std::vector<float>& out, int latency, int from = 0)
    {
        for (int i = from; i + latency < (int) in.size(); ++i)
            if (out[(size_t) (i + latency)] != in[(size_t) i])
                return false;

        return true;
    }

    int measuredLag (const std::vector<float>& in, const std::vector<float>& out, int maxLag, int skip)
    {
        const std::vector<float> a (in.begin() + skip, in.end());
        const std::vector<float> b (out.begin() + skip, out.end());
        return aec::test::crossCorrelationLag (a, b, maxLag);
    }
}

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("Processor", "Processor") {}

    // Separate function: keeps these processors off runTest's stack frame.
    void runLevelWiringTests()
    {
        beginTest ("APVTS auto gain Strong raises a quiet speech-like signal through processBlock");
        {
            const auto in = aec::test::speechLikeSine (12 * 48000, 1000.0f, -40.0f, 48000.0);
            auto lastSecondDb = [&] (bool agc)
            {
                Proc p;
                setRaw (p, aec::ids::nsLevel, 0.0f);   // NS Off
                if (agc)
                    aec::applyAgcPreset (p.getAPVTS(), aec::AgcPreset::Strong);   // also switches agcEnabled on
                p.getEchoCancellerEngine().setAgcSpeechProbabilityForTesting (1.0f);
                prepare (p, 48000.0, 480);
                const auto out = run (p, in, 480);
                return aec::test::rmsDb (out.data() + out.size() - 48000, 48000);
            };

            const float off = lastSecondDb (false), on = lastSecondDb (true);
            logMessage ("processor AGC Strong: last-second rms " + juce::String (off, 1) + " -> " + juce::String (on, 1) + " dBFS");
            expectGreaterThan (on - off, 10.0f);
        }

        beginTest ("APVTS limiter keeps a +6 dBFS input at or below -1 dBFS through processBlock");
        {
            const auto in = aec::test::sine (3 * 48000, 997.0f, 2.0f, 48000.0);
            auto peakAfterStart = [&] (bool limiter)
            {
                Proc p;
                setRaw (p, aec::ids::nsLevel, 0.0f);
                setRaw (p, aec::ids::limiterEnabled, limiter ? 1.0f : 0.0f);
                prepare (p, 48000.0, 480);
                const auto out = run (p, in, 480);
                float peak = 0.0f;
                for (size_t i = 48000; i < out.size(); ++i)   // past the start-up (dry, unlimited) priming
                    peak = juce::jmax (peak, std::abs (out[i]));
                return peak;
            };

            const float on = peakAfterStart (true), off = peakAfterStart (false);
            logMessage ("processor limiter: max |out| on " + juce::String (on, 6) + ", off " + juce::String (off, 6));
            expectLessOrEqual (on, aec::OutputLimiter::kCeilingLinear + 1.0e-6f);
            expectGreaterThan (off, aec::OutputLimiter::kCeilingLinear, "the input never reached the ceiling");
        }
    }

    void runTest() override
    {
        runLevelWiringTests();

        beginTest ("reported latency is 30 ms");
        {
            Proc p;
            prepare (p, 48000.0, 480);
            expectEquals (p.getLatencySamples(), 1440);
        }

        beginTest ("bypassed output == input delayed by latency, bit exact");
        {
            Proc p;
            setRaw (p, "aecEnabled", 0.0f);
            prepare (p, 48000.0, 480);

            const auto in = aec::test::whiteNoise (48000, 0.3f, 11);
            const auto out = run (p, in, 480);
            expect (delayedExactly (in, out, 1440));
        }

        beginTest ("power off is a true pass-through with the limiter on");
        {
            Proc p;
            setRaw (p, "aecEnabled", 0.0f);
            expect (raw (p, aec::ids::limiterEnabled) >= 0.5f, "the limiter is not on by default");
            prepare (p, 48000.0, 480);

            const auto in = aec::test::whiteNoise (48000, 2.0f, 22);   // +6 dBFS peaks
            const auto out = run (p, in, 480);
            float peak = 0.0f;
            for (auto x : out)
                peak = juce::jmax (peak, std::abs (x));
            expect (peak > 1.0f, "values above full scale were not preserved");
            expect (delayedExactly (in, out, 1440));
        }

        beginTest ("host bypass == pill bypass");
        {
            const auto in = aec::test::whiteNoise (48000, 0.3f, 12);

            Proc pill;
            setRaw (pill, "aecEnabled", 0.0f);
            prepare (pill, 48000.0, 480);
            const auto a = run (pill, in, 480);

            Proc host;
            prepare (host, 48000.0, 480);
            const auto b = run (host, in, 480, true);

            expect (a == b);
            expect (delayedExactly (in, b, 1440));
        }

        beginTest ("processor latency at 44.1k/441");
        {
            const auto in = aec::test::whiteNoise (44100 * 3, 0.3f, 13);

            Proc bypassedProc;
            setRaw (bypassedProc, "aecEnabled", 0.0f);
            prepare (bypassedProc, 44100.0, 441);
            expectEquals (bypassedProc.getLatencySamples(), 1323);
            const auto bypassedOut = run (bypassedProc, in, 441);
            expectWithinAbsoluteError (measuredLag (in, bypassedOut, 3000, 44100), 1323, 1);

            Proc processed;
            prepare (processed, 44100.0, 441);
            expectEquals (processed.getLatencySamples(), 1323);
            const auto out = run (processed, in, 441);
            expectWithinAbsoluteError (measuredLag (in, out, 3000, 44100), 1323, 1);
        }

        beginTest ("processor latency at 8k / 11.025k / 16k: wet and dry lag == getLatencySamples");
        {
            for (int rate : { 8000, 11025, 16000 })
            {
                const int block = rate / 100;
                const auto in = aec::test::whiteNoise (block * 300, 0.3f, 14);   // whole blocks only

                Proc bypassedProc;
                setRaw (bypassedProc, "aecEnabled", 0.0f);
                prepare (bypassedProc, (double) rate, block);
                const int latency = bypassedProc.getLatencySamples();
                const auto bypassedOut = run (bypassedProc, in, block);
                expect (delayedExactly (in, bypassedOut, latency), juce::String (rate) + ": dry not delayed by the latency");

                Proc processed;
                prepare (processed, (double) rate, block);
                expectEquals (processed.getLatencySamples(), latency);
                const auto out = run (processed, in, block);
                const int lag = measuredLag (in, out, 3000, rate);
                logMessage (juce::String (rate) + ": getLatencySamples " + juce::String (latency) + ", processed lag " + juce::String (lag));
                expectWithinAbsoluteError (lag, latency, 1);

                if (rate == 16000)
                    expectEquals (latency, 480);
            }
        }

        beginTest ("leaving bypass never fades into priming silence");
        {
            Proc p;
            setRaw (p, "aecEnabled", 0.0f);
            prepare (p, 48000.0, 480);

            const auto in = aec::test::whiteNoise (48000, 0.3f, 14);
            const int half = 24000;
            const std::vector<float> first (in.begin(), in.begin() + half);
            const std::vector<float> second (in.begin() + half, in.end());

            auto out = run (p, first, 480);
            setRaw (p, "aecEnabled", 1.0f);
            const auto out2 = run (p, second, 480);
            out.insert (out.end(), out2.begin(), out2.end());

            // The engine is re-enabled at sample `half`; the dry signal is held for `latency` samples
            int firstBad = -1;
            for (int j = half; j < half + 1440 && firstBad < 0; ++j)
                if (out[(size_t) j] != in[(size_t) (j - 1440)])
                    firstBad = j - half;
            expect (firstBad < 0, "held dry broke at +" + juce::String (firstBad));

            // and the output has no silent hole afterwards
            float maxAbs = 0.0f;
            for (int i = half + 1440; i < half + 1440 + 480; ++i)
                maxAbs = juce::jmax (maxAbs, std::abs (out[(size_t) i]));
            expect (maxAbs > 0.01f, "maxAbs " + juce::String (maxAbs));
        }

        beginTest ("OUT meter shows the dry output while bypassed");
        {
            const auto in = aec::test::whiteNoise (48000, 0.3f, 15);

            Proc pill;
            setRaw (pill, "aecEnabled", 0.0f);
            prepare (pill, 48000.0, 480);
            run (pill, in, 480);
            expect (pill.getEchoCancellerEngine().outPeak.takeDb() > -20.0f);

            Proc host;
            prepare (host, 48000.0, 480);
            run (host, in, 480, true);
            expect (host.getEchoCancellerEngine().outPeak.takeDb() > -20.0f);
        }

        beginTest ("bypass toggle storm");
        {
            Proc p;
            prepare (p, 48000.0, 480);
            const int latencyBefore = p.getLatencySamples();

            const auto in = aec::test::whiteNoise (48000 * 3, 0.1f, 16);
            const auto out = run (p, in, 480, false, 3);

            float maxStep = 0.0f;
            bool finite = true;

            for (size_t i = 0; i < out.size(); ++i)
            {
                finite = finite && std::isfinite (out[i]);
                if (i > 0)
                    maxStep = juce::jmax (maxStep, std::abs (out[i] - out[i - 1]));
            }

            expect (finite);
            expect (maxStep < 0.5f, "max step " + juce::String (maxStep));
            expectEquals (p.getLatencySamples(), latencyBefore);
            expectEquals (latencyBefore, 1440);
        }

        beginTest ("toggle storm on a sine has no click");
        {
            Proc p;
            prepare (p, 48000.0, 480);

            const auto in = aec::test::sine (48000 * 3, 100.0f, 0.5f, 48000.0);
            const auto out = run (p, in, 480, false, 3);

            // Natural step of a 0.5 / 100 Hz sine is 0.5*2*pi*100/48000 = 0.0065. The aligned dry and wet
            // paths are close on a tone, so the ramp adds well under 1/480 = 0.002 per sample; 0.05 is
            // ~7x the natural slope. What it catches is a fade into the engine's priming silence or a
            // misaligned/undelayed path (steps of ~0.5).
            float maxStep = 0.0f;
            for (size_t i = 1; i < out.size(); ++i)
                maxStep = juce::jmax (maxStep, std::abs (out[i] - out[i - 1]));

            expect (maxStep < 0.05f, "max step " + juce::String (maxStep));
        }

        beginTest ("AEC3 error state outputs the delayed dry signal");
        {
            Proc p;
            prepare (p, 48000.0, 480);

            const auto in = aec::test::whiteNoise (48000 * 2, 0.3f, 18);
            const int half = 48000;
            const std::vector<float> first (in.begin(), in.begin() + half);
            const std::vector<float> second (in.begin() + half, in.end());

            auto out = run (p, first, 480);
            p.getEchoCancellerEngine().setErrorForTesting (true);
            const auto out2 = run (p, second, 480);
            out.insert (out.end(), out2.begin(), out2.end());

            expect (delayedExactly (in, out, 1440, half));
            expect (p.getEchoCancellerEngine().outPeak.takeDb() > -20.0f);
        }

        beginTest ("factory reset while bypassed re-activates the reference capture");
        {
            Proc p;
            prepare (p, 48000.0, 480);
            const auto in = aec::test::whiteNoise (48000, 0.1f, 19);
            setRaw (p, "aecEnabled", 0.0f);
            run (p, in, 480);
            const bool pausedWhileBypassed = ! p.getLoopbackCapture().isCaptureActive();

            // OtherTab::factoryReset: aecEnabled = 1 and requestReset back to back
            setRaw (p, "aecEnabled", 1.0f);
            p.getEchoCancellerEngine().requestReset();
            run (p, in, 480);

            expect (pausedWhileBypassed, "capture was not paused while bypassed");
            expect (p.getLoopbackCapture().isCaptureActive(), "capture still paused after the reset");
        }

        beginTest ("factory reset while enabled: no priming silence, lag == getLatencySamples");
        {
            // A sine for holes / clicks, noise (aperiodic) for the lag
            for (const bool noise : { false, true })
            {
                Proc p;
                prepare (p, 48000.0, 480);
                const int latency = p.getLatencySamples(), resetAt = 200 * 480;
                const size_t minLen = 48000 * 5, maxLen = minLen + 2500 * 480;   // spare room: up to 5 s for the swap
                auto in = noise ? aec::test::whiteNoise ((int) maxLen, 0.3f, 20)
                                : aec::test::sine ((int) maxLen, 100.0f, 0.5f, 48000.0);
                std::vector<float> out;
                int swapsAtReset = 0;

                for (size_t pos = 0; pos < in.size(); pos += 480)
                {
                    if (pos >= minLen && p.getEchoCancellerEngine().getAecSwapCount() > swapsAtReset)
                        break;
                    if ((int) pos == resetAt)
                    {
                        swapsAtReset = p.getEchoCancellerEngine().getAecSwapCount();
                        p.getEchoCancellerEngine().requestReset();   // OtherTab::factoryReset, plugin running
                    }
                    const std::vector<float> chunk (in.begin() + (std::ptrdiff_t) pos, in.begin() + (std::ptrdiff_t) pos + 480);
                    const auto o = run (p, chunk, 480);
                    out.insert (out.end(), o.begin(), o.end());
                    if ((int) pos > resetAt && p.getEchoCancellerEngine().getAecSwapCount() <= swapsAtReset)
                        juce::Thread::sleep (2);   // let the worker build the fresh instance
                }
                in.resize (out.size());
                expectEquals (p.getEchoCancellerEngine().getAecSwapCount() - swapsAtReset, 1);

                if (noise)
                {
                    const std::vector<float> a (in.begin() + resetAt, in.end());
                    const std::vector<float> b (out.begin() + resetAt, out.end());
                    const int lag = aec::test::crossCorrelationLag (a, b, 3000);
                    logMessage ("reset while enabled, noise: lag " + juce::String (lag) + ", getLatencySamples " + juce::String (latency));
                    expectWithinAbsoluteError (lag, latency, 1);
                    expectEquals (p.getLatencySamples(), 1440);
                }
                else
                {
                    int best = 0, run0 = 0;
                    float maxStep = 0.0f;
                    for (size_t i = (size_t) latency; i < out.size(); ++i)
                    {
                        run0 = out[i] == 0.0f ? run0 + 1 : 0;
                        best = juce::jmax (best, run0);
                        maxStep = juce::jmax (maxStep, std::abs (out[i] - out[i - 1]));
                    }
                    logMessage ("reset while enabled, sine: longest zero run " + juce::String (best) + ", max step " + juce::String (maxStep, 4));
                    expect (best <= 8, "zero run " + juce::String (best));
                    expect (maxStep < 0.05f, "max step " + juce::String (maxStep));
                }
            }
        }

        beginTest ("unusable host rate: reported latency matches the undelayed pass-through");
        {
            Proc p;
            prepare (p, 100000007.0, 480);   // the engine's resamplers cannot be built
            const auto in = aec::test::whiteNoise (4800, 0.3f, 21);
            const auto out = run (p, in, 480);
            logMessage ("unusable rate: getLatencySamples " + juce::String (p.getLatencySamples()));
            expectEquals (p.getLatencySamples(), 0);
            expect (delayedExactly (in, out, 0));
        }

        beginTest ("setState during processing");
        {
            Proc p;
            prepare (p, 48000.0, 480);
            const int latencyBefore = p.getLatencySamples();

            const auto in = aec::test::whiteNoise (4800, 0.1f, 17);
            run (p, in, 480);

            const juce::String xmlText =
                "<AEC3 captureDeviceId=\"\" uiMode=\"advanced\">"
                "<PARAM id=\"aecEnabled\" value=\"0.0\"/>"
                "<PARAM id=\"lfSuppression\" value=\"40.0\"/>"
                "<PARAM id=\"roomReverb\" value=\"22.0\"/>"
                "</AEC3>";
            juce::MemoryBlock block;
            auto xml = juce::parseXML (xmlText);
            expect (xml != nullptr);
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
            p.setStateInformation (block.getData(), (int) block.getSize());

            const auto out = run (p, in, 480);

            expectEquals (raw (p, "aecEnabled"), 0.0f);
            expectEquals (raw (p, "lfSuppression"), 40.0f);
            expectEquals (raw (p, "roomReverb"), 22.0f);
            expectEquals (p.getLatencySamples(), latencyBefore);

            bool finite = true;
            for (auto x : out)
                finite = finite && std::isfinite (x);
            expect (finite);
        }
    }
};

static ProcessorTests processorTests;
