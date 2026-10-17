#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SegmentedSelector.h"

namespace aec::ui
{
/** Port of FM-Ready Chain's "dark studio" Theme, trimmed to what this editor uses.
    Colours as constexpr ARGB words: safe in any static initialiser. */
namespace argb
{
    inline constexpr juce::uint32 bg = 0xff16181b, panel = 0xff1f2226, well = 0xff111316, line = 0xff2d3137,
                                  text = 0xffe6e9ed, dim = 0xff8a919a, accent = 0xfff0a050, level = 0xff4ea4e6,
                                  knob = 0xff2a2e33,
                                  warn = 0xffe5534b,       // "Unsupported sample rate" (FM's limit75 red)
                                  onAccent = 0xff1b1206;   // text on an accent fill
}

/** The same colours as juce::Colour. Dynamically initialised: use at run time only. */
namespace colours
{
    extern const juce::Colour bg, panel, well, line, text, dim, accent, level, knob, warn;
}

juce::Font monoFont (float height, bool bold = false);
juce::Font uiFont (float height, bool bold = false);
/** Bold UI font with letter-spacing (kerning factor, e.g. 0.08 = .08em): title and box captions. */
juce::Font spacedBoldFont (float height, float kerningFactor);

/** Flat panels, orange value arcs drawn from the rotary start, pill toggles, flat combo box,
    segmented-selector pills. */
class Theme final : public juce::LookAndFeel_V4,
                    public SegmentedSelector::LookAndFeelMethods
{
public:
    static constexpr float valueFontHeight = 12.0f;
    Theme();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    /** Slider value boxes: monospaced 12 px with a 2/3 px border, so a 16 px box shows the full glyph height. */
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (12.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return uiFont (12.0f); }
    /** Selected: accent fill and outline, onAccent bold mono text. Others: knob fill, line outline
        (dim when hovered), dim text (text colour when hovered). Fully rounded ends. */
    void drawSegmentPill (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& text,
                          bool selected, bool highlighted) override;
};
} // namespace aec::ui
