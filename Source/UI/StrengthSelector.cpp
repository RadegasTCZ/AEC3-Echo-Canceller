#include "StrengthSelector.h"
#include "SegmentedSelector.h"
#include "../Strength.h"

namespace aec::ui
{
namespace
{
    const char* const kLabels[] = { "Gentle", "Normal", "Strong", "Custom" };
}

StrengthSelector::StrengthSelector (juce::AudioProcessorValueTreeState& state, bool withCustom, bool isHorizontal)
    : apvts (state), showCustom (withCustom), horizontal (isHorizontal), numPills (withCustom ? 4 : 3)
{
    if (horizontal)
        setSize (numPills * kHorizontalPillWidth + (numPills - 1) * kHorizontalGap, kPillHeight);
    else
        setSize (kPillWidth, numPills * kPillHeight + (numPills - 1) * kGap);
    lit = computeLit();
}

int StrengthSelector::computeLit() const
{
    const auto s = aec::matchStrength (apvts);
    if (s == aec::Strength::Custom)
        return showCustom ? 3 : -1;
    return (int) s;
}

void StrengthSelector::refresh()
{
    const int now = computeLit();
    if (now != lit)
    {
        lit = now;
        repaint();
    }
}

juce::Rectangle<int> StrengthSelector::getPillBounds (int index) const
{
    if (horizontal)
        return { index * (kHorizontalPillWidth + kHorizontalGap), 0, kHorizontalPillWidth, kPillHeight };
    return { 0, index * (kPillHeight + kGap), kPillWidth, kPillHeight };
}

int StrengthSelector::pillAt (juce::Point<int> p) const
{
    for (int i = 0; i < numPills; ++i)
        if (getPillBounds (i).contains (p))
            return i;
    return -1;
}

void StrengthSelector::selectIndex (int index)
{
    if (index < 0 || index > 2)
        return;
    aec::applyStrength (apvts, (aec::Strength) index);
    refresh();
}

void StrengthSelector::paint (juce::Graphics& g)
{
    auto* lf = dynamic_cast<SegmentedSelector::LookAndFeelMethods*> (&getLookAndFeel());
    for (int i = 0; i < numPills; ++i)
    {
        const auto r = getPillBounds (i).toFloat();
        const bool on = i == lit;
        if (lf != nullptr)
            lf->drawSegmentPill (g, r, kLabels[i], on, i == hovered && i < 3);
        else
        {
            g.setColour (on ? juce::Colours::orange : juce::Colours::darkgrey);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (on ? juce::Colours::black : juce::Colours::white);
            g.drawText (kLabels[i], r, juce::Justification::centred, false);
        }
    }
}

void StrengthSelector::mouseDown (const juce::MouseEvent& e)
{
    if (isEnabled() && e.mods.isLeftButtonDown())
        selectIndex (pillAt (e.getPosition()));
}

void StrengthSelector::mouseMove (const juce::MouseEvent& e)  { setHovered (pillAt (e.getPosition())); }
void StrengthSelector::mouseExit (const juce::MouseEvent&)    { setHovered (-1); }

void StrengthSelector::setHovered (int index)
{
    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}
} // namespace aec::ui
