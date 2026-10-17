#include "Theme.h"
#include <cmath>

namespace aec::ui
{
namespace colours
{
    const juce::Colour bg { argb::bg }, panel { argb::panel }, well { argb::well }, line { argb::line },
                       text { argb::text }, dim { argb::dim }, accent { argb::accent }, level { argb::level },
                       knob { argb::knob }, warn { argb::warn };
}

juce::Font monoFont (float h, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font uiFont (float h, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font spacedBoldFont (float h, float kerning)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), h, juce::Font::bold).withKerningFactor (kerning));
}

Theme::Theme()
{
    using namespace colours;
    const juce::Colour onText (argb::onAccent);
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId, dim);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, onText);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, onText);
    setColour (juce::ToggleButton::textColourId, dim);
    setColour (juce::ToggleButton::tickColourId, accent);   // "on" pill colour
    setColour (juce::TextEditor::backgroundColourId, well);  // slider value box in-place editor
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, line);
}

juce::Label* Theme::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (monoFont (valueFontHeight));
    l->setBorderSize (juce::BorderSize<int> (2, 3, 2, 3));
    l->setJustificationType (juce::Justification::centred);
    return l;
}

void Theme::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    using namespace colours;
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    const auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    const float radius = d * 0.5f - 2.0f, cx = c.getCentreX(), cy = c.getCentreY();
    g.setColour (knob);
    g.fillEllipse (c.reduced (3.0f));
    juce::Path track;
    track.addCentredArc (cx, cy, radius, radius, 0.0f, start, end, true);
    g.setColour (well);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // value arc from the rotary start to the pointer (no bipolar ranges in this editor): pos 0 = no arc.
    // SuppressKnob flips pos (D13), so its arc grows with suppression strength.
    const float a1 = start + pos * (end - start);
    if (pos > 1.0e-3f)
    {
        juce::Path val;
        val.addCentredArc (cx, cy, radius, radius, 0.0f, start, a1, true);
        g.setColour (s.isEnabled() ? accent : dim);
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Path tick;
    tick.addRectangle (-1.0f, -radius + 4.0f, 2.0f, radius * 0.45f);
    g.setColour (text);
    g.fillPath (tick, juce::AffineTransform::rotation (a1).translated (cx, cy));
}

void Theme::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    using namespace colours;
    const auto r = b.getLocalBounds().toFloat();
    const auto pill = juce::Rectangle<float> (24.0f, 12.0f).withPosition (r.getX() + 1.0f, r.getCentreY() - 6.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? b.findColour (juce::ToggleButton::tickColourId) : line);
    g.fillRoundedRectangle (pill, 6.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (on ? pill.getRight() - 10.0f : pill.getX() + 2.0f, pill.getY() + 2.0f, 8.0f, 8.0f);
    g.setColour (highlighted || on ? text : b.findColour (juce::ToggleButton::textColourId));
    g.setFont (uiFont (12.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (32.0f), juce::Justification::centredLeft, true);
}

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    using namespace colours;
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : (down ? line : panel));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (highlighted ? dim : line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void Theme::drawSegmentPill (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& label,
                             bool selected, bool highlighted)
{
    using namespace colours;
    const auto r = bounds.reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    g.setColour (selected ? accent : knob);
    g.fillRoundedRectangle (r, radius);
    g.setColour (selected ? accent : (highlighted ? dim : line));
    g.drawRoundedRectangle (r, radius, 1.0f);
    g.setColour (selected ? juce::Colour (argb::onAccent) : (highlighted ? text : dim));
    g.setFont (monoFont (10.0f, selected));
    g.drawText (label, bounds, juce::Justification::centred, false);
}

void Theme::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    using namespace colours;
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (well);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f - 2.0f;
    arrow.addTriangle (ax, ay, ax + 8.0f, ay, ax + 4.0f, ay + 5.0f);
    g.setColour (dim);
    g.fillPath (arrow);
}
} // namespace aec::ui
