#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../OutputLimiter.h"

namespace aec::ui
{
/** One 16 px meter row for the limiter gain reduction: label "LIM" (24 px), bar showing 0..24 dB filled
    from the right (accent on the well colour), number "-x.x" (38 px, mono 11; "0.0" at no reduction).
    tick() runs on the message thread: it takes the held maximum from the source, rises at once, falls
    12 dB/s, and repaints only when the bar width or the text changes. */
class ReductionMeter final : public juce::Component
{
public:
    static constexpr int kHeight = 16, kLabelWidth = 24, kNumberWidth = 38, kGap = 6, kBarHeight = 8;
    static constexpr float kRangeDb = 24.0f, kFallDbPerSec = 12.0f;

    explicit ReductionMeter (aec::ReductionHold&);

    void tick (float dtSec);

    float getReductionDb() const noexcept   { return shownDb; }
    juce::String numberText() const         { return shown.text; }
    int getBarPixels() const noexcept       { return shown.barPx; }
    int getRepaintRequestCount() const noexcept { return repaintRequests; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Shown
    {
        int barPx = 0;
        juce::String text { "0.0" };
        bool operator!= (const Shown& o) const { return barPx != o.barPx || text != o.text; }
    };

    juce::Rectangle<int> barBounds() const;
    Shown shownFor() const;

    aec::ReductionHold& source;
    float shownDb = 0.0f;
    Shown shown;
    int repaintRequests = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReductionMeter)
};
} // namespace aec::ui
