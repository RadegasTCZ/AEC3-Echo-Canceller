#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace aec::ui
{
/** One 20 px Advanced-page row: a 92 px label, a 4 px track (well) with an accent fill and a faint tick at the
    Normal value, and the value as mono text 28 px wide. Clicking or dragging the track sets the parameter as
    one gesture; hovering or pressing the row reports the parameter ID (the help strip listens). */
class CompactSlider final : public juce::Component
{
public:
    static constexpr int kHeight = 20, kLabelWidth = 92, kValueWidth = 28;

    /** normalValue is in the parameter's own units (e.g. tuningFor (Normal)[i], 0-100). */
    CompactSlider (juce::RangedAudioParameter&, const juce::String& label, float normalValue);

    std::function<void (const juce::String& paramId)> onHoverOrClick;

    const juce::String& getParamId() const noexcept   { return paramId; }
    float getNormalValue() const noexcept             { return normalValue; }
    float getValue() const noexcept                   { return value; }
    /** Sets the parameter to `v` (parameter units) as one complete gesture. */
    void setValueAsGesture (float v)                  { attachment.setValueAsCompleteGesture (v); }
    /** x of the Normal tick's left edge in local coordinates, and of the track. */
    juce::Rectangle<int> getTrackBounds() const;
    int getTickX() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    float fraction (float v) const;
    void dragTo (const juce::MouseEvent&);

    juce::RangedAudioParameter& parameter;
    const juce::String paramId, label;
    const float normalValue;
    float value = 0.0f;
    bool dragging = false;
    juce::ParameterAttachment attachment;   // last member: its callback touches `value`

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompactSlider)
};
} // namespace aec::ui
