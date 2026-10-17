#pragma once

#include <JuceHeader.h>
#include <array>

namespace aec
{
    enum class Strength { Gentle = 0, Normal = 1, Strong = 2, Custom = 3 };

    inline constexpr int kNumTuningParams = 14;

    // Order: lfSuppression, lfTransparency, hfSuppression, hfTransparency, lfAttack, lfDecay,
    //        hfDucking, hfEchoThreshold, dtLfSuppression, dtLfTransparency, dtHfSuppression,
    //        dtHfTransparency, dtLfAttack, dtLfDecay
    extern const std::array<const char*, kNumTuningParams> kTuningParamIds;

    using TuningValues = std::array<float, kNumTuningParams>;

    /** Parameter values (0-100%) for a strength. s must not be Custom. */
    const TuningValues& tuningFor (Strength s);

    /** The strength whose 14 values all match the current parameters (within 0.01), else Custom. */
    Strength matchStrength (const juce::AudioProcessorValueTreeState& apvts);

    /** Writes the 14 tuning parameters for s (one change gesture each). No-op for Custom. */
    void applyStrength (juce::AudioProcessorValueTreeState& apvts, Strength s);
}
