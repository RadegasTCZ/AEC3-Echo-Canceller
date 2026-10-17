#include "SegmentedSelector.h"

namespace aec::ui
{
SegmentedSelector::SegmentedSelector (juce::RangedAudioParameter& parameter, const juce::StringArray& segmentLabels,
                                      int numColumns, int width, int height, int gapPx)
    : labels (segmentLabels),
      columns (juce::jmax (1, numColumns)),
      pillWidth (width),
      pillHeight (height),
      gap (gapPx),
      attachment (parameter, [this] (float value)
                  {
                      selected = juce::jlimit (0, juce::jmax (0, labels.size() - 1), juce::roundToInt (value));
                      repaint();
                  })
{
    jassert (labels.size() > 0);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&parameter))
        jassert (choice->choices.size() == labels.size());

    setSize (gridBounds().getWidth(), gridBounds().getHeight());
    attachment.sendInitialUpdate();
}

juce::Rectangle<int> SegmentedSelector::gridBounds() const
{
    const int n = labels.size();
    const int cols = juce::jmin (columns, n);
    const int rows = (n + columns - 1) / columns;
    return { 0, 0, cols * pillWidth + (cols - 1) * gap, rows * pillHeight + (rows - 1) * gap };
}

juce::Rectangle<int> SegmentedSelector::getSegmentBounds (int index) const
{
    const int row = index / columns, col = index % columns;
    return { col * (pillWidth + gap), row * (pillHeight + gap), pillWidth, pillHeight };
}

int SegmentedSelector::segmentAt (juce::Point<int> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (getSegmentBounds (i).contains (p))
            return i;
    return -1;
}

void SegmentedSelector::selectIndex (int index)
{
    if (juce::isPositiveAndBelow (index, labels.size()) && index != selected)
        attachment.setValueAsCompleteGesture ((float) index);   // the attachment callback updates `selected`
}

void SegmentedSelector::paint (juce::Graphics& g)
{
    auto* lf = dynamic_cast<LookAndFeelMethods*> (&getLookAndFeel());

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = getSegmentBounds (i).toFloat();
        const bool on = i == selected;

        if (lf != nullptr)
        {
            lf->drawSegmentPill (g, r, labels[i], on, i == hovered);
            continue;
        }

        // fallback without the Theme (e.g. a host-less preview): plain pills
        g.setColour (on ? juce::Colours::orange : juce::Colours::darkgrey);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? juce::Colours::black : juce::Colours::white);
        g.drawText (labels[i], r, juce::Justification::centred, false);
    }
}

void SegmentedSelector::mouseDown (const juce::MouseEvent& e)
{
    if (isEnabled() && e.mods.isLeftButtonDown())
        selectIndex (segmentAt (e.getPosition()));
}

void SegmentedSelector::mouseMove (const juce::MouseEvent& e)
{
    setHovered (segmentAt (e.getPosition()));
}

void SegmentedSelector::mouseExit (const juce::MouseEvent&)
{
    setHovered (-1);
}

void SegmentedSelector::setHovered (int index)
{
    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}
} // namespace aec::ui
