#include "OtherTab.h"
#include "Theme.h"
#include "../ParamIds.h"
#include "../PresetIO.h"
#include <cmath>

namespace aec::ui
{
namespace
{
    constexpr juce::uint32 kGreen = 0xff5ec27a;   // mockup "ok" colour
    constexpr float kSpectrumMinDb = -100.0f, kSpectrumMaxDb = -10.0f;

    juce::String withEllipsis (const char* word)   { return juce::String (word) + juce::String::fromUTF8 ("\xe2\x80\xa6"); }
}

OtherTab::OtherTab (AEC3EchoCancellerAudioProcessor& p)
    : processor (p)
{
    auto& apvts = p.getAPVTS();
    setSize (424, 184);

    boundedErl.setComponentID (aec::ids::boundedErl);
    clockDrift.setComponentID (aec::ids::clockDrift);
    hearReference.setComponentID (aec::ids::monitorCapture);
    for (auto* t : { &boundedErl, &clockDrift, &hearReference })
        addAndMakeVisible (*t);
    boundedAttachment = std::make_unique<Apvts::ButtonAttachment> (apvts, aec::ids::boundedErl, boundedErl);
    driftAttachment   = std::make_unique<Apvts::ButtonAttachment> (apvts, aec::ids::clockDrift, clockDrift);
    hearAttachment    = std::make_unique<Apvts::ButtonAttachment> (apvts, aec::ids::monitorCapture, hearReference);

    saveButton.setButtonText (withEllipsis ("Save preset"));
    loadButton.setButtonText (withEllipsis ("Load preset"));
    factoryResetButton.setColour (juce::TextButton::textColourOffId, colours::warn);
    factoryResetButton.setColour (juce::TextButton::textColourOnId, colours::warn);
    for (auto* b : { &saveButton, &loadButton, &factoryResetButton })
        addAndMakeVisible (*b);

    saveButton.onClick = [this] { saveClicked(); };
    loadButton.onClick = [this] { loadClicked(); };
    factoryResetButton.onClick = [this] { factoryReset(); };

    // positions from layout-v2.html; the box has a 1 px border, so child coordinates are +1
    boundedErl.setBounds (11, 13, 190, 20);
    clockDrift.setBounds (11, 37, 190, 20);
    hearReference.setBounds (11, 61, 190, 20);
    saveButton.setBounds (11, 91, 90, 22);
    loadButton.setBounds (107, 91, 90, 22);
    factoryResetButton.setBounds (11, 119, 186, 22);
}

void OtherTab::saveClicked()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Save preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.ini");

    const auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;                                           // cancelled
        if (file.getFileExtension().isEmpty())
            file = file.withFileExtension ("ini");

        juce::String error;
        if (! PresetIO::savePreset (file, processor.getAPVTS(), error))
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Could not save preset", error);
    });
}

void OtherTab::loadClicked()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.ini");

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;                                           // cancelled

        juce::String error;
        if (! PresetIO::loadPreset (file, processor.getAPVTS(), error))
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Could not load preset", error);
    });
}

void OtherTab::factoryReset()
{
    // Every parameter back to its layout default: Normal strength, Delay 0, Room 13, Noise 12 dB,
    // monitors off, On, auto gain off with the Normal values, limiter on (one change gesture each)
    for (auto* prm : processor.getParameters())
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }

    processor.getLoopbackCapture().setDeviceId ("");
    if (onDeviceReset)
        onDeviceReset();

    processor.getEchoCancellerEngine().requestReset();        // also clears a halted AEC3
}

void OtherTab::tick()
{
    repaint (spectrumArea());
}

void OtherTab::setStatus (const juce::String& text, LinkState state, int sampleRate)
{
    if (text != statusText || state != link || sampleRate != rate)
    {
        statusText = text;
        link = state;
        rate = sampleRate;
        repaint (statusArea());
    }
}

void OtherTab::setLatencyMs (int ms)
{
    if (ms != latencyMs)
    {
        latencyMs = ms;
        repaint (statusArea());
    }
}

void OtherTab::paint (juce::Graphics& g)
{
    // the AdvancedPage paints the box; this tab covers it transparently
    g.setColour (colours::line);
    g.fillRect (213, 11, 1, 130);                             // separator

    g.setColour (colours::dim);
    g.setFont (spacedBoldFont (10.0f, 0.08f));
    g.drawText ("REFERENCE SPECTRUM", 223, 11, 192, 14, juce::Justification::centredLeft, false);

    const auto area = spectrumArea();
    g.setColour (colours::well);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (colours::level);
    auto& engine = processor.getEchoCancellerEngine();
    for (int i = 0; i < EchoCancellerEngine::kSpectrumBands; ++i)
    {
        const float mag = engine.displaySpectrumBands[i].load (std::memory_order_relaxed);   // linear magnitude
        const float db = 20.0f * std::log10 (juce::jmax (mag, 1.0e-7f));
        const float n = juce::jlimit (0.0f, 1.0f, (db - kSpectrumMinDb) / (kSpectrumMaxDb - kSpectrumMinDb));
        const float h = n * (float) (area.getHeight() - 8);
        g.fillRoundedRectangle ((float) area.getX() + 6.0f + (float) i * 11.3f, (float) area.getBottom() - 4.0f - h, 8.0f, h, 1.0f);
    }

    // Status <state> <rate> Hz . latency <ms> ms . v6.0.0
    const auto sep = juce::String::fromUTF8 (" \xc2\xb7 ");
    const juce::Colour stateColour = link == LinkState::Connected ? juce::Colour (kGreen)
                                   : link == LinkState::Problem ? colours::warn : colours::dim;

    juce::AttributedString s;
    const auto font = monoFont (10.0f);
    s.append ("Status ", font, colours::dim);
    s.append (statusText, font, stateColour);
    if (rate > 0)
        s.append (" " + juce::String (rate) + " Hz", font, colours::text);
    s.append (sep + "latency ", font, colours::dim);
    s.append (juce::String (latencyMs) + " ms", font, colours::text);
    s.append (sep + "v" + juce::String (ProjectInfo::versionString), font, colours::dim);
    s.setWordWrap (juce::AttributedString::none);
    s.draw (g, statusArea().toFloat());
}
} // namespace aec::ui
