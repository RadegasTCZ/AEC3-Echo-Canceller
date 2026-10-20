#include "AdvancedPage.h"
#include "HelpText.h"
#include "Theme.h"

namespace aec::ui
{
namespace
{
    // Echo tab: LF column then HF column (indices into kTuningParamIds); Double-talk tab the same for dt*.
    constexpr int kEchoLeft[]  = { 0, 1, 4, 5 },  kEchoRight[] = { 2, 3, 6, 7 };
    constexpr int kDtLeft[]    = { 8, 9, 12 },    kDtRight[]   = { 10, 11, 13 };

    const char* const kLabels[aec::kNumTuningParams] = {
        "LF suppression", "LF transparency", "HF suppression", "HF transparency",
        "LF attack", "LF decay", "HF ducking", "HF echo thresh.",
        "LF suppression", "LF transparency", "HF suppression", "HF transparency",
        "LF attack", "LF decay" };

    constexpr int kAgcLeft[] = { 0, 1 }, kAgcRight[] = { 2, 3 };
    const char* const kAgcLabels[aec::kNumAgcParams] = { "Headroom", "Max gain", "Adapt speed", "Noise ceiling" };

    constexpr int kLeftX = 11, kRightX = 221, kColumnY = 35;
}

AdvancedPage::AdvancedPage (AEC3EchoCancellerAudioProcessor& p)
    : processor (p),
      strength (p.getAPVTS(), true, true),
      agc (p.getAPVTS(), false, true),
      otherTab (p)
{
    auto& apvts = p.getAPVTS();
    setSize (kWidth, kHeight);

    const auto& normal = aec::tuningFor (aec::Strength::Normal);
    for (int i = 0; i < aec::kNumTuningParams; ++i)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (aec::kTuningParamIds[(size_t) i]));
        jassert (param != nullptr);
        sliders[(size_t) i] = std::make_unique<CompactSlider> (*param, kLabels[i], normal[(size_t) i]);
        sliders[(size_t) i]->onHoverOrClick = [this] (const juce::String& id) { showHelp (id); };
        addChildComponent (*sliders[(size_t) i]);
    }

    const auto& agcNormal = aec::agcValuesFor (aec::AgcPreset::Normal);
    for (int i = 0; i < aec::kNumAgcParams; ++i)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (aec::kAgcParamIds[(size_t) i]));
        jassert (param != nullptr);
        agcSliders[(size_t) i] = std::make_unique<CompactSlider> (*param, kAgcLabels[i], agcNormal[(size_t) i]);
        agcSliders[(size_t) i]->onHoverOrClick = [this] (const juce::String& id) { showHelp (id); };
        addChildComponent (*agcSliders[(size_t) i]);
    }

    // positions from layout-v2.html; the box has a 1 px border, so child coordinates are +1
    strength.setTopLeftPosition (81, 6);
    addChildComponent (strength);
    agc.setTopLeftPosition (81, 6);
    addChildComponent (agc);

    auto place = [this] (const int* ids, int n, int x)
    {
        for (int r = 0; r < n; ++r)
            sliders[(size_t) ids[r]]->setBounds (x, kColumnY + r * CompactSlider::kHeight,
                                                 x == kLeftX ? 196 : 194, CompactSlider::kHeight);
    };
    auto placeAgc = [this] (const int* ids, int x)
    {
        for (int r = 0; r < 2; ++r)
            agcSliders[(size_t) ids[r]]->setBounds (x, kColumnY + r * CompactSlider::kHeight,
                                                    x == kLeftX ? 196 : 194, CompactSlider::kHeight);
    };
    placeAgc (kAgcLeft, kLeftX);    placeAgc (kAgcRight, kRightX);
    place (kEchoLeft, 4, kLeftX);   place (kEchoRight, 4, kRightX);
    place (kDtLeft, 3, kLeftX);     place (kDtRight, 3, kRightX);

    addChildComponent (otherTab);
    setTab (Echo);
}

void AdvancedPage::setTab (int newTab)
{
    tab = juce::jlimit (0, 3, newTab);
    helpTitle = helpBody = juce::String();                  // the strip explains the last slider of this tab
    strength.setVisible (tab == Echo);
    for (int i = 0; i < aec::kNumTuningParams; ++i)
        sliders[(size_t) i]->setVisible (i < 8 ? tab == Echo : tab == DoubleTalk);
    agc.setVisible (tab == Level);
    for (auto& s : agcSliders)
        s->setVisible (tab == Level);
    otherTab.setVisible (tab == Other);
    repaint();
}

void AdvancedPage::tick()
{
    strength.refresh();
    if (tab == Level)
        agc.refresh();
    if (tab == Other)
        otherTab.tick();
}

juce::String AdvancedPage::getIdleHelpText() const
{
    juce::String text = "tick = Normal (WebRTC default)";
    if (tab == Level)
        text = "With Auto gain on and the limiter off, loud peaks can clip.\n" + text;
    return text;
}

void AdvancedPage::showHelp (const juce::String& paramId)
{
    if (const auto* entry = aec::help::find (paramId))
    {
        if (entry->title != helpTitle)
        {
            helpTitle = entry->title;
            helpBody = entry->whatItDoes;
            repaint (helpArea().expanded (0, 6));
        }
    }
}

void AdvancedPage::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);

    if (tab == Other)
        return;

    g.setColour (colours::dim);
    g.setFont (spacedBoldFont (10.0f, 0.08f));
    g.drawText (tab == Echo ? "STRENGTH" : tab == Level ? "AUTO GAIN" : "WHEN BOTH SIDES TALK AT ONCE", 11, 9, 240, 14,
                juce::Justification::centredLeft, false);

    g.setColour (colours::line);                                       // column separator and help-strip rule
    g.fillRect (213, kColumnY + 2, 1, tab == Echo ? 76 : tab == Level ? 36 : 56);
    g.fillRect (11, helpArea().getY() - 10, 402, 1);

    if (helpTitle.isEmpty())
    {
        g.setColour (colours::dim);
        g.setFont (monoFont (10.0f));
        const auto lines = juce::StringArray::fromLines (getIdleHelpText());
        for (int i = 0; i < lines.size(); ++i)
            g.drawText (lines[i], 11, kHeight - 19 - (lines.size() - 1 - i) * 16, 402, 14, juce::Justification::centredLeft, false);
        return;
    }

    juce::AttributedString s;
    const auto font = monoFont (10.0f);
    s.append (helpTitle, font, colours::text);
    s.append (" - " + helpBody, font, colours::dim);
    s.setLineSpacing (2.0f);
    s.setWordWrap (juce::AttributedString::byWord);
    s.draw (g, helpArea().toFloat());
}
} // namespace aec::ui
