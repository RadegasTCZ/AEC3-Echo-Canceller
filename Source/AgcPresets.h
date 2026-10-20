#pragma once

#include <JuceHeader.h>
#include <array>
#include "AgcStage.h"

namespace aec
{
    enum class AgcPreset { Gentle = 0, Normal = 1, Strong = 2, Custom = 3 };

    inline constexpr int kNumAgcParams = 4;

    // Order: agcHeadroom, agcMaxGain, agcAdaptSpeed, agcNoiseCeiling
    extern const std::array<const char*, kNumAgcParams> kAgcParamIds;

    /** Parameter values (dB, dB, dB/s, dBFS) for a preset. p must not be Custom. */
    const std::array<float, kNumAgcParams>& agcValuesFor (AgcPreset p);

    /** The preset whose four values all match the current parameters (within 0.01), else Custom. */
    AgcPreset matchAgcPreset (const juce::AudioProcessorValueTreeState& apvts);

    /** Writes the four AGC values for p and switches agcEnabled on (one change gesture each). No-op for Custom. */
    void applyAgcPreset (juce::AudioProcessorValueTreeState& apvts, AgcPreset p);

    /** The current parameter values as sanitised AgcSettings. */
    AgcSettings readAgcSettings (const juce::AudioProcessorValueTreeState& apvts);
}
