#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace aec::ui
{
/** Horizontal pill group [Off] Gentle / Normal / Strong [Custom] for the auto gain.
    Off clears agcEnabled (the four values stay); a preset pill applies that row and turns AGC on.
    Custom is read-only. refresh() (editor timer) lights Off while AGC is off (withOff), otherwise the
    preset matchAgcPreset reports; custom values light the Custom pill, or nothing without withCustom. */
class AgcPresetSelector final : public juce::Component
{
public:
    static constexpr int kPillWidth = 48, kPillHeight = 18, kGap = 4;

    AgcPresetSelector (juce::AudioProcessorValueTreeState&, bool withOff, bool withCustom);

    /** Re-reads the parameters and repaints only when the lit pill changed. */
    void refresh();
    /** Index into the pills as laid out (Off first when present), or -1 when nothing is lit. */
    int getLitIndex() const noexcept { return lit; }
    int getNumPills() const noexcept { return (int) labels.size(); }
    juce::String getPillText (int index) const { return labels[(size_t) index]; }
    /** What a click on pill `index` does (Custom is not clickable). */
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
    const bool hasOff, hasCustom;
    std::vector<juce::String> labels;
    int lit = -1, hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AgcPresetSelector)
};
} // namespace aec::ui
