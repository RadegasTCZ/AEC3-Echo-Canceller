#include <JuceHeader.h>
#include "../Source/EchoCancellerEngine.h"
#include "../Source/Resampler.h"
#include "../Source/Strength.h"
#include "AllocCounter.h"
#include "FakeLoopback.h"
#include "TestSignals.h"

#include "api/audio/audio_processing.h"
#include "api/audio/echo_canceller3_config.h"
#include "api/audio/echo_canceller3_factory.h"
#include "api/environment/environment_factory.h"
#include "modules/audio_processing/audio_buffer.h"
#include "modules/audio_processing/high_pass_filter.h"

namespace
{
using Engine = EchoCancellerEngine;

/** Runs `mic` through a prepared engine in blocks of `block`. If `ref` is given, `refPerBlock (n)`
    reference samples are fed to the loopback before each block (the reference is at the loopback rate). */
std::vector<float> runEngine (Engine& e, const std::vector<float>& mic, int block, aec::test::FakeLoopback& lb,
                              const std::vector<float>* ref = nullptr, double refRatio = 1.0)
{
    std::vector<float> out (mic.size(), 0.0f);
    size_t refPos = 0;
    double refDue = 0.0;

    for (size_t pos = 0; pos < mic.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, mic.size() - pos);

        if (ref != nullptr)
        {
            refDue += n * refRatio;
            const auto want = (size_t) refDue;
            const auto count = std::min (want - refPos, ref->size() - refPos);
            lb.feed (ref->data() + refPos, (int) count);
            refPos += count;
        }

        e.process (mic.data() + pos, out.data() + pos, n, lb);
    }

    return out;
}

/** The bare WebRTC capture chain the engine runs, without NS and with zero render. Upstream default
    config instead of the engine's Normal mapping: with no echo the suppressor tuning has no effect. */
std::vector<float> runAec3Chain (const std::vector<float>& mic)
{
    constexpr int kRate = 48000, kFrame = 480;
    auto ctl = webrtc::EchoCanceller3Factory (webrtc::EchoCanceller3Config {}).Create (webrtc::CreateEnvironment(), kRate, 1, 1);
    webrtc::AudioBuffer render (kRate, 1, kRate, 1, kRate, 1), cap (kRate, 1, kRate, 1, kRate, 1);
    webrtc::HighPassFilter hp (kRate, 1);
    const webrtc::StreamConfig sc (kRate, 1);
    std::vector<float> out (mic.size(), 0.0f), zero ((size_t) kFrame, 0.0f);

    for (size_t f = 0; f + kFrame <= mic.size(); f += kFrame)
    {
        const float* z = zero.data();
        render.CopyFrom (&z, sc);
        render.SplitIntoFrequencyBands();
        ctl->AnalyzeRender (&render);
        render.MergeFrequencyBands();

        const float* ip = mic.data() + f;
        float* op = out.data() + f;
        cap.CopyFrom (&ip, sc);
        ctl->AnalyzeCapture (&cap);
        cap.SplitIntoFrequencyBands();
        hp.Process (&cap, true);
        ctl->SetAudioBufferDelay (0);
        ctl->ProcessCapture (&cap, nullptr, false);
        cap.MergeFrequencyBands();
        cap.CopyTo (sc, &op);
    }

    return out;
}

/** Longest run of exact zeros in x[from..). */
int longestZeroRun (const std::vector<float>& x, size_t from)
{
    int best = 0, run = 0;
    for (size_t i = from; i < x.size(); ++i)
    {
        run = x[i] == 0.0f ? run + 1 : 0;
        best = juce::jmax (best, run);
    }
    return best;
}

std::vector<float> slice (const std::vector<float>& x, size_t from, size_t count)
{
    from = std::min (from, x.size());
    count = std::min (count, x.size() - from);
    return { x.begin() + (std::ptrdiff_t) from, x.begin() + (std::ptrdiff_t) (from + count) };
}

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Engine", "Engine") {}

    int measureLag (int hostRate, int block, int nsLevel, int& latencyOut, int& zeroRun, int seconds = 2, int measureSamples = 0)
    {
        Engine e;
        e.setNoiseLevel (nsLevel);
        e.prepare ((double) hostRate, block);
        latencyOut = e.getLatencySamples();

        aec::test::FakeLoopback lb;
        const auto mic = aec::test::whiteNoise (seconds * hostRate, 0.1f, 11);
        const auto out = runEngine (e, mic, block, lb);
        zeroRun = longestZeroRun (out, (size_t) latencyOut);

        const size_t m = measureSamples > 0 ? (size_t) measureSamples : mic.size();
        return aec::test::crossCorrelationLag (slice (mic, 0, m), slice (out, 0, m + 4000), 4000);
    }

    void runTest() override
    {
        beginTest ("Params{} tuning equals tuningFor (Normal)");
        {
            const Engine::Params p;
            const float v[aec::kNumTuningParams] = { p.lfSuppression, p.lfTransparency, p.hfSuppression, p.hfTransparency,
                                                     p.lfAttack, p.lfDecay, p.hfDucking, p.hfEchoThreshold,
                                                     p.dtLfSuppression, p.dtLfTransparency, p.dtHfSuppression,
                                                     p.dtHfTransparency, p.dtLfAttack, p.dtLfDecay };
            const auto& normal = aec::tuningFor (aec::Strength::Normal);
            for (int i = 0; i < aec::kNumTuningParams; ++i)
                expectEquals (v[i], normal[(size_t) i], aec::kTuningParamIds[(size_t) i]);
        }

        beginTest ("constants");
        {
            expectEquals (Engine::naturalLatency48k(), 1198);
        }

        int lagOff = -1;

        beginTest ("latency 48k NS off == 1440");
        {
            int latency = 0, zeros = 0;
            lagOff = measureLag (48000, 480, 0, latency, zeros);
            logMessage ("48k NS off: getLatencySamples " + juce::String (latency) + ", measured lag " + juce::String (lagOff));
            expectEquals (latency, 1440);
            expect (std::abs (lagOff - 1440) <= 1, "lag " + juce::String (lagOff));
        }

        beginTest ("latency 48k NS 12 dB == NS off");
        {
            int latency = 0, zeros = 0;
            const int lag = measureLag (48000, 480, 2, latency, zeros, 1, 48000 * 3 / 10);
            logMessage ("48k NS 12 dB: measured lag " + juce::String (lag));
            expect (std::abs (lag - lagOff) <= 1, "lag " + juce::String (lag));
        }

        beginTest ("latency with 512 blocks");
        {
            int latency = 0, zeros = 0;
            const int lag = measureLag (48000, 512, 0, latency, zeros);
            logMessage ("48k block 512: measured lag " + juce::String (lag) + ", longest zero run " + juce::String (zeros));
            expect (std::abs (lag - 1440) <= 1, "lag " + juce::String (lag));
            expect (zeros <= 8, "zero run " + juce::String (zeros));
        }

        beginTest ("resampled latency");
        {
            struct Case { int rate, block, expected; };
            for (auto c : { Case { 44100, 441, 1323 }, Case { 44100, 1000, 1323 }, Case { 96000, 1024, 2880 } })
            {
                int latency = 0, zeros = 0;
                const int lag = measureLag (c.rate, c.block, 0, latency, zeros);
                logMessage (juce::String (c.rate) + " block " + juce::String (c.block) + ": getLatencySamples "
                            + juce::String (latency) + ", measured lag " + juce::String (lag)
                            + ", longest zero run " + juce::String (zeros));
                expectEquals (latency, c.expected);
                expect (std::abs (lag - latency) <= 1, "lag " + juce::String (lag));
                expect (zeros <= 8, "zero run " + juce::String (zeros));
            }
        }

        beginTest ("echo removed with reference");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb (48000);
            const int n = 5 * 48000, delay = 48000 * 30 / 1000;
            const auto ref = aec::test::whiteNoise (n, 0.3f, 21);
            std::vector<float> mic ((size_t) n, 0.0f);
            for (int i = delay; i < n; ++i)
                mic[(size_t) i] = 0.5f * ref[(size_t) (i - delay)];

            const auto out = runEngine (e, mic, 480, lb, &ref);
            const float micDb = aec::test::rmsDb (mic.data() + n - 48000, 48000);
            const float outDb = aec::test::rmsDb (out.data() + n - 48000, 48000);
            logMessage ("echo: mic " + juce::String (micDb, 1) + " dB, out " + juce::String (outDb, 1) + " dB");
            expect (outDb <= micDb - 15.0f, "only " + juce::String (micDb - outDb, 1) + " dB removed");
        }

        beginTest ("loopback at 44.1k while host at 48k");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480, 44100);   // known rate: resampler built in prepare, not on the audio thread
            expectEquals (e.getLoopbackResamplerRate(), 44100);
            aec::test::FakeLoopback lb (44100);
            const int n = 5 * 48000, delay = 48000 * 30 / 1000;

            // Band-limited reference: noise at 44.1 kHz, the echo is its 48 kHz version.
            const auto ref44 = aec::test::whiteNoise (5 * 44100 + 4410, 0.3f, 22);
            aec::Resampler up;
            expect (up.prepare (44100, 48000));
            std::vector<float> ref48 ((size_t) aec::Resampler::maxOutputFor ((int) ref44.size(), 44100, 48000));
            ref48.resize ((size_t) up.process (ref44.data(), (int) ref44.size(), ref48.data(), (int) ref48.size()));

            std::vector<float> mic ((size_t) n, 0.0f);
            for (int i = delay; i < n && (size_t) (i - delay) < ref48.size(); ++i)
                mic[(size_t) i] = 0.5f * ref48[(size_t) (i - delay)];

            const auto out = runEngine (e, mic, 480, lb, &ref44, 44100.0 / 48000.0);
            const float micDb = aec::test::rmsDb (mic.data() + n - 48000, 48000);
            const float outDb = aec::test::rmsDb (out.data() + n - 48000, 48000);
            logMessage ("echo, loopback 44.1k: mic " + juce::String (micDb, 1) + " dB, out " + juce::String (outDb, 1) + " dB");
            expect (outDb <= micDb - 15.0f, "only " + juce::String (micDb - outDb, 1) + " dB removed");
        }

        beginTest ("loopback rate change 48k -> 44.1k mid-run: no audio-thread allocation, echo recovers");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480, 48000);
            aec::test::FakeLoopback lb (48000);
            const int seconds = 8, switchBlock = 200, blocks = seconds * 100, delay = 48000 * 30 / 1000;

            // One band-limited reference: noise at 44.1 kHz, its 48 kHz version is fed before the switch
            const auto ref44 = aec::test::whiteNoise (seconds * 44100 + 4410, 0.3f, 23);
            aec::Resampler up;
            expect (up.prepare (44100, 48000));
            std::vector<float> ref48 ((size_t) aec::Resampler::maxOutputFor ((int) ref44.size(), 44100, 48000));
            ref48.resize ((size_t) up.process (ref44.data(), (int) ref44.size(), ref48.data(), (int) ref48.size()));

            const int n = blocks * 480;
            std::vector<float> mic ((size_t) n, 0.0f), out ((size_t) n, 0.0f);
            for (int i = delay; i < n; ++i)
                mic[(size_t) i] = 0.5f * ref48[(size_t) (i - delay)];

            int allocs = -1, rateAtSwitch = 0;
            {
                aec::test::ScopedAllocCheck check;
                // Count from block 100: the Debug AEC3 library logs its initial delay estimate through
                // RTC_LOG, which allocates (Release has no logging). The rate switch at block 200 is covered.
                int atWarm = 0;
                for (int b = 0; b < blocks; ++b)
                {
                    if (b == 100) atWarm = check.count();
                    if (b < switchBlock)
                    {
                        lb.feed (ref48.data() + b * 480, 480);
                    }
                    else
                    {
                        if (b == switchBlock)
                        {
                            lb.setRate (44100);
                            rateAtSwitch = e.getLoopbackResamplerRate();
                        }
                        lb.feed (ref44.data() + b * 441, 441);
                    }

                    e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);

                    if (b == switchBlock)
                        juce::Thread::sleep (5);   // let the worker build the 44.1k resampler
                }
                allocs = check.count() - atWarm;
            }

            const float micDb = aec::test::rmsDb (mic.data() + n - 48000, 48000);
            const float outDb = aec::test::rmsDb (out.data() + n - 48000, 48000);
            logMessage ("rate change: resampler " + juce::String (rateAtSwitch) + " -> " + juce::String (e.getLoopbackResamplerRate())
                        + ", allocations " + juce::String (allocs) + ", mic " + juce::String (micDb, 1)
                        + " dB, out " + juce::String (outDb, 1) + " dB");
            expectEquals (rateAtSwitch, 48000);
            expectEquals (e.getLoopbackResamplerRate(), 44100);
            expectEquals (allocs, 0);
            expect (outDb <= micDb - 15.0f, "only " + juce::String (micDb - outDb, 1) + " dB removed");
        }

        beginTest ("no reference");
        {
            Engine e;
            e.setNoiseLevel (2);
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const int n = 5 * 48000;
            const auto mic = aec::test::whiteNoise (n, 0.1f, 31);
            const auto out = runEngine (e, mic, 480, lb);
            const float inDb = aec::test::rmsDb (mic.data() + n - 48000, 48000);
            const float outDb = aec::test::rmsDb (out.data() + n - 48000, 48000);
            logMessage ("no reference, NS 12 dB: in " + juce::String (inDb, 1) + " dB, out " + juce::String (outDb, 1) + " dB");
            expect (outDb > -60.0f, "output silent");
            expect (outDb <= inDb - 6.0f, "only " + juce::String (inDb - outDb, 1) + " dB quieter");
        }

        beginTest ("NS off is transparent");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const int n = 3 * 48000, lat = e.getLatencySamples();
            const auto mic = aec::test::sine (n, 1000.0f, 0.3f, 48000.0);
            const auto out = runEngine (e, mic, 480, lb);

            // The bare WebRTC chain without NS (AnalyzeCapture -> Split -> HPF -> ProcessCapture -> Merge)
            // already delays by kSplitDelay48k + kAecDelay48k; NS Off must add nothing but the rest of the delay.
            const auto chain = runAec3Chain (mic);
            const int chainLag = Engine::kSplitDelay48k + Engine::kAecDelay48k;

            const int start = 48000, len = n - start - lat;
            std::vector<float> err ((size_t) len), errChain ((size_t) len);
            for (int i = 0; i < len; ++i)
            {
                err[(size_t) i] = out[(size_t) (start + lat + i)] - mic[(size_t) (start + i)];
                errChain[(size_t) i] = out[(size_t) (start + lat + i)] - chain[(size_t) (start + chainLag + i)];
            }
            const float errDb = aec::test::rmsDb (err.data(), len);
            const float chainDb = aec::test::rmsDb (errChain.data(), len);
            logMessage ("NS off, 1 kHz sine aligned by " + juce::String (lat) + ": rms error vs input " + juce::String (errDb, 1)
                        + " dB, vs bare AEC3 chain " + juce::String (chainDb, 1) + " dB");
            expect (chainDb < -40.0f, "rms error vs AEC3 chain " + juce::String (chainDb, 1) + " dB");
            expect (errDb < -20.0f, "rms error vs input " + juce::String (errDb, 1) + " dB");
        }

        beginTest ("residual = preNs - nsOut");
        {
            // normal (NS 12) + residual (NS 12) must equal the NS-off output (= delayed preNs).
            Engine normal, residual, off;
            normal.setNoiseLevel (2);
            residual.setNoiseLevel (2);
            residual.setMonitorNoiseResidual (true);
            off.setNoiseLevel (0);
            for (auto* e : { &normal, &residual, &off })
                e->prepare (48000.0, 480);

            aec::test::FakeLoopback lb1, lb2, lb3;
            const int n = 3 * 48000;
            const auto mic = aec::test::whiteNoise (n, 0.1f, 41);
            const auto a = runEngine (normal, mic, 480, lb1);
            const auto r = runEngine (residual, mic, 480, lb2);
            const auto p = runEngine (off, mic, 480, lb3);

            const int start = 48000, len = n - start;
            std::vector<float> diff ((size_t) len);
            for (int i = 0; i < len; ++i)
                diff[(size_t) i] = a[(size_t) (start + i)] + r[(size_t) (start + i)] - p[(size_t) (start + i)];
            const float resDb = aec::test::rmsDb (r.data() + start, len);
            const float diffDb = aec::test::rmsDb (diff.data(), len);
            logMessage ("residual rms " + juce::String (resDb, 1) + " dB, out + residual - preNs " + juce::String (diffDb, 1) + " dB");
            expect (resDb > -60.0f, "residual too quiet");
            expect (diffDb < -50.0f, "identity error " + juce::String (diffDb, 1) + " dB");
        }

        beginTest ("nsLevel automation");
        {
            Engine e;
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            // 2 s changing every block, 4 s changing every 30 blocks (each change is applied), then 3 s at 21 dB
            const int blocks = 300 + 400 + 300;
            const auto mic = aec::test::whiteNoise (blocks * 480, 0.1f, 51);
            std::vector<float> out (mic.size(), 0.0f);
            int allocs = -1;
            {
                aec::test::ScopedAllocCheck check;
                for (int b = 0; b < blocks; ++b)
                {
                    const int level = b < 300 ? b % 5 : b < 700 ? ((b - 300) / 30) % 5 : 4;
                    e.setNoiseLevel (level);
                    e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);

                    if ((b >= 300 && (b - 300) % 30 == 29) || b == 730)
                        juce::Thread::sleep (2);   // give the worker time to build the suppressor
                }
                allocs = check.count();
            }

            float maxStep = 0.0f;
            for (size_t i = 1; i < out.size(); ++i)
                maxStep = juce::jmax (maxStep, std::abs (out[i] - out[i - 1]));
            logMessage ("automation: max step " + juce::String (maxStep, 3) + ", audio-thread allocations " + juce::String (allocs));
            expect (maxStep <= 0.5f, "step " + juce::String (maxStep));
            expectEquals (allocs, 0);

            // the last change (to 21 dB) took effect
            const int tail = 100 * 480;
            const float inDb = aec::test::rmsDb (mic.data() + mic.size() - tail, tail);
            const float outDb = aec::test::rmsDb (out.data() + out.size() - tail, tail);
            logMessage ("automation: final 21 dB, in " + juce::String (inDb, 1) + " dB, out " + juce::String (outDb, 1) + " dB");
            expect (outDb <= inDb - 6.0f, "final level not applied");
        }

        beginTest ("nsLevel automation on a sine has no click");
        {
            Engine e;
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const int blocks = 700;
            const auto mic = aec::test::sine (blocks * 480, 100.0f, 0.5f, 48000.0);
            std::vector<float> out (mic.size(), 0.0f);
            for (int b = 0; b < blocks; ++b)
            {
                e.setNoiseLevel ((b / 30) % 5);   // every level change is applied, incl 0 <-> on
                e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);
                if (b % 30 == 29)
                    juce::Thread::sleep (2);   // give the worker time to build the suppressor
            }

            // Natural step of a 0.5 / 100 Hz sine is 0.0065; 0.05 catches a hole or a hard edge
            const size_t from = 100 * 480;
            const float maxStep = maxStepFrom (out, from);
            const int zeros = longestZeroRun (out, from);
            logMessage ("nsLevel sine automation: max step " + juce::String (maxStep, 4) + ", longest zero run " + juce::String (zeros));
            expect (maxStep <= 0.05f, "step " + juce::String (maxStep));
            expect (zeros <= 8, "zero run " + juce::String (zeros));
        }

        beginTest ("AEC3 swap on a sine: no hole, no click, no audio-thread allocation");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const int blocks = 600, changeAt = 200;
            const auto mic = aec::test::sine (blocks * 480, 100.0f, 0.5f, 48000.0);
            std::vector<float> out (mic.size(), 0.0f);
            const auto strong = paramsFor (aec::Strength::Strong);

            int allocs = -1, swapsAtChange = -1;
            {
                aec::test::ScopedAllocCheck check;
                // Count from block 100: the Debug AEC3 library logs during its initial warm-up (see the
                // loopback rate-change test). The swap after block 224 is covered.
                int atWarm = 0;
                for (int b = 0; b < blocks; ++b)
                {
                    if (b == 100) atWarm = check.count();
                    if (b == changeAt) swapsAtChange = e.getAecSwapCount();
                    e.setParams (b >= changeAt ? strong : Engine::Params {});
                    e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);
                    if (b > changeAt + 20 && b < changeAt + 60)
                        juce::Thread::sleep (2);   // let the worker build the new instance
                }
                allocs = check.count() - atWarm;
            }

            const size_t from = 100 * 480;
            const float maxStep = maxStepFrom (out, from);
            const int zeros = longestZeroRun (out, from);
            logMessage ("AEC3 swap on a sine: swaps " + juce::String (e.getAecSwapCount()) + ", max step " + juce::String (maxStep, 4)
                        + ", longest zero run " + juce::String (zeros) + ", audio-thread allocations " + juce::String (allocs));
            expectEquals (swapsAtChange, 0);
            expectEquals (e.getAecSwapCount(), 1);
            expect (zeros <= 8, "zero run " + juce::String (zeros));
            expect (maxStep <= 0.05f, "step " + juce::String (maxStep));
            expectEquals (allocs, 0);
        }

        beginTest ("measured lag == getLatencySamples after an AEC3 swap, an enable toggle and a gap reset");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const int blocks = 800, latency = e.getLatencySamples();
            const auto mic = aec::test::whiteNoise (blocks * 480, 0.1f, 61);
            std::vector<float> out (mic.size(), 0.0f);
            const auto strong = paramsFor (aec::Strength::Strong);

            for (int b = 0; b < blocks; ++b)
            {
                e.setParams (b >= 100 ? strong : Engine::Params {});   // swap after block 124
                e.setEnabled (b < 350 || b >= 370);                    // enable toggle
                if (b == 600)
                    juce::Thread::sleep (150);                         // processBlock gap > 100 ms
                e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);
                if (b > 120 && b < 160)
                    juce::Thread::sleep (2);
            }

            auto lagAt = [&] (int fromBlock)
            {
                const size_t s = (size_t) fromBlock * 480, m = 100 * 480;
                return aec::test::crossCorrelationLag (slice (mic, s, m), slice (out, s, m + 4000), 4000);
            };
            const int afterSwap = lagAt (200), afterToggle = lagAt (420), afterGap = lagAt (650);
            logMessage ("lag after swap " + juce::String (afterSwap) + ", after enable toggle " + juce::String (afterToggle)
                        + ", after gap " + juce::String (afterGap) + " (getLatencySamples " + juce::String (latency) + ")");
            expectEquals (e.getAecSwapCount(), 1);
            expect (std::abs (afterSwap - latency) <= 1, "after swap " + juce::String (afterSwap));
            expect (std::abs (afterToggle - latency) <= 1, "after toggle " + juce::String (afterToggle));
            expect (std::abs (afterGap - latency) <= 1, "after gap " + juce::String (afterGap));
        }

        // Factory reset after capture was paused (AEC3 Error, or bypass with the reset's aecEnabled = 1):
        // the reference must be re-activated, or WASAPI keeps discarding it and the echo stays.
        for (const bool fromError : { true, false })
        {
            beginTest (fromError ? "factory reset from AEC3 Error re-activates the reference"
                                 : "factory reset while disabled re-activates the reference");
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480, 48000);
            aec::test::FakeLoopback lb (48000);
            const int blocks = 800, pauseAt = 100, resetAt = 200, delay = 48000 * 30 / 1000;
            const auto ref = aec::test::whiteNoise (blocks * 480, 0.3f, 71);
            std::vector<float> mic (ref.size(), 0.0f), out (ref.size(), 0.0f);
            for (int i = delay; i < (int) mic.size(); ++i)
                mic[(size_t) i] = 0.5f * ref[(size_t) (i - delay)];

            bool pausedBeforeReset = false;
            for (int b = 0; b < blocks; ++b)
            {
                if (b == pauseAt)
                {
                    if (fromError) e.setErrorForTesting (true);
                    else           e.setEnabled (false);
                }
                if (b == resetAt)
                {
                    pausedBeforeReset = ! lb.active;
                    e.setEnabled (true);   // OtherTab::factoryReset sets aecEnabled = 1 with the reset
                    e.requestReset();
                }
                lb.feed (ref.data() + b * 480, 480);
                e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);
            }

            const int n = (int) mic.size();
            const float micDb = aec::test::rmsDb (mic.data() + n - 48000, 48000);
            const float outDb = aec::test::rmsDb (out.data() + n - 48000, 48000);
            logMessage (juce::String (fromError ? "reset from error" : "reset while disabled") + ": capture active "
                        + juce::String (lb.active ? "yes" : "no") + ", mic " + juce::String (micDb, 1) + " dB, out " + juce::String (outDb, 1) + " dB");
            expect (pausedBeforeReset, "capture was not paused");
            expect (! e.hasError());
            expect (lb.active, "capture still paused after the reset");
            expect (outDb <= micDb - 15.0f, "only " + juce::String (micDb - outDb, 1) + " dB removed");
        }

        beginTest ("factory reset while running: fresh AEC3 swapped in, no hole, no audio-thread allocation");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const int blocks = 600, resetAt = 200, latency = e.getLatencySamples();
            const int maxBlocks = blocks + 2500;   // room to keep processing until the worker's swap lands (5 s)
            const auto mic = aec::test::sine (maxBlocks * 480, 100.0f, 0.5f, 48000.0);
            std::vector<float> out (mic.size(), 0.0f);

            int allocs = -1;
            int processed = 0;
            {
                aec::test::ScopedAllocCheck check;
                int atWarm = 0;   // from block 100: Debug AEC3 logs (allocates) during its initial warm-up
                for (int b = 0; b < maxBlocks; ++b)
                {
                    if (b >= blocks && e.getAecSwapCount() >= 1) break;
                    if (b == 100) atWarm = check.count();
                    if (b == resetAt) e.requestReset();   // OtherTab::factoryReset (message thread)
                    e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);
                    processed = b + 1;
                    if (b > resetAt && e.getAecSwapCount() < 1)
                        juce::Thread::sleep (2);           // let the worker build the fresh instance
                }
                allocs = check.count() - atWarm;
            }
            out.resize ((size_t) processed * 480);

            const size_t from = 100 * 480;
            const float maxStep = maxStepFrom (out, from);
            const int zeros = longestZeroRun (out, from);
            logMessage ("reset while running: swaps " + juce::String (e.getAecSwapCount()) + ", latency " + juce::String (e.getLatencySamples())
                        + ", max step " + juce::String (maxStep, 4) + ", longest zero run " + juce::String (zeros)
                        + ", audio-thread allocations " + juce::String (allocs));
            expectEquals (e.getAecSwapCount(), 1);
            expectEquals (e.getLatencySamples(), latency);
            expect (zeros <= 8, "zero run " + juce::String (zeros));
            expect (maxStep <= 0.05f, "step " + juce::String (maxStep));
            expectEquals (allocs, 0);
        }

        // A FIFO reset re-primes the pipeline; nothing captured before it may come out afterwards.
        for (const bool gap : { true, false })
            for (const int ns : { 0, 2 })
            {
                beginTest (juce::String (gap ? "gap reset" : "enable transition") + ", NS " + juce::String (ns)
                           + ": no pre-reset audio in the output");
                Engine e;
                e.setNoiseLevel (ns);
                e.prepare (48000.0, 480);
                aec::test::FakeLoopback lb;
                const auto noise = aec::test::whiteNoise (300 * 480, 0.3f, 91);
                const std::vector<float> silence (480, 0.0f);
                std::vector<float> scratch (480, 0.0f), after (200 * 480, 0.0f);

                for (int b = 0; b < 300; ++b)
                    e.process (noise.data() + b * 480, scratch.data(), 480, lb);

                if (gap)
                {
                    juce::Thread::sleep (150);   // processBlock gap > 100 ms
                }
                else
                {
                    e.setEnabled (false);
                    for (int b = 0; b < 5; ++b)
                        e.process (silence.data(), scratch.data(), 480, lb);
                    e.setEnabled (true);
                }

                for (int b = 0; b < 200; ++b)
                    e.process (silence.data(), after.data() + b * 480, 480, lb);

                const int latency = e.getLatencySamples();
                float maxAbs = 0.0f, maxLate = 0.0f;
                for (int i = 0; i < (int) after.size(); ++i)
                {
                    maxAbs = juce::jmax (maxAbs, std::abs (after[(size_t) i]));
                    if (i >= latency)
                        maxLate = juce::jmax (maxLate, std::abs (after[(size_t) i]));
                }
                logMessage ("after reset on silence: max " + juce::String (maxAbs, 6) + " (" + juce::String (juce::Decibels::gainToDecibels (maxAbs, -200.0f), 1)
                            + " dBFS; pre-reset peak 0.3), max after one latency (" + juce::String (latency) + ") " + juce::String (maxLate, 6));
                // The first latency samples are exactly silent; after that only the faded ringing of the
                // band split / AEC3 / suppressor state is left (they cannot be cleared without allocating).
                expectEquals (maxAbs, maxLate, "non-zero output within one latency");
                expect (maxAbs <= 2.0e-3f, "pre-reset audio, max " + juce::String (maxAbs, 6));
            }

        beginTest ("loopback resampler build failure: reference silent, bounded retry, then recovers");
        {
            constexpr int kBadRate = 100000007;   // speex refuses the ratio (its filter table would overflow)
            aec::Resampler probe;
            expect (! probe.prepare (kBadRate, 48000), "speex accepted the test rate");

            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480, kBadRate);
            const int rateAfterPrepare = e.getLoopbackResamplerRate();
            aec::test::FakeLoopback lb (kBadRate);
            const auto ref = aec::test::whiteNoise (480, 0.3f, 101);
            const std::vector<float> mic (480, 0.0f);
            std::vector<float> out (480, 0.0f);

            for (int b = 0; b < 300; ++b)   // 3 s of host audio
            {
                lb.feed (ref.data(), 480);
                e.process (mic.data(), out.data(), 480, lb);
                juce::Thread::sleep (1);     // the worker tries (and fails) to build the resampler
            }
            const float refDb = e.refPeak.takeDb();
            const int jobs = e.getLoopbackJobCount();
            const int rateWhileFailing = e.getLoopbackResamplerRate();

            lb.setRate (44100);
            for (int b = 0; b < 50; ++b)
            {
                lb.feed (ref.data(), 441);
                e.process (mic.data(), out.data(), 480, lb);
                juce::Thread::sleep (1);
            }

            logMessage ("bad loopback rate: resampler rate after prepare " + juce::String (rateAfterPrepare) + ", while failing "
                        + juce::String (rateWhileFailing) + ", reference peak " + juce::String (refDb, 1) + " dB, rebuild jobs in 3 s "
                        + juce::String (jobs) + ", after the switch to 44.1k " + juce::String (e.getLoopbackResamplerRate()));
            expectEquals (rateAfterPrepare, 0);
            expectEquals (rateWhileFailing, 0);
            expect (refDb <= -100.0f, "reference not silent: " + juce::String (refDb, 1) + " dB");
            expect (jobs >= 1 && jobs <= 4, "rebuild jobs " + juce::String (jobs));
            expectEquals (e.getLoopbackResamplerRate(), 44100);
        }

        beginTest ("unusable host rate: not ready, latency 0");
        {
            Engine e;
            e.prepare (100000007.0, 480);   // speex refuses the ratio
            expect (! e.isReady());
            expectEquals (e.getLatencySamples(), 0);
        }

        beginTest ("low host rates: measured lag == getLatencySamples, 30 ms from 16 kHz");
        {
            for (int rate : { 8000, 11025, 16000 })
            {
                int latency = 0, zeros = 0;
                const int lag = measureLag (rate, rate / 100, 0, latency, zeros);
                logMessage (juce::String (rate) + ": getLatencySamples " + juce::String (latency)
                            + ", measured lag " + juce::String (lag) + ", longest zero run " + juce::String (zeros));
                expect (latency >= juce::roundToInt (0.030 * rate), "latency " + juce::String (latency));
                expect (std::abs (lag - latency) <= 1, juce::String (rate) + ": lag " + juce::String (lag)
                                                         + " vs latency " + juce::String (latency));
                if (rate == 16000)
                    expectEquals (latency, 480);
            }
        }

        beginTest ("non-finite params and latency compensation are replaced by defaults");
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            Engine::Params bad;
            bad.lfSuppression = nan;
            bad.hfTransparency = std::numeric_limits<float>::infinity();
            bad.roomReverb = nan;

            Engine e;
            e.setNoiseLevel (2);
            e.setParams (bad);
            e.setLatencyCompMs (nan);
            e.prepare (48000.0, 480);
            expect (e.isReady() && ! e.hasError());

            aec::test::FakeLoopback lb;
            const auto mic = aec::test::whiteNoise (3 * 48000, 0.1f, 33);
            const auto ref = aec::test::whiteNoise (3 * 48000, 0.1f, 34);
            e.setEnabled (true);
            std::vector<float> out (mic.size(), 0.0f);
            for (size_t pos = 0; pos + 480 <= mic.size(); pos += 480)
            {
                e.setParams (bad);            // every block, as the processor does
                e.setLatencyCompMs (nan);
                lb.feed (ref.data() + pos, 480);
                e.process (mic.data() + pos, out.data() + pos, 480, lb);
            }

            bool finite = true;
            for (auto x : out)
                finite = finite && std::isfinite (x);
            expect (finite, "output finite");
            expect (! e.hasError());
            expectEquals (e.getAecSwapCount(), 0);   // the sanitised params match the prepared instance: no rebuilds
        }

        beginTest ("worker waiting on an unconsumed AEC3 instance does not spin; release() is prompt");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const std::vector<float> mic (480, 0.0f);
            std::vector<float> out (480, 0.0f);
            const auto strong = paramsFor (aec::Strength::Strong);

            // Job A: 23 full blocks + 1 one-sample call run the debounce out without completing another
            // frame, so A is staged and never consumed.
            e.setParams (strong);
            for (int b = 0; b < 23; ++b)
                e.process (mic.data(), out.data(), 480, lb);
            e.process (mic.data(), out.data(), 1, lb);
            expect (waitUntil ([&] { return e.isAecInstanceStagedForTesting(); }), "the worker did not stage A");

            // Job B, the same way (24 samples in all: still no frame). The worker builds B and then has
            // to wait for the audio thread to take A; the engine is disabled, so it never does.
            e.setParams (Engine::Params {});
            for (int i = 0; i < 24; ++i)
                e.process (mic.data(), out.data(), 1, lb);
            e.setEnabled (false);
            e.process (mic.data(), out.data(), 480, lb);
            expect (waitUntil ([&] { return e.getWorkerPollCount() > 0; }), "the worker did not reach the wait");   // B built, the worker waits
            juce::Thread::sleep (100);   // let its first back-off steps pass

            const int before = e.getWorkerPollCount();
            juce::Thread::sleep (1000);
            const int polls = e.getWorkerPollCount() - before;

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            e.release();
            const double releaseMs = juce::Time::getMillisecondCounterHiRes() - t0;

            logMessage ("worker wake-ups in 1 s while waiting " + juce::String (polls) + " (swaps " + juce::String (e.getAecSwapCount())
                        + "), release() " + juce::String (releaseMs, 1) + " ms");
            expectEquals (e.getAecSwapCount(), 0);
            expect (before > 0, "the worker was not waiting");
            expect (polls <= 20, "wake-ups " + juce::String (polls));
            expect (releaseMs < 100.0, "release " + juce::String (releaseMs, 1) + " ms");
        }

        beginTest ("factory reset after a worker creation failure restarts the worker");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const auto mic = aec::test::whiteNoise (480 * 400, 0.1f, 81);
            std::vector<float> out (mic.size(), 0.0f);
            const auto strong = paramsFor (aec::Strength::Strong);
            auto runBlocks = [&] (int from, int to, const Engine::Params& p)
            {
                for (int b = from; b < to; ++b)
                {
                    e.setParams (p);
                    e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);
                    juce::Thread::sleep (1);
                }
            };

            e.failNextJobForTesting();
            runBlocks (0, 100, strong);              // the debounced job throws: AEC3 Error, worker stopped
            const bool halted = e.hasError();
            const int swapsWhenHalted = e.getAecSwapCount();

            e.requestReset();
            const bool clearedAtOnce = ! e.hasError();
            runBlocks (100, 200, strong);            // the reset's rebuild job
            const int swapsAfterReset = e.getAecSwapCount();
            runBlocks (200, 400, Engine::Params {}); // a later parameter change is still served

            logMessage ("worker failure: halted " + juce::String (halted ? "yes" : "no") + ", swaps " + juce::String (swapsWhenHalted)
                        + " -> " + juce::String (swapsAfterReset) + " -> " + juce::String (e.getAecSwapCount()));
            expect (halted, "the failed job did not halt the engine");
            expect (clearedAtOnce, "requestReset did not clear the error");
            expect (! e.hasError());
            expectEquals (swapsWhenHalted, 0);
            expectEquals (swapsAfterReset, 1);
            expectEquals (e.getAecSwapCount(), 2);
        }

        beginTest ("a reset requested while unprepared does not rebuild after the next prepare");
        {
            Engine e;
            e.setNoiseLevel (0);
            e.requestReset();   // nothing to reset yet
            e.prepare (48000.0, 480);
            aec::test::FakeLoopback lb;
            const std::vector<float> mic (480, 0.0f);
            std::vector<float> out (480, 0.0f);
            for (int b = 0; b < 100; ++b)
            {
                e.process (mic.data(), out.data(), 480, lb);
                juce::Thread::sleep (2);
            }
            logMessage ("stale reset: swaps " + juce::String (e.getAecSwapCount()));
            expectEquals (e.getAecSwapCount(), 0);
        }

        beginTest ("requestReset racing prepare / release after a worker failure neither crashes nor hangs");
        {
            Engine e;
            e.setNoiseLevel (0);
            std::atomic<bool> stop { false };
            std::atomic<int> resets { 0 };
            std::thread resetter ([&]
            {
                while (! stop.load())
                {
                    e.requestReset();
                    ++resets;
                    std::this_thread::yield();
                }
            });

            const auto mic = aec::test::whiteNoise (480 * 30, 0.1f, 91);
            std::vector<float> out (mic.size(), 0.0f);
            aec::test::FakeLoopback lb;
            const auto strong = paramsFor (aec::Strength::Strong);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            int cycles = 0;
            while (juce::Time::getMillisecondCounterHiRes() - t0 < 1000.0)
            {
                e.prepare (48000.0, 480);
                e.failNextJobForTesting();
                e.setParams (strong);
                for (int b = 0; b < 30; ++b)
                {
                    e.process (mic.data() + b * 480, out.data() + b * 480, 480, lb);
                    if (b % 8 == 0) juce::Thread::sleep (1);
                }
                e.release();
                ++cycles;
            }
            stop.store (true);
            resetter.join();
            logMessage ("lifecycle race: " + juce::String (cycles) + " prepare/release cycles, " + juce::String (resets.load()) + " resets");
            expect (cycles > 0);
        }
    }

    /** Polls `done` every millisecond until it holds or `timeoutMs` has passed. */
    template <typename Fn>
    static bool waitUntil (Fn done, int timeoutMs = 5000)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        while (! done())
        {
            if (juce::Time::getMillisecondCounterHiRes() - t0 > timeoutMs)
                return false;
            juce::Thread::sleep (1);
        }
        return true;
    }

    static Engine::Params paramsFor (aec::Strength s)
    {
        const auto& v = aec::tuningFor (s);
        Engine::Params p;
        float* f[aec::kNumTuningParams] = { &p.lfSuppression, &p.lfTransparency, &p.hfSuppression, &p.hfTransparency,
                                            &p.lfAttack, &p.lfDecay, &p.hfDucking, &p.hfEchoThreshold,
                                            &p.dtLfSuppression, &p.dtLfTransparency, &p.dtHfSuppression,
                                            &p.dtHfTransparency, &p.dtLfAttack, &p.dtLfDecay };
        for (int i = 0; i < aec::kNumTuningParams; ++i)
            *f[i] = v[(size_t) i];
        return p;
    }

    static float maxStepFrom (const std::vector<float>& x, size_t from)
    {
        float m = 0.0f;
        for (size_t i = juce::jmax ((size_t) 1, from); i < x.size(); ++i)
            m = juce::jmax (m, std::abs (x[i] - x[i - 1]));
        return m;
    }
};

static EngineTests engineTests;
} // namespace
