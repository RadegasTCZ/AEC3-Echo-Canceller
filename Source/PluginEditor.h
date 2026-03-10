#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class AEC3EchoCancellerAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    AEC3EchoCancellerAudioProcessorEditor (AEC3EchoCancellerAudioProcessor&);
    ~AEC3EchoCancellerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AEC3EchoCancellerAudioProcessor& audioProcessor;

    juce::ToggleButton enableButton { "AEC Enabled" };
    juce::ToggleButton aggressiveModeButton { "Aggressive Mode (Speakers)" };
    juce::ToggleButton monitorCaptureButton { "Monitor Capture" };
    juce::Label statusLabel;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ButtonAttachment> enableAttachment;
    std::unique_ptr<ButtonAttachment> aggressiveModeAttachment;
    std::unique_ptr<ButtonAttachment> monitorCaptureAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AEC3EchoCancellerAudioProcessorEditor)
};
