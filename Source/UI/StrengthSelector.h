#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace aec::ui
{
/** Vertical pill group Gentle / Normal / Strong (and an optional read-only Custom pill).
    Clicking a strength pill applies it to the 14 tuning parameters. refresh() (editor timer) lights
    the pill that matchStrength reports; with showCustom == false the Custom state lights no pill. */
class StrengthSelector final : public juce::Component
{
public:
    static constexpr int kPillWidth = 62, kPillHeight = 18, kGap = 3;
    static constexpr int kHorizontalPillWidth = 52, kHorizontalGap = 4;   // Advanced > Echo: one row, 52 x 18

    /** horizontal == false: a column of 62 x 18 pills; true: a row of 52 x 18 pills. */
    StrengthSelector (juce::AudioProcessorValueTreeState&, bool showCustom, bool horizontal = false);

    /** Re-reads the parameters and repaints only when the lit pill changed. */
    void refresh();
    /** 0..3 (Gentle, Normal, Strong, Custom), or -1 when nothing is lit. */
    int getLitIndex() const noexcept { return lit; }
    int getNumPills() const noexcept { return numPills; }
    /** What a click on pill `index` does: applies Gentle/Normal/Strong (the Custom pill is not clickable). */
    void selectIndex (int index);
    juce::Rectangle<int> getPillBounds (int index) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int pillAt (juce::Point<int>) const;
    int computeLit() const;
    void setHovered (int);

    juce::AudioProcessorValueTreeState& apvts;
    const bool showCustom, horizontal;
    const int numPills;
    int lit = -1, hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrengthSelector)
};
} // namespace aec::ui
