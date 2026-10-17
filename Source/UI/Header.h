#pragma once
#include "../PluginProcessor.h"
#include <functional>

namespace aec::ui
{
/** Top bar (440 x 44): title, reference-device picker with a speaker glyph, Advanced toggle, On pill.
    While Advanced is open the picker and the Advanced button give way to the tab pills Echo / Double-talk / Other
    and a close (X) button. */
class Header final : public juce::Component
{
public:
    static constexpr int kHeight = 44;
    static constexpr const char* kReferenceTooltip = "Reference: what the plugin listens to as the echo source";

    explicit Header (AEC3EchoCancellerAudioProcessor&);

    /** Called with the new state when the user presses Advanced. */
    std::function<void (bool open)> onAdvancedToggled;

    /** Called with 0 (Echo), 1 (Double-talk) or 2 (Other) when the user presses a tab pill. */
    std::function<void (int tab)> onTabSelected;

    /** Silently moves the Advanced button to `open` and swaps the picker / Advanced button for the tabs / close button. */
    void setAdvancedShown (bool open);
    /** Silently lights tab pill `tab`. */
    void setTab (int tab);
    /** Re-enumerates the devices (also from the editor timer when the device list changed). */
    void refreshDeviceList();

    juce::ComboBox& getDeviceCombo() noexcept         { return deviceCombo; }
    juce::Button& getAdvancedButton() noexcept        { return advancedButton; }
    juce::ToggleButton& getOnButton() noexcept        { return onButton; }
    juce::Button& getTabButton (int tab) noexcept     { return tabButtons[tab]; }
    juce::Button& getCloseButton() noexcept           { return closeButton; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /** Combo with the speaker glyph at x + 8 and the text inset 28 px. */
    class DeviceCombo final : public juce::ComboBox
    {
    public:
        void paint (juce::Graphics&) override;
        void resized() override;
    };

    /** "Advanced": outlined button, accent outline and text while the page is open. */
    class AdvancedButton final : public juce::TextButton
    {
    public:
        AdvancedButton() : juce::TextButton ("Advanced") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    /** Tab pill: a segment pill, lit while its tab is shown. */
    class TabPill final : public juce::Button
    {
    public:
        TabPill() : juce::Button ({}) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    /** Close button: outlined in accent with a drawn X. */
    class CloseButton final : public juce::Button
    {
    public:
        CloseButton() : juce::Button ("Close") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    /** The On pill: accent fill with "ON" while enabled, knob fill with "OFF" otherwise. */
    class OnPill final : public juce::ToggleButton
    {
    public:
        OnPill() : juce::ToggleButton ("On") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    AEC3EchoCancellerAudioProcessor& processor;
    DeviceCombo deviceCombo;
    AdvancedButton advancedButton;
    TabPill tabButtons[3];
    CloseButton closeButton;
    OnPill onButton;
    std::unique_ptr<ButtonAttachment> onAttachment;
    std::vector<WasapiLoopbackCapture::DeviceInfo> cachedDevices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Header)
};
} // namespace aec::ui
