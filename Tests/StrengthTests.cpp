#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include "../Source/PluginProcessor.h"
#include "../Source/AecMapping.h"
#include "../Source/Strength.h"
#include "api/audio/echo_canceller3_config.h"

namespace
{
    class StrengthTests final : public juce::UnitTest
    {
    public:
        StrengthTests() : juce::UnitTest ("Strength", "Strength") {}

        void runTest() override
        {
            beginTest ("Normal is the nearest 5% grid point to the upstream default");
            {
                const webrtc::EchoCanceller3Config cfg;
                const auto& n  = cfg.suppressor.normal_tuning;
                const auto& d  = cfg.suppressor.nearend_tuning;
                const auto& hb = cfg.suppressor.high_bands_suppression;
                using namespace aec::mapping;

                struct Row { const char* id; float (*fn) (float); float upstream; };
                const Row rows[aec::kNumTuningParams] =
                {
                    { "lfSuppression",    mapSuppression,       n.mask_lf.enr_suppress },
                    { "lfTransparency",   mapTransparency,      n.mask_lf.enr_transparent },
                    { "hfSuppression",    mapHfSuppression,     n.mask_hf.enr_suppress },
                    { "hfTransparency",   mapHfTransparency,    n.mask_hf.enr_transparent },
                    { "lfAttack",         mapAttack,            n.max_inc_factor },
                    { "lfDecay",          mapDecay,             n.max_dec_factor_lf },
                    { "hfDucking",        mapHfDucking,         hb.max_gain_during_echo },
                    { "hfEchoThreshold",  mapHfEchoThreshold,   hb.enr_threshold },
                    { "dtLfSuppression",  mapSuppression,       d.mask_lf.enr_suppress },
                    { "dtLfTransparency", mapTransparency,      d.mask_lf.enr_transparent },
                    { "dtHfSuppression",  mapHfSuppression,     d.mask_hf.enr_suppress },
                    { "dtHfTransparency", mapHfTransparency,    d.mask_hf.enr_transparent },
                    { "dtLfAttack",       mapAttack,            d.max_inc_factor },
                    { "dtLfDecay",        mapDecay,             d.max_dec_factor_lf },
                };

                const auto& normal = aec::tuningFor (aec::Strength::Normal);

                for (int i = 0; i < aec::kNumTuningParams; ++i)
                {
                    const auto& r = rows[i];
                    expectEquals (juce::String (aec::kTuningParamIds[(size_t) i]), juce::String (r.id));

                    const float chosen = std::abs (r.fn (normal[(size_t) i]) - r.upstream);
                    float best = chosen;

                    for (int g = 0; g <= 100; g += 5)
                        best = std::min (best, std::abs (r.fn ((float) g) - r.upstream));

                    expect (chosen <= best + 1.0e-6f,
                            juce::String (r.id) + ": Normal " + juce::String (normal[(size_t) i])
                                + " is not the nearest grid point to upstream " + juce::String (r.upstream));
                }
            }

            beginTest ("tables stay on the 5% grid and inside 0..100");
            {
                for (auto s : { aec::Strength::Gentle, aec::Strength::Normal, aec::Strength::Strong })
                    for (float v : aec::tuningFor (s))
                    {
                        expect (v >= 0.0f && v <= 100.0f);
                        expectEquals (std::fmod (v, 5.0f), 0.0f);
                    }
            }

            beginTest ("matchStrength finds each strength and Custom");
            {
                AEC3EchoCancellerAudioProcessor p;
                auto& apvts = p.getAPVTS();

                for (auto s : { aec::Strength::Gentle, aec::Strength::Normal, aec::Strength::Strong })
                {
                    aec::applyStrength (apvts, s);
                    expect (aec::matchStrength (apvts) == s);
                }

                auto* decay = apvts.getParameter ("lfDecay");
                decay->setValueNotifyingHost (decay->convertTo0to1 (
                    apvts.getRawParameterValue ("lfDecay")->load() + 5.0f));
                expect (aec::matchStrength (apvts) == aec::Strength::Custom);
            }

            beginTest ("applyStrength never touches room, delay, switches or nsLevel");
            {
                AEC3EchoCancellerAudioProcessor p;
                auto& apvts = p.getAPVTS();

                auto set = [&] (const char* id, float v)
                {
                    if (auto* prm = apvts.getParameter (id))
                        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                };

                auto read = [&] (const char* id)
                {
                    auto* v = apvts.getRawParameterValue (id);
                    return v != nullptr ? v->load() : -1.0f;
                };

                set ("roomReverb", 30.0f);
                set ("latencyComp", 50.0f);
                set ("boundedErl", 1.0f);
                expect (apvts.getParameter ("nsLevel") != nullptr);
                set ("nsLevel", 4.0f);

                const char* ids[] = { "roomReverb", "latencyComp", "boundedErl", "nsLevel" };
                float before[4];

                for (int i = 0; i < 4; ++i)
                    before[i] = read (ids[i]);

                expectEquals (before[0], 30.0f);
                expectEquals (before[1], 50.0f);
                expectEquals (before[2], 1.0f);
                expectEquals (before[3], 4.0f);

                aec::applyStrength (apvts, aec::Strength::Strong);

                for (int i = 0; i < 4; ++i)
                    expectEquals (read (ids[i]), before[i]);
            }
        }
    };

    static StrengthTests strengthTests;
}
