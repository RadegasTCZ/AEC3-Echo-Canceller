#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace aec::ui
{
/** A grid of pill buttons bound to a Choice parameter: pill i = choice index i, laid
    out row by row, `columns` per row. Clicking a pill sets the parameter as one gesture; host
    automation and state loads move the selection (juce::ParameterAttachment, synchronous on the
    message thread). Drawn by the look-and-feel when it implements LookAndFeelMethods (Theme does). */
class SegmentedSelector final : public juce::Component
{
public:
    struct LookAndFeelMethods
    {
        virtual ~LookAndFeelMethods() = default;
        virtual void drawSegmentPill (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& text,
                                      bool selected, bool highlighted) = 0;
    };

    /** labels.size() == the parameter's number of choices. Pills are pillWidth x pillHeight, `gap` px apart. */
    SegmentedSelector (juce::RangedAudioParameter& parameter, const juce::StringArray& labels,
                       int columns, int pillWidth, int pillHeight, int gap);

    /** Width x height that exactly fits the grid (set it as the component size). */
    juce::Rectangle<int> gridBounds() const;

    int getNumSegments() const noexcept    { return labels.size(); }
    int getSelectedIndex() const noexcept  { return selected; }
    juce::String getSegmentText (int index) const { return labels[index]; }
    /** Pill `index` in local coordinates. */
    juce::Rectangle<int> getSegmentBounds (int index) const;
    /** The pill under a local point, -1 for none (gaps included). */
    int segmentAt (juce::Point<int> localPoint) const;
    /** What a click on pill `index` does: sets the parameter (no-op if out of range or already selected). */
    void selectIndex (int index);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setHovered (int index);

    const juce::StringArray labels;
    const int columns, pillWidth, pillHeight, gap;
    int selected = 0, hovered = -1;
    juce::ParameterAttachment attachment;   // last member: its callback touches `selected`

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedSelector)
};
} // namespace aec::ui
