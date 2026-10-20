#include "AgcPresets.h"
#include "ParamIds.h"
#include <cmath>

namespace aec
{
    const std::array<const char*, kNumAgcParams> kAgcParamIds =
    {
        ids::agcHeadroom, ids::agcMaxGain, ids::agcAdaptSpeed, ids::agcNoiseCeiling
    };

    const std::array<float, kNumAgcParams>& agcValuesFor (AgcPreset p)
    {
        static const std::array<float, kNumAgcParams> gentle { 8.0f, 20.0f, 3.0f, -60.0f };
        static const std::array<float, kNumAgcParams> normal { 5.0f, 30.0f, 6.0f, -50.0f };
        static const std::array<float, kNumAgcParams> strong { 3.0f, 50.0f, 10.0f, -45.0f };

        jassert (p != AgcPreset::Custom);

        switch (p)
        {
            case AgcPreset::Gentle: return gentle;
            case AgcPreset::Strong: return strong;
            case AgcPreset::Normal:
            case AgcPreset::Custom:
            default:                return normal;
        }
    }

    AgcPreset matchAgcPreset (const juce::AudioProcessorValueTreeState& apvts)
    {
        std::array<float, kNumAgcParams> current {};

        for (size_t i = 0; i < current.size(); ++i)
        {
            auto* v = apvts.getRawParameterValue (kAgcParamIds[i]);

            if (v == nullptr)
                return AgcPreset::Custom;

            current[i] = v->load();
        }

        // Same tolerance as matchStrength: APVTS normalisation can leave a value off by a few ulps
        auto matches = [&] (const std::array<float, kNumAgcParams>& v)
        {
            for (size_t i = 0; i < current.size(); ++i)
                if (! (std::abs (current[i] - v[i]) <= 0.01f))
                    return false;
            return true;
        };

        for (auto p : { AgcPreset::Gentle, AgcPreset::Normal, AgcPreset::Strong })
            if (matches (agcValuesFor (p)))
                return p;

        return AgcPreset::Custom;
    }

    void applyAgcPreset (juce::AudioProcessorValueTreeState& apvts, AgcPreset p)
    {
        if (p == AgcPreset::Custom)
            return;

        auto set = [&] (const char* id, float real)
        {
            if (auto* prm = apvts.getParameter (id))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (real));
                prm->endChangeGesture();
            }
        };

        const auto& values = agcValuesFor (p);

        for (size_t i = 0; i < values.size(); ++i)
            set (kAgcParamIds[i], values[i]);

        set (ids::agcEnabled, 1.0f);
    }

    AgcSettings readAgcSettings (const juce::AudioProcessorValueTreeState& apvts)
    {
        auto get = [&] (const char* id, float fallback)
        {
            auto* v = apvts.getRawParameterValue (id);
            return v != nullptr ? v->load() : fallback;
        };

        AgcSettings s;
        s.enabled = get (ids::agcEnabled, 0.0f) >= 0.5f;
        s.headroomDb = get (ids::agcHeadroom, s.headroomDb);
        s.maxGainDb = get (ids::agcMaxGain, s.maxGainDb);
        s.adaptSpeedDbPerSec = get (ids::agcAdaptSpeed, s.adaptSpeedDbPerSec);
        s.noiseCeilingDbfs = get (ids::agcNoiseCeiling, s.noiseCeilingDbfs);
        return s.sanitised();
    }
}
