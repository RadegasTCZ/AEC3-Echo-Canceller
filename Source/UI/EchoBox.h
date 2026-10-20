#pragma once
#include "../PluginProcessor.h"
#include "MeterView.h"
#include "StrengthSelector.h"

namespace aec::ui
{
/** Link state shown by the connection dot: green Connected, dim Connecting or Disconnected (capture stopped), warn for Stalled / Error / AEC3 Error. */
enum class LinkState { Connected, Connecting, Disconnected, Problem };

/** Box "1 - ECHO" (424 x 96): strength pills, Delay and Room rotaries, IN / REF meters,
    "Hear reference" toggle, connection dot with the captured sample rate. */
class EchoBox final : public juce::Component,
                      public juce::TooltipClient
{
public:
    static constexpr int kWidth = 424, kHeight = 96;

    explicit EchoBox (AEC3EchoCancellerAudioProcessor&);

    void tick (float dtSec)    { meters.tick (dtSec); }
    /** Repaints the dot area only when the state or the rate text changed. */
    void setConnection (LinkState, int sampleRate);

    StrengthSelector& getStrengthSelector() noexcept { return strength; }
    MeterView& getMeters() noexcept                  { return meters; }
    juce::Slider& getDelayKnob() noexcept            { return delayKnob; }
    juce::Slider& getRoomKnob() noexcept             { return roomKnob; }
    juce::ToggleButton& getHearReference() noexcept  { return hearReference; }
    LinkState getLinkState() const noexcept          { return link; }
    juce::String getRateText() const                 { return rateText; }

    /** Tooltip of the connection dot / rate text (the state word, e.g. "Stalled"). */
    void setConnectionTooltip (const juce::String& text) { connectionTip = text; }
    juce::String getConnectionTooltip() const            { return connectionTip; }
    /** The tooltip while the mouse is over the dot area, otherwise empty (the knobs and the toggle have their own). */
    juce::String getTooltip() override;

    void paint (juce::Graphics&) override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    static juce::Rectangle<int> connectionArea() { return { 335, 65, 80, 16 }; }
    static void setUpCaption (juce::Label&, const juce::String&);

    StrengthSelector strength;
    juce::Slider delayKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider roomKnob  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label delayCaption, roomCaption;
    MeterView meters;
    juce::ToggleButton hearReference { "Hear reference" };
    std::unique_ptr<Apvts::SliderAttachment> delayAttachment, roomAttachment;
    std::unique_ptr<Apvts::ButtonAttachment> hearAttachment;

    LinkState link = LinkState::Connecting;
    juce::String rateText { "--" };
    juce::String connectionTip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoBox)
};
} // namespace aec::ui
