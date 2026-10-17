#include "EchoBox.h"
#include "Theme.h"
#include "../ParamIds.h"

namespace aec::ui
{
namespace
{
    constexpr juce::uint32 kGreen = 0xff5ec27a;   // mockup "ok" colour
    constexpr int kKnobW = 62, kKnobH = 58, kTextBoxH = 16;

    void setUpKnob (juce::Slider& s)
    {
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW, kTextBoxH);
    }

    double numberFromText (const juce::String& t)
    {
        return t.retainCharacters ("-+0123456789.").getDoubleValue();
    }
}

void EchoBox::setUpCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (uiFont (10.0f));
    l.setJustificationType (juce::Justification::centred);
    l.setBorderSize ({});
    l.setInterceptsMouseClicks (false, false);
}

EchoBox::EchoBox (AEC3EchoCancellerAudioProcessor& p)
    : strength (p.getAPVTS(), false),
      meters (std::vector<MeterView::Row> { { "IN", &p.getEchoCancellerEngine().inPeak },
                                            { "REF", &p.getEchoCancellerEngine().refPeak } })
{
    auto& apvts = p.getAPVTS();
    setSize (kWidth, kHeight);

    addAndMakeVisible (strength);

    setUpKnob (delayKnob);
    delayKnob.setComponentID (aec::ids::latencyComp);
    addAndMakeVisible (delayKnob);
    delayAttachment = std::make_unique<Apvts::SliderAttachment> (apvts, aec::ids::latencyComp, delayKnob);
    // the attachment installs param.getText(); show "<n> ms" / raw blocks instead
    delayKnob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    delayKnob.valueFromTextFunction = numberFromText;
    delayKnob.updateText();

    setUpKnob (roomKnob);
    roomKnob.setComponentID (aec::ids::roomReverb);
    addAndMakeVisible (roomKnob);
    roomAttachment = std::make_unique<Apvts::SliderAttachment> (apvts, aec::ids::roomReverb, roomKnob);
    roomKnob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
    roomKnob.valueFromTextFunction = numberFromText;
    roomKnob.updateText();

    setUpCaption (delayCaption, "Delay");
    setUpCaption (roomCaption, "Room");
    addAndMakeVisible (delayCaption);
    addAndMakeVisible (roomCaption);

    addAndMakeVisible (meters);

    hearReference.setComponentID (aec::ids::monitorCapture);
    addAndMakeVisible (hearReference);
    hearAttachment = std::make_unique<Apvts::ButtonAttachment> (apvts, aec::ids::monitorCapture, hearReference);

    // positions from layout-v3.html; the box has a 1 px border, so child coordinates are +1
    strength.setTopLeftPosition (11, 25);
    delayKnob.setBounds (85, 7, kKnobW, kKnobH);
    roomKnob.setBounds (155, 7, kKnobW, kKnobH);
    delayCaption.setBounds (85, 71, kKnobW, 12);
    roomCaption.setBounds (155, 71, kKnobW, 12);
    meters.setBounds (237, 13, 178, MeterView::kHeight (2));
    hearReference.setBounds (237, 57, 124, 20);   // ends before the connection dot
}

void EchoBox::setConnection (LinkState state, int sampleRate)
{
    juce::String text = "--";
    if (sampleRate > 0)
        text = sampleRate % 1000 == 0 ? juce::String (sampleRate / 1000) + "k"
                                      : juce::String ((double) sampleRate / 1000.0, 1) + "k";

    if (state != link || text != rateText)
    {
        link = state;
        rateText = text;
        repaint (connectionArea());
    }
}

juce::String EchoBox::getTooltip()
{
    return connectionArea().contains (getMouseXYRelative()) ? connectionTip : juce::String();
}

void EchoBox::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
    g.fillRect (227, 9, 1, 70);                       // separator

    g.setColour (colours::dim);
    g.setFont (spacedBoldFont (10.0f, 0.08f));
    g.drawText (juce::String::fromUTF8 ("1 \xc2\xb7 ECHO"), 11, 7, 120, 14, juce::Justification::centredLeft, false);

    // connection dot + captured sample rate, right aligned
    const auto area = connectionArea();
    g.setColour (colours::text);
    g.setFont (monoFont (11.0f));
    g.drawText (rateText, area, juce::Justification::centredRight, false);
    juce::GlyphArrangement ga;
    ga.addLineOfText (monoFont (11.0f), rateText, 0.0f, 0.0f);
    const float textW = ga.getBoundingBox (0, -1, true).getWidth();
    const juce::Colour dot = link == LinkState::Connected ? juce::Colour (kGreen)
                           : link == LinkState::Problem ? colours::warn : colours::dim;
    g.setColour (dot);
    g.fillEllipse ((float) area.getRight() - textW - 14.0f, (float) area.getCentreY() - 4.0f, 8.0f, 8.0f);
}
} // namespace aec::ui
