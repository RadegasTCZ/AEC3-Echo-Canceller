#include "CompactSlider.h"
#include "Theme.h"

namespace aec::ui
{
CompactSlider::CompactSlider (juce::RangedAudioParameter& p, const juce::String& labelText, float normal)
    : parameter (p), paramId (p.getParameterID()), label (labelText), normalValue (normal),
      attachment (p, [this] (float v) { value = v; repaint(); })
{
    setSize (196, kHeight);
    attachment.sendInitialUpdate();
}

juce::Rectangle<int> CompactSlider::getTrackBounds() const
{
    return { kLabelWidth, (kHeight - 4) / 2, juce::jmax (1, getWidth() - kLabelWidth - kValueWidth - 6), 4 };
}

float CompactSlider::fraction (float v) const
{
    return juce::jlimit (0.0f, 1.0f, parameter.convertTo0to1 (v));
}

int CompactSlider::getTickX() const
{
    const auto t = getTrackBounds();
    return t.getX() + juce::roundToInt (fraction (normalValue) * (float) (t.getWidth() - 2));
}

void CompactSlider::paint (juce::Graphics& g)
{
    g.setColour (colours::dim);
    g.setFont (uiFont (10.0f));
    g.drawText (label, 0, 0, kLabelWidth, kHeight, juce::Justification::centredLeft, true);

    const auto t = getTrackBounds().toFloat();
    g.setColour (colours::well);
    g.fillRoundedRectangle (t, 2.0f);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (t.withWidth (juce::jmax (0.0f, t.getWidth() * fraction (value))), 2.0f);

    g.setColour (colours::dim.withAlpha (0.6f));                 // Normal (WebRTC default) tick
    g.fillRect (getTickX(), (kHeight - 10) / 2, 2, 10);

    g.setColour (colours::text);
    g.setFont (monoFont (10.0f));
    g.drawText (juce::String (juce::roundToInt (value)), getWidth() - kValueWidth, 0, kValueWidth, kHeight,
                juce::Justification::centredRight, false);
}

void CompactSlider::mouseEnter (const juce::MouseEvent&)
{
    if (onHoverOrClick)
        onHoverOrClick (paramId);
}

void CompactSlider::dragTo (const juce::MouseEvent& e)
{
    const auto t = getTrackBounds();
    const float f = juce::jlimit (0.0f, 1.0f, (float) (e.x - t.getX()) / (float) t.getWidth());
    const auto range = parameter.getNormalisableRange();
    attachment.setValueAsPartOfGesture (range.snapToLegalValue (parameter.convertFrom0to1 (f)));
}

void CompactSlider::mouseDown (const juce::MouseEvent& e)
{
    if (onHoverOrClick)
        onHoverOrClick (paramId);

    if (e.mods.isLeftButtonDown() && e.x >= kLabelWidth && e.x < getWidth() - kValueWidth)
    {
        dragging = true;
        attachment.beginGesture();
        dragTo (e);
    }
}

void CompactSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        dragTo (e);
}

void CompactSlider::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }
}
} // namespace aec::ui
