#include "NoiseBox.h"
#include "Theme.h"
#include "../ParamIds.h"

namespace aec::ui
{
NoiseBox::NoiseBox (AEC3EchoCancellerAudioProcessor& p)
    : level (*p.getAPVTS().getParameter (aec::ids::nsLevel), juce::StringArray { "Off", "6", "12", "18", "21" }, 5, 34, 20, 4),
      meters (std::vector<MeterView::Row> { { "OUT", &p.getEchoCancellerEngine().outPeak } })
{
    setSize (kWidth, kHeight);

    addAndMakeVisible (level);
    level.setTopLeftPosition (11, 29);

    levelCaption.setText ("Level (dB)", juce::dontSendNotification);
    levelCaption.setFont (uiFont (10.0f));
    levelCaption.setJustificationType (juce::Justification::centred);
    levelCaption.setBorderSize ({});
    levelCaption.setInterceptsMouseClicks (false, false);
    levelCaption.setBounds (11, 57, 186, 12);
    addAndMakeVisible (levelCaption);

    addAndMakeVisible (meters);
    meters.setBounds (237, 23, 178, MeterView::kHeight (1));

    hearRemoved.setComponentID (aec::ids::monitorNoise);
    hearRemoved.setBounds (237, 51, 130, 20);
    addAndMakeVisible (hearRemoved);
    hearAttachment = std::make_unique<Apvts::ButtonAttachment> (p.getAPVTS(), aec::ids::monitorNoise, hearRemoved);
}

void NoiseBox::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
    g.fillRect (227, 9, 1, 70);

    g.setColour (colours::dim);
    g.setFont (spacedBoldFont (10.0f, 0.08f));
    g.drawText (juce::String::fromUTF8 ("2 \xc2\xb7 NOISE"), 11, 7, 120, 14, juce::Justification::centredLeft, false);
}
} // namespace aec::ui
