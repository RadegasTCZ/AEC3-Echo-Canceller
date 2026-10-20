#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "AllocCounter.h"
#include "TestSignals.h"
#include "../Source/OutputLimiter.h"
#include "../Source/AgcStage.h"
#include "../Source/AgcPresets.h"
#include "../Source/PluginProcessor.h"

namespace
{
    class LevelTests final : public juce::UnitTest
    {
    public:
        LevelTests() : juce::UnitTest ("Level", "Level") {}

        // Runs the sine through the limiter frame by frame; returns max |out - in| and max |out|.
        struct Result { float maxDiff = 0.0f, maxOut = 0.0f, lastReductionDb = 0.0f, maxReductionDb = 0.0f; };

        static Result run (float amp, int frames)
        {
            aec::OutputLimiter lim;
            const int n = frames * aec::OutputLimiter::kFrameSize;
            const auto in = aec::test::sine (n, 997.0f, amp, 48000.0);
            std::vector<float> out ((size_t) n);
            Result r;

            for (int f = 0; f < frames; ++f)
            {
                const int o = f * aec::OutputLimiter::kFrameSize;
                lim.process (in.data() + o, out.data() + o);
                r.lastReductionDb = lim.getLastReductionDb();
                r.maxReductionDb = std::max (r.maxReductionDb, r.lastReductionDb);
            }

            for (int i = 0; i < n; ++i)
            {
                r.maxDiff = std::max (r.maxDiff, std::abs (out[(size_t) i] - in[(size_t) i]));
                r.maxOut = std::max (r.maxOut, std::abs (out[(size_t) i]));
            }
            return r;
        }


        struct AgcResult { float lastSecondRmsDb = -200.0f, lastSecondMax = 0.0f, gainAt1s = 0.0f, gainEnd = 0.0f; };

        // Speech-like gated 1 kHz sine (aec::test::speechLikeSine): 300 ms at rmsDbfs, 100 ms gap at rmsDbfs - gapDb.
        // The rms is measured over the "on" frames of the last second only; override < 0 runs the real VAD.
        static AgcResult runAgc (const aec::AgcSettings& s, float rmsDbfs, float override, int seconds, float gapDb = 35.0f)
        {
            aec::AgcStage agc (s);
            const int frames = seconds * 100;
            const int fs = aec::AgcStage::kFrameSize;
            const auto in = aec::test::speechLikeSine (frames * fs, 1000.0f, rmsDbfs, 48000.0, gapDb);
            std::vector<float> buf ((size_t) fs);
            AgcResult r;
            double sumSq = 0.0;
            int onFrames = 0;
            for (int f = 0; f < frames; ++f)
            {
                const bool on = aec::test::speechLikeOn ((size_t) (f * fs), 48000.0);
                for (int i = 0; i < fs; ++i)
                    buf[(size_t) i] = in[(size_t) (f * fs + i)];
                agc.process (buf.data(), override);
                if (f == 99) r.gainAt1s = agc.getLastGainDb();
                if (f >= frames - 100)
                {
                    for (float x : buf) r.lastSecondMax = std::max (r.lastSecondMax, std::abs (x));
                    if (on)
                    {
                        ++onFrames;
                        for (float x : buf) sumSq += (double) x * x;
                    }
                }
            }
            r.gainEnd = agc.getLastGainDb();
            r.lastSecondRmsDb = 10.0f * std::log10 ((float) (sumSq / ((double) onFrames * fs)) + 1e-20f);
            return r;
        }

        void runTest() override
        {
            beginTest ("OutputLimiter: quiet signal passes unchanged");
            {
                const auto r = run (0.1f, 100);
                expectLessOrEqual (r.maxDiff, 1e-6f);
            }

            beginTest ("OutputLimiter: +6 dBFS sine never exceeds -1 dBFS");
            {
                const auto r = run (2.0f, 200);
                expectLessOrEqual (r.maxOut, aec::OutputLimiter::kCeilingLinear + 1e-6f);
                expectGreaterThan (r.maxReductionDb, 3.0f);
            }

            beginTest ("OutputLimiter: -0.5 dBFS peaks are held at -1 dBFS, -3 dBFS pass");
            {
                const auto a = run (0.7079458f, 100);
                expectLessOrEqual (a.maxDiff, 1e-6f);
                const auto b = run (0.9440609f, 100);
                expectLessOrEqual (b.maxOut, aec::OutputLimiter::kCeilingLinear);
            }

            beginTest ("OutputLimiter: process does not allocate");
            {
                aec::OutputLimiter lim;
                const auto in = aec::test::sine (aec::OutputLimiter::kFrameSize, 997.0f, 2.0f, 48000.0);
                std::vector<float> out (in.size());
                int allocs = -1;
                {
                    aec::test::ScopedAllocCheck check;
                    for (int i = 0; i < 100; ++i)
                        lim.process (in.data(), out.data());
                    allocs = check.count();
                }
                expectEquals (allocs, 0);
            }

            beginTest ("AgcSettings defaults are the Normal row");
            {
                const aec::AgcSettings d;
                expect (! d.enabled);
                expectEquals (d.headroomDb, 5.0f);
                expectEquals (d.maxGainDb, 30.0f);
                expectEquals (d.adaptSpeedDbPerSec, 6.0f);
                expectEquals (d.noiseCeilingDbfs, -50.0f);
                expect (d == aec::AgcSettings());
            }

            beginTest ("AgcSettings::sanitised replaces NaN and clamps");
            {
                aec::AgcSettings s;
                s.headroomDb = std::numeric_limits<float>::quiet_NaN();
                s.maxGainDb = 99.0f;
                s.noiseCeilingDbfs = -10.0f;
                s.adaptSpeedDbPerSec = std::numeric_limits<float>::infinity();
                const auto c = s.sanitised();
                expectEquals (c.headroomDb, 5.0f);
                expectEquals (c.maxGainDb, 50.0f);
                expectEquals (c.noiseCeilingDbfs, -20.0f);
                expectEquals (c.adaptSpeedDbPerSec, 6.0f);
            }

            beginTest ("AgcStage raises quiet speech toward the target");
            {
                const auto r = runAgc (aec::AgcSettings(), -40.0f, 1.0f, 10);
                logMessage ("raise: rms " + juce::String (r.lastSecondRmsDb) + " dB, max " + juce::String (r.lastSecondMax)
                            + ", gain " + juce::String (r.gainEnd) + " dB");
                expectGreaterOrEqual (r.lastSecondRmsDb, -40.0f + 15.0f);
                expectLessThan (r.lastSecondMax, 1.0f);
            }

            beginTest ("AgcStage leaves non-speech alone");
            {
                const auto r = runAgc (aec::AgcSettings(), -40.0f, 0.0f, 10);
                logMessage ("nonspeech: rms " + juce::String (r.lastSecondRmsDb) + " dB, gain@1s " + juce::String (r.gainAt1s)
                            + ", gain end " + juce::String (r.gainEnd));
                expectLessOrEqual (r.lastSecondRmsDb, -40.0f + 15.0f + 0.5f);
                expectLessOrEqual (r.gainEnd, r.gainAt1s + 0.01f);
            }

            beginTest ("AgcStage max gain caps the boost");
            {
                aec::AgcSettings g;
                g.headroomDb = 8.0f; g.maxGainDb = 20.0f; g.adaptSpeedDbPerSec = 3.0f; g.noiseCeilingDbfs = -60.0f;
                const auto r = runAgc (g, -50.0f, 1.0f, 20, 32.0f);
                logMessage ("cap: gain " + juce::String (r.gainEnd) + " dB");
                expectLessOrEqual (r.gainEnd, 20.01f);
            }

            beginTest ("AgcStage initial gain: default min (15, max gain), override clamped to [0, max gain]");
            {
                aec::AgcSettings capped;
                capped.maxGainDb = 10.0f;
                expectEquals (aec::AgcStage (aec::AgcSettings()).getLastGainDb(), 15.0f);
                expectEquals (aec::AgcStage (capped).getLastGainDb(), 10.0f);
                expectEquals (aec::AgcStage (aec::AgcSettings(), 4.5f).getLastGainDb(), 4.5f);
                expectEquals (aec::AgcStage (capped, 25.0f).getLastGainDb(), 10.0f);
                expectEquals (aec::AgcStage (aec::AgcSettings(), std::numeric_limits<float>::quiet_NaN()).getLastGainDb(), 15.0f);

                // The override is the gain actually applied from the first frame on
                aec::AgcStage agc (aec::AgcSettings(), 4.0f);
                auto frame = aec::test::sine (aec::AgcStage::kFrameSize, 1000.0f, 0.1f, 48000.0);
                const auto in = frame;
                agc.process (frame.data(), 0.0f);   // non-speech: no gain change
                const float appliedDb = aec::test::rmsDb (frame.data(), (int) frame.size()) - aec::test::rmsDb (in.data(), (int) in.size());
                logMessage ("initial gain 4 dB: applied " + juce::String (appliedDb, 3) + " dB");
                expectWithinAbsoluteError (appliedDb, 4.0f, 0.05f);
            }

            beginTest ("AgcStage process does not allocate");
            {
                aec::AgcStage agc { aec::AgcSettings() };
                const auto in = aec::test::sine (aec::AgcStage::kFrameSize, 200.0f, 0.05f, 48000.0);
                std::vector<float> buf (in.size());
                int allocs = -1;
                {
                    aec::test::ScopedAllocCheck check;
                    for (int i = 0; i < 100; ++i)
                    {
                        std::copy (in.begin(), in.end(), buf.begin());
                        agc.process (buf.data());
                    }
                    allocs = check.count();
                }
                expectEquals (allocs, 0);
            }

            beginTest ("AGC preset rows are exact");
            {
                using aec::AgcPreset;
                const std::array<float, 4> gentle { 8.0f, 20.0f, 3.0f, -60.0f };
                const std::array<float, 4> normal { 5.0f, 30.0f, 6.0f, -50.0f };
                const std::array<float, 4> strong { 3.0f, 50.0f, 10.0f, -45.0f };
                expect (aec::agcValuesFor (AgcPreset::Gentle) == gentle);
                expect (aec::agcValuesFor (AgcPreset::Normal) == normal);
                expect (aec::agcValuesFor (AgcPreset::Strong) == strong);
                expectEquals (juce::String (aec::kAgcParamIds[0]), juce::String ("agcHeadroom"));
                expectEquals (juce::String (aec::kAgcParamIds[1]), juce::String ("agcMaxGain"));
                expectEquals (juce::String (aec::kAgcParamIds[2]), juce::String ("agcAdaptSpeed"));
                expectEquals (juce::String (aec::kAgcParamIds[3]), juce::String ("agcNoiseCeiling"));
            }

            beginTest ("matchAgcPreset / applyAgcPreset round trip");
            {
                using aec::AgcPreset;
                AEC3EchoCancellerAudioProcessor p;
                auto& apvts = p.getAPVTS();
                expect (aec::matchAgcPreset (apvts) == AgcPreset::Normal);   // defaults are the Normal row
                expect (apvts.getRawParameterValue ("agcEnabled")->load() < 0.5f);

                for (auto pr : { AgcPreset::Strong, AgcPreset::Gentle, AgcPreset::Normal })
                {
                    aec::applyAgcPreset (apvts, pr);
                    expect (aec::matchAgcPreset (apvts) == pr);
                    expect (apvts.getRawParameterValue ("agcEnabled")->load() >= 0.5f);
                }

                aec::applyAgcPreset (apvts, AgcPreset::Strong);
                auto* g = apvts.getParameter ("agcMaxGain");
                g->setValueNotifyingHost (g->convertTo0to1 (49.0f));
                expect (aec::matchAgcPreset (apvts) == AgcPreset::Custom);

                const float before = apvts.getRawParameterValue ("agcMaxGain")->load();
                aec::applyAgcPreset (apvts, AgcPreset::Custom);   // no-op
                expectEquals (apvts.getRawParameterValue ("agcMaxGain")->load(), before);
            }

            beginTest ("readAgcSettings mirrors the parameters");
            {
                AEC3EchoCancellerAudioProcessor p;
                auto& apvts = p.getAPVTS();
                auto set = [&] (const char* id, float v)
                {
                    auto* prm = apvts.getParameter (id);
                    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                };
                set ("agcEnabled", 1.0f);
                set ("agcHeadroom", 7.0f);
                set ("agcMaxGain", 44.0f);
                set ("agcAdaptSpeed", 12.0f);
                set ("agcNoiseCeiling", -65.0f);
                const auto s = aec::readAgcSettings (apvts);
                expect (s.enabled);
                expectEquals (s.headroomDb, 7.0f);
                expectEquals (s.maxGainDb, 44.0f);
                expectEquals (s.adaptSpeedDbPerSec, 12.0f);
                expectEquals (s.noiseCeilingDbfs, -65.0f);
                expect (s == s.sanitised());
            }

            beginTest ("ReductionHold keeps the max and resets on take");
            {
                aec::ReductionHold h;
                h.push (1.0f); h.push (4.0f); h.push (2.0f);
                expectEquals (h.takeDb(), 4.0f);
                expectEquals (h.takeDb(), 0.0f);
            }
        }
    };

    static LevelTests levelTests;
}
