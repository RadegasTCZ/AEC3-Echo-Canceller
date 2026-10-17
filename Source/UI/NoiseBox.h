#pragma once
#include "../PluginProcessor.h"
#include "MeterView.h"
#include "SegmentedSelector.h"

namespace aec::ui
{
/** Box "2 - NOISE" (424 x 88): level pills Off 6 12 18 21, OUT meter, "Hear removed" toggle. */
class NoiseBox final : public juce::Component
{
public:
    static constexpr int kWidth = 424, kHeight = 88;

    explicit NoiseBox (AEC3EchoCancellerAudioProcessor&);

    void tick (float dtSec)    { meters.tick (dtSec); }

    SegmentedSelector& getLevelSelector() noexcept   { return level; }
    MeterView& getMeters() noexcept                  { return meters; }
    juce::ToggleButton& getHearRemoved() noexcept    { return hearRemoved; }

    void paint (juce::Graphics&) override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    SegmentedSelector level;
    juce::Label levelCaption;
    MeterView meters;
    juce::ToggleButton hearRemoved { "Hear removed" };
    std::unique_ptr<Apvts::ButtonAttachment> hearAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseBox)
};
} // namespace aec::ui
