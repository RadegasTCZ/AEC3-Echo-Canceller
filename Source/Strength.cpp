#include "Strength.h"
#include <cmath>

namespace aec
{
    const std::array<const char*, kNumTuningParams> kTuningParamIds =
    {
        "lfSuppression", "lfTransparency", "hfSuppression", "hfTransparency",
        "lfAttack", "lfDecay", "hfDucking", "hfEchoThreshold",
        "dtLfSuppression", "dtLfTransparency", "dtHfSuppression", "dtHfTransparency",
        "dtLfAttack", "dtLfDecay"
    };

    const TuningValues& tuningFor (Strength s)
    {
        static const TuningValues gentle { 50, 60, 50, 60, 25, 15, 0, 30, 15, 20, 25, 60, 25, 15 };
        static const TuningValues normal { 75, 80, 80, 85, 35, 25, 0, 55, 25, 30, 40, 80, 35, 25 };
        static const TuningValues strong { 95, 100, 95, 100, 55, 50, 50, 80, 50, 55, 60, 90, 55, 40 };

        jassert (s != Strength::Custom);

        switch (s)
        {
            case Strength::Gentle: return gentle;
            case Strength::Strong: return strong;
            case Strength::Normal:
            case Strength::Custom:
            default:               return normal;
        }
    }

    Strength matchStrength (const juce::AudioProcessorValueTreeState& apvts)
    {
        TuningValues current {};

        for (int i = 0; i < kNumTuningParams; ++i)
        {
            auto* v = apvts.getRawParameterValue (kTuningParamIds[(size_t) i]);

            if (v == nullptr)
                return Strength::Custom;

            current[(size_t) i] = v->load();
        }

        // APVTS normalises values to 0..1 and back, so a default can come back as 30.000002: compare with a
        // tolerance far below the 5-point grid step
        auto matches = [&] (const TuningValues& v)
        {
            for (size_t i = 0; i < current.size(); ++i)
                if (std::abs (current[i] - v[i]) > 0.01f)
                    return false;
            return true;
        };

        for (auto s : { Strength::Gentle, Strength::Normal, Strength::Strong })
            if (matches (tuningFor (s)))
                return s;

        return Strength::Custom;
    }

    void applyStrength (juce::AudioProcessorValueTreeState& apvts, Strength s)
    {
        if (s == Strength::Custom)
            return;

        const auto& values = tuningFor (s);

        for (int i = 0; i < kNumTuningParams; ++i)
        {
            if (auto* p = apvts.getParameter (kTuningParamIds[(size_t) i]))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (values[(size_t) i]));
                p->endChangeGesture();
            }
        }
    }
}
