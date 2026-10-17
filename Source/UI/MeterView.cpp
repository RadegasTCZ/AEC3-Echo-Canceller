#include "MeterView.h"
#include "Theme.h"

namespace aec::ui
{
MeterView::MeterView (std::vector<Row> rows)
{
    states.resize (rows.size());
    for (size_t r = 0; r < states.size(); ++r)
    {
        states[r].source = rows[r].source;
        states[r].label = rows[r].label;
        states[r].ballistics.reset();
        states[r].shown = shownFor ((int) r);
    }
    setOpaque (true);                       // repaints never reach the editor behind it
    setInterceptsMouseClicks (false, false);
}

juce::Rectangle<int> MeterView::rowBounds (int row) const
{
    return { 0, row * (kRowHeight + kRowGap), getWidth(), kRowHeight };
}

juce::Rectangle<int> MeterView::barBounds (int row) const
{
    const auto r = rowBounds (row);
    const int x = kLabelWidth + kGap;
    const int w = juce::jmax (0, r.getWidth() - x - kGap - kNumberWidth);
    return { x, r.getY() + (kRowHeight - kBarHeight) / 2, w, kBarHeight };
}

MeterView::Shown MeterView::shownFor (int row) const
{
    const auto& b = states[(size_t) row].ballistics;
    const float w = (float) barBounds (row).getWidth();
    auto px = [w] (float db)
    {
        const float f = juce::jlimit (0.0f, 1.0f, (db - aec::MeterBallistics::floorDb) / -aec::MeterBallistics::floorDb);
        return juce::roundToInt (f * w);
    };
    Shown s;
    s.barPx = px (b.barDb());
    s.tickPx = px (b.holdDb());
    s.text = b.holdDb() <= aec::MeterBallistics::floorDb ? juce::String ("-inf") : juce::String (b.holdDb(), 1);
    return s;
}

void MeterView::tick (float dtSec)
{
    bool changed = false;
    for (size_t r = 0; r < states.size(); ++r)
    {
        states[r].ballistics.update (states[r].source->takeDb(), dtSec);
        const auto s = shownFor ((int) r);
        if (s != states[r].shown)
        {
            states[r].shown = s;
            changed = true;
        }
    }
    if (changed)
    {
        ++repaintRequests;
        repaint();
    }
}

void MeterView::resized()
{
    for (size_t r = 0; r < states.size(); ++r)
        states[r].shown = shownFor ((int) r);   // JUCE repaints a resized component itself
}

void MeterView::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    for (int r = 0; r < (int) states.size(); ++r)
    {
        const auto& row = states[(size_t) r];
        auto line = rowBounds (r);
        g.setColour (colours::dim);
        g.setFont (uiFont (10.0f, true));
        g.drawText (row.label, line.removeFromLeft (kLabelWidth), juce::Justification::centredLeft, false);

        const auto bar = barBounds (r).toFloat();
        g.setColour (colours::well);
        g.fillRoundedRectangle (bar, 1.0f);
        g.setColour (colours::level);
        g.fillRect (bar.withWidth ((float) row.shown.barPx));
        if (row.ballistics.holdDb() > aec::MeterBallistics::floorDb)
        {
            g.setColour (juce::Colours::white);
            const float x = juce::jlimit (bar.getX(), bar.getRight() - 2.0f, bar.getX() + (float) row.shown.tickPx - 1.0f);
            g.fillRect (x, bar.getY(), 2.0f, bar.getHeight());
        }

        g.setColour (colours::text);
        g.setFont (monoFont (11.0f));
        g.drawText (row.shown.text, line.removeFromRight (kNumberWidth), juce::Justification::centredRight, false);
    }
}
} // namespace aec::ui
