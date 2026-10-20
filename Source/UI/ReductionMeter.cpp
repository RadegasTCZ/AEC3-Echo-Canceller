#include "ReductionMeter.h"
#include "Theme.h"

namespace aec::ui
{
ReductionMeter::ReductionMeter (aec::ReductionHold& s) : source (s)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

juce::Rectangle<int> ReductionMeter::barBounds() const
{
    const int x = kLabelWidth + kGap;
    const int w = juce::jmax (0, getWidth() - x - kGap - kNumberWidth);
    return { x, (kHeight - kBarHeight) / 2, w, kBarHeight };
}

ReductionMeter::Shown ReductionMeter::shownFor() const
{
    Shown s;
    s.barPx = juce::roundToInt (juce::jlimit (0.0f, 1.0f, shownDb / kRangeDb) * (float) barBounds().getWidth());
    s.text = shownDb < 0.05f ? juce::String ("0.0") : "-" + juce::String (shownDb, 1);
    return s;
}

void ReductionMeter::tick (float dtSec)
{
    float in = source.takeDb();
    if (! (in >= 0.0f))                       // NaN counts as no reduction
        in = 0.0f;
    in = juce::jmin (in, kRangeDb);
    shownDb = in >= shownDb ? in : juce::jmax (in, shownDb - kFallDbPerSec * juce::jmax (0.0f, dtSec));

    const auto s = shownFor();
    if (s != shown)
    {
        shown = s;
        ++repaintRequests;
        repaint();
    }
}

void ReductionMeter::resized()
{
    shown = shownFor();
}

void ReductionMeter::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    auto line = getLocalBounds();
    g.setColour (colours::dim);
    g.setFont (uiFont (10.0f, true));
    g.drawText ("LIM", line.removeFromLeft (kLabelWidth), juce::Justification::centredLeft, false);

    const auto bar = barBounds().toFloat();
    g.setColour (colours::well);
    g.fillRoundedRectangle (bar, 1.0f);
    g.setColour (colours::accent);
    g.fillRect (bar.withLeft (bar.getRight() - (float) shown.barPx));

    g.setColour (colours::text);
    g.setFont (monoFont (11.0f));
    g.drawText (shown.text, line.removeFromRight (kNumberWidth), juce::Justification::centredRight, false);
}
} // namespace aec::ui
