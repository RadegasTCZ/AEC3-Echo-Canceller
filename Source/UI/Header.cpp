#include "Header.h"
#include "Theme.h"
#include "SegmentedSelector.h"
#include "../ParamIds.h"

namespace aec::ui
{
void Header::DeviceCombo::resized()
{
    juce::ComboBox::resized();
    // the combo's label is its only child: leave room for the speaker glyph at the left
    if (auto* textLabel = dynamic_cast<juce::Label*> (getChildComponent (0)))
        textLabel->setBounds (textLabel->getBounds().withTrimmedLeft (27));
}

void Header::DeviceCombo::paint (juce::Graphics& g)
{
    juce::ComboBox::paint (g);

    // speaker glyph from the mockup (16 x 16 viewBox drawn at 14 x 14, x + 8, vertically centred)
    const float s = 14.0f / 16.0f;
    const auto at = juce::AffineTransform::scale (s).translated (8.0f, ((float) getHeight() - 14.0f) * 0.5f);

    juce::Path body;
    body.startNewSubPath (2.0f, 6.0f);
    body.lineTo (5.0f, 6.0f);
    body.lineTo (9.0f, 3.0f);
    body.lineTo (9.0f, 13.0f);
    body.lineTo (5.0f, 10.0f);
    body.lineTo (2.0f, 10.0f);
    body.closeSubPath();
    g.setColour (colours::text);
    g.fillPath (body, at);

    juce::Path waves;
    waves.addCentredArc (8.5f, 8.0f, 3.2f, 3.2f, 0.0f, juce::MathConstants<float>::pi * 0.28f,
                         juce::MathConstants<float>::pi * 0.72f, true);
    waves.addCentredArc (8.5f, 8.0f, 6.2f, 6.2f, 0.0f, juce::MathConstants<float>::pi * 0.28f,
                         juce::MathConstants<float>::pi * 0.72f, true);
    g.setColour (colours::dim);
    g.strokePath (waves, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), at);
}

void Header::AdvancedButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? colours::accent : (highlighted ? colours::dim : colours::line));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (on ? colours::accent : colours::text);
    g.setFont (uiFont (11.0f));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

void Header::TabPill::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat();
    if (auto* lf = dynamic_cast<SegmentedSelector::LookAndFeelMethods*> (&getLookAndFeel()))
        lf->drawSegmentPill (g, bounds, getButtonText(), getToggleState(), highlighted);
    else
    {
        g.setColour (getToggleState() ? juce::Colours::orange : juce::Colours::darkgrey);
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
        g.setColour (juce::Colours::white);
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
}

void Header::CloseButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? colours::line : colours::panel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (highlighted ? colours::text : colours::accent);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    const auto c = r.getCentre();
    g.drawLine (c.x - 4.0f, c.y - 4.0f, c.x + 4.0f, c.y + 4.0f, 1.5f);
    g.drawLine (c.x - 4.0f, c.y + 4.0f, c.x + 4.0f, c.y - 4.0f, 1.5f);
}

void Header::OnPill::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    const float radius = r.getHeight() * 0.5f;
    g.setColour (on ? colours::accent : colours::knob);
    g.fillRoundedRectangle (r, radius);
    g.setColour (on ? colours::accent : (highlighted ? colours::dim : colours::line));
    g.drawRoundedRectangle (r, radius, 1.0f);
    g.setColour (on ? juce::Colour (argb::onAccent) : (highlighted ? colours::text : colours::dim));
    g.setFont (uiFont (11.0f, true));
    g.drawText (on ? "ON" : "OFF", getLocalBounds(), juce::Justification::centred, false);
}

Header::Header (AEC3EchoCancellerAudioProcessor& p)
    : processor (p)
{
    setSize (440, kHeight);

    deviceCombo.setTooltip (kReferenceTooltip);
    deviceCombo.onChange = [this]
    {
        const int id = deviceCombo.getSelectedId();
        if (id == 1)
            processor.getLoopbackCapture().setDeviceId ("");
        else if (id >= 2 && (size_t) (id - 2) < cachedDevices.size())
            processor.getLoopbackCapture().setDeviceId (cachedDevices[(size_t) (id - 2)].id);
    };
    addAndMakeVisible (deviceCombo);
    refreshDeviceList();

    advancedButton.setClickingTogglesState (true);
    advancedButton.onClick = [this]
    {
        if (onAdvancedToggled)
            onAdvancedToggled (advancedButton.getToggleState());
    };
    addAndMakeVisible (advancedButton);

    static const char* const tabNames[] = { "Echo", "Double-talk", "Level", "Other" };
    for (int i = 0; i < 4; ++i)
    {
        auto& tab = tabButtons[i];
        tab.setButtonText (tabNames[i]);
        tab.setClickingTogglesState (true);
        tab.setRadioGroupId (1001);
        tab.onClick = [this, i]
        {
            // the radio group also clicks the pill it turns off: only the lit one selects
            if (tabButtons[i].getToggleState() && onTabSelected)
                onTabSelected (i);
        };
        addChildComponent (tab);
    }
    tabButtons[0].setToggleState (true, juce::dontSendNotification);

    closeButton.onClick = [this]
    {
        if (onAdvancedToggled)
            onAdvancedToggled (false);
    };
    addChildComponent (closeButton);

    onButton.setComponentID (aec::ids::aecEnabled);
    addAndMakeVisible (onButton);
    onAttachment = std::make_unique<ButtonAttachment> (p.getAPVTS(), aec::ids::aecEnabled, onButton);
}

void Header::setAdvancedShown (bool open)
{
    advancedButton.setToggleState (open, juce::dontSendNotification);
    advancedShown = open;
    deviceCombo.setVisible (! open);
    advancedButton.setVisible (! open);
    for (auto& tab : tabButtons)
        tab.setVisible (open);
    closeButton.setVisible (open);
    repaint();                                              // the title is narrower while the tabs are shown
}

void Header::setTab (int tab)
{
    for (int i = 0; i < 4; ++i)
        tabButtons[i].setToggleState (i == tab, juce::dontSendNotification);
}

void Header::refreshDeviceList()
{
    cachedDevices = WasapiLoopbackCapture::enumerateDevices();
    const auto currentId = processor.getLoopbackCapture().getDeviceId();

    deviceCombo.clear (juce::dontSendNotification);
    deviceCombo.addItem ("System Default", 1);

    int selectedId = 1;
    for (int i = 0; i < (int) cachedDevices.size(); ++i)
    {
        const int itemId = i + 2;
        deviceCombo.addItem (cachedDevices[(size_t) i].name, itemId);
        if (cachedDevices[(size_t) i].id == currentId)
            selectedId = itemId;
    }
    deviceCombo.setSelectedId (selectedId, juce::dontSendNotification);
}

void Header::resized()
{
    // positions from layout-v3.html
    deviceCombo.setBounds (148, 10, 120, 24);
    advancedButton.setBounds (274, 10, 74, 24);
    onButton.setBounds (356, 8, 76, 28);
    // tab pills and close button from layout-v2.html (shown in place of the picker and the Advanced button)
    // four tabs plus the close button must fit between the (narrower) title and the On pill
    tabButtons[0].setBounds (104, 12, 42, 20);
    tabButtons[1].setBounds (149, 12, 70, 20);
    tabButtons[2].setBounds (222, 12, 42, 20);
    tabButtons[3].setBounds (267, 12, 44, 20);
    closeButton.setBounds (318, 10, 28, 24);
}

void Header::paint (juce::Graphics& g)
{
    g.setColour (colours::text);
    g.setFont (spacedBoldFont (11.0f, 0.08f));
    g.drawFittedText ("ECHO CANCELLER", 8, 8, advancedShown ? 92 : 140, 28, juce::Justification::centredLeft, 1, 0.8f);
}
} // namespace aec::ui
