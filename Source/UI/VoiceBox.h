#pragma once
#include "../PluginProcessor.h"
#include "AgcPresetSelector.h"
#include "MeterView.h"
#include "ReductionMeter.h"
#include "SegmentedSelector.h"

namespace aec::ui
{
/** Box "2 - VOICE" (424 x 176): everything after the echo canceller. Left: noise suppression pills
    Off 6 12 18 21 above the auto gain pills Off Gentle Normal Strong. Right: OUT meter, limiter
    reduction meter, "Hear removed" and "Limiter" toggles. */
class VoiceBox final : public juce::Component
{
public:
    static constexpr int kWidth = 424, kHeight = 176;

    explicit VoiceBox (AEC3EchoCancellerAudioProcessor&);

    void tick (float dtSec)
    {
        meters.tick (dtSec);
        reduction.tick (dtSec);
    }

    SegmentedSelector& getNoiseSelector() noexcept  { return noise; }
    AgcPresetSelector& getAgcSelector() noexcept    { return agc; }
    MeterView& getMeters() noexcept                 { return meters; }
    ReductionMeter& getReductionMeter() noexcept    { return reduction; }
    juce::ToggleButton& getHearRemoved() noexcept   { return hearRemoved; }
    juce::ToggleButton& getLimiterToggle() noexcept { return limiter; }

    void paint (juce::Graphics&) override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    SegmentedSelector noise;
    AgcPresetSelector agc;
    juce::Label noiseCaption, agcCaption;
    MeterView meters;
    ReductionMeter reduction;
    juce::ToggleButton hearRemoved { "Hear removed" }, limiter { "Limiter" };
    std::unique_ptr<Apvts::ButtonAttachment> hearAttachment, limiterAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceBox)
};
} // namespace aec::ui
