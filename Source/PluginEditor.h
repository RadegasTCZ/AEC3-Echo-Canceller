#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/Header.h"
#include "UI/EchoBox.h"
#include "UI/VoiceBox.h"
#include "UI/AdvancedPage.h"

/** Fixed 440 x 332 window (no resizing; setSize is called once, in the constructor). A 30 Hz timer ticks
    the meters, the strength pills, the connection dot and the device list, repainting only what changed.
    Shows the header plus the Echo and Voice boxes, or (Advanced open) the Advanced page. */
class AEC3EchoCancellerAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    static constexpr int kWidth = 440, kHeight = 332;

    explicit AEC3EchoCancellerAudioProcessorEditor (AEC3EchoCancellerAudioProcessor&);
    ~AEC3EchoCancellerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    aec::ui::Header& getHeader() noexcept        { return header; }
    aec::ui::EchoBox& getEchoBox() noexcept      { return echoBox; }
    aec::ui::VoiceBox& getVoiceBox() noexcept    { return voiceBox; }
    aec::ui::AdvancedPage& getAdvancedPage() noexcept { return advancedPage; }
    bool isAdvancedShown() const noexcept        { return advancedShown; }
    /** Re-reads the loopback / engine state into the dot, its tooltip and the Other-tab status (timer, 30 Hz). */
    void updateConnection();

private:
    void timerCallback() override;
    void showAdvanced (bool open);

    AEC3EchoCancellerAudioProcessor& audioProcessor;
    aec::ui::Theme theme;                         // declared before every child: destroyed after them

    aec::ui::Header header;
    aec::ui::EchoBox echoBox;
    aec::ui::VoiceBox voiceBox;
    aec::ui::AdvancedPage advancedPage;
    juce::TooltipWindow tooltipWindow { this, 700 };

    bool advancedShown = false;
    double lastTickSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AEC3EchoCancellerAudioProcessorEditor)
};
