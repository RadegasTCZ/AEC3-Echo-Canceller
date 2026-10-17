#pragma once
#include "../PluginProcessor.h"
#include "EchoBox.h"
#include <functional>

namespace aec::ui
{
/** Advanced > Other (content of the 424 x 184 box, transparent over the page's panel): Bounded echo level / Clock drift / Hear reference toggles,
    Save / Load preset and Factory reset buttons, the 16-band reference spectrum and the status line. */
class OtherTab final : public juce::Component
{
public:
    explicit OtherTab (AEC3EchoCancellerAudioProcessor&);

    /** Called after a factory reset changed the loopback device (the header re-reads its list). */
    std::function<void()> onDeviceReset;

    /** Restores every control to its default (Normal strength, delay 0, room 13, noise 12, switches off,
        On, "System Default" reference) and asks the engine to rebuild. */
    void factoryReset();
    /** Editor timer: repaints the spectrum bars. */
    void tick();
    /** The state word (Connected / Connecting / Stalled / Error / AEC3 Error ...), its colour class and the
        captured sample rate (0 = none). */
    void setStatus (const juce::String& statusText, LinkState, int sampleRate);
    /** The reported plugin latency shown in the status line (30 ms from 16 kHz up). */
    void setLatencyMs (int ms);

    juce::String getStatusText() const noexcept     { return statusText; }
    juce::Button& getFactoryResetButton() noexcept  { return factoryResetButton; }
    juce::Button& getSaveButton() noexcept          { return saveButton; }
    juce::Button& getLoadButton() noexcept          { return loadButton; }
    juce::ToggleButton& getBoundedErl() noexcept    { return boundedErl; }
    juce::ToggleButton& getClockDrift() noexcept    { return clockDrift; }
    juce::ToggleButton& getHearReference() noexcept { return hearReference; }

    void paint (juce::Graphics&) override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    static juce::Rectangle<int> spectrumArea() { return { 223, 29, 192, 110 }; }
    static juce::Rectangle<int> statusArea()   { return { 11, 156, 404, 16 }; }
    void saveClicked();
    void loadClicked();

    AEC3EchoCancellerAudioProcessor& processor;
    juce::ToggleButton boundedErl { "Bounded echo level" }, clockDrift { "Clock drift" }, hearReference { "Hear reference" };
    juce::TextButton saveButton, loadButton, factoryResetButton { "Factory reset" };
    std::unique_ptr<Apvts::ButtonAttachment> boundedAttachment, driftAttachment, hearAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String statusText { "Connecting" };
    LinkState link = LinkState::Connecting;
    int rate = 0;
    int latencyMs = juce::roundToInt (EchoCancellerEngine::kLatencySeconds * 1000.0);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtherTab)
};
} // namespace aec::ui
