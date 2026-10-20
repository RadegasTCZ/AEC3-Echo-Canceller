#include "VoiceBox.h"
#include "Theme.h"
#include "../ParamIds.h"

namespace aec::ui
{
namespace
{
    void setUpCaption (juce::Label& l, const juce::String& text, juce::Rectangle<int> bounds)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (uiFont (10.0f));
        l.setJustificationType (juce::Justification::centredLeft);
        l.setBorderSize ({});
        l.setInterceptsMouseClicks (false, false);
        l.setBounds (bounds);
    }
}

VoiceBox::VoiceBox (AEC3EchoCancellerAudioProcessor& p)
    : noise (*p.getAPVTS().getParameter (aec::ids::nsLevel), juce::StringArray { "Off", "6", "12", "18", "21" }, 5, 34, 20, 4),
      agc (p.getAPVTS(), true, false),
      meters (std::vector<MeterView::Row> { { "OUT", &p.getEchoCancellerEngine().outPeak } }),
      reduction (p.getEchoCancellerEngine().limiterReduction)
{
    setSize (kWidth, kHeight);

    // left column: one pill row per stage, caption above it
    addAndMakeVisible (noise);
    noise.setTopLeftPosition (11, 44);
    setUpCaption (noiseCaption, "Noise suppression (dB)", { 11, 30, noise.getWidth(), 12 });
    addAndMakeVisible (noiseCaption);

    addAndMakeVisible (agc);
    agc.setTopLeftPosition (11, 106);
    setUpCaption (agcCaption, "Auto gain", { 11, 92, agc.getWidth(), 12 });
    addAndMakeVisible (agcCaption);

    // right column: output meters, then the toggles
    addAndMakeVisible (meters);
    meters.setBounds (237, 30, 178, MeterView::kHeight (1));

    addAndMakeVisible (reduction);
    reduction.setBounds (237, 30 + MeterView::kHeight (1) + 6, 178, ReductionMeter::kHeight);

    hearRemoved.setComponentID (aec::ids::monitorNoise);
    hearRemoved.setBounds (237, 97, 130, 20);
    addAndMakeVisible (hearRemoved);
    hearAttachment = std::make_unique<Apvts::ButtonAttachment> (p.getAPVTS(), aec::ids::monitorNoise, hearRemoved);

    limiter.setComponentID (aec::ids::limiterEnabled);
    limiter.setTooltip ("Output limiter: catches peaks just before they clip. With Auto gain on and the limiter off, "
                        "a sudden loud sound can clip.");
    limiter.setBounds (237, 123, 130, 20);
    addAndMakeVisible (limiter);
    limiterAttachment = std::make_unique<Apvts::ButtonAttachment> (p.getAPVTS(), aec::ids::limiterEnabled, limiter);
}

void VoiceBox::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
    g.fillRect (227, 9, 1, kHeight - 18);

    g.setColour (colours::dim);
    g.setFont (spacedBoldFont (10.0f, 0.08f));
    g.drawText (juce::String::fromUTF8 ("2 \xc2\xb7 VOICE"), 11, 7, 120, 14, juce::Justification::centredLeft, false);
}
} // namespace aec::ui
