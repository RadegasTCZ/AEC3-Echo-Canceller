#include "AgcPresetSelector.h"
#include "SegmentedSelector.h"
#include "../AgcPresets.h"
#include "../ParamIds.h"

namespace aec::ui
{
AgcPresetSelector::AgcPresetSelector (juce::AudioProcessorValueTreeState& state, bool withOff, bool withCustom)
    : apvts (state), hasOff (withOff), hasCustom (withCustom)
{
    if (hasOff)
        labels.push_back ("Off");
    labels.push_back ("Gentle");
    labels.push_back ("Normal");
    labels.push_back ("Strong");
    if (hasCustom)
        labels.push_back ("Custom");

    const int n = getNumPills();
    setSize (n * kPillWidth + (n - 1) * kGap, kPillHeight);
    lit = computeLit();
}

int AgcPresetSelector::computeLit() const
{
    if (hasOff && apvts.getRawParameterValue (aec::ids::agcEnabled)->load() < 0.5f)
        return 0;
    const auto p = aec::matchAgcPreset (apvts);
    if (p == aec::AgcPreset::Custom)
        return hasCustom ? getNumPills() - 1 : -1;
    return (hasOff ? 1 : 0) + (int) p;
}

void AgcPresetSelector::refresh()
{
    const int now = computeLit();
    if (now != lit)
    {
        lit = now;
        repaint();
    }
}

juce::Rectangle<int> AgcPresetSelector::getPillBounds (int index) const
{
    return { index * (kPillWidth + kGap), 0, kPillWidth, kPillHeight };
}

int AgcPresetSelector::pillAt (juce::Point<int> p) const
{
    for (int i = 0; i < getNumPills(); ++i)
        if (getPillBounds (i).contains (p))
            return i;
    return -1;
}

void AgcPresetSelector::selectIndex (int index)
{
    if (index < 0 || index >= getNumPills())
        return;
    if (hasOff && index == 0)
    {
        if (auto* prm = apvts.getParameter (aec::ids::agcEnabled))
        {
            if (prm->getValue() >= 0.5f)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (0.0f);
                prm->endChangeGesture();
            }
        }
    }
    else
    {
        const int preset = index - (hasOff ? 1 : 0);
        if (preset > 2)
            return;                                   // Custom
        aec::applyAgcPreset (apvts, (aec::AgcPreset) preset);
    }
    refresh();
}

void AgcPresetSelector::paint (juce::Graphics& g)
{
    auto* lf = dynamic_cast<SegmentedSelector::LookAndFeelMethods*> (&getLookAndFeel());
    for (int i = 0; i < getNumPills(); ++i)
    {
        const auto r = getPillBounds (i).toFloat();
        const bool on = i == lit;
        const bool clickable = ! (hasCustom && i == getNumPills() - 1);
        if (lf != nullptr)
            lf->drawSegmentPill (g, r, labels[(size_t) i], on, i == hovered && clickable);
        else
        {
            g.setColour (on ? juce::Colours::orange : juce::Colours::darkgrey);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (on ? juce::Colours::black : juce::Colours::white);
            g.drawText (labels[(size_t) i], r, juce::Justification::centred, false);
        }
    }
}

void AgcPresetSelector::mouseDown (const juce::MouseEvent& e)
{
    if (isEnabled() && e.mods.isLeftButtonDown())
        selectIndex (pillAt (e.getPosition()));
}

void AgcPresetSelector::mouseMove (const juce::MouseEvent& e)  { setHovered (pillAt (e.getPosition())); }
void AgcPresetSelector::mouseExit (const juce::MouseEvent&)    { setHovered (-1); }

void AgcPresetSelector::setHovered (int index)
{
    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}
} // namespace aec::ui
