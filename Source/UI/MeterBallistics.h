#pragma once
#include <algorithm>

namespace aec
{
/** Display ballistics of one peak meter row (as in FM-Ready Chain), in dBFS.
    Bar: rises at once, otherwise falls at fallDbPerSec toward the input. Hold tick: jumps to every
    new maximum, holds it holdSec, then falls at holdFallDbPerSec, never below the bar.
    Nothing goes below floorDb (NaN input counts as the floor). Pure C++: no JUCE GUI dependency. */
struct MeterBallistics
{
    static constexpr float floorDb = -60.0f, fallDbPerSec = 12.0f, holdSec = 1.5f, holdFallDbPerSec = 20.0f;

    void reset() noexcept { bar = hold = floorDb; holdAge = 0.0f; }

    void update (float peakDb, float dtSec) noexcept
    {
        const float dt = std::max (0.0f, dtSec);
        const float in = std::max (floorDb, peakDb);          // std::max (floor, NaN) == floor
        bar = in >= bar ? in : std::max (in, bar - fallDbPerSec * dt);

        if (in >= hold)                                        // new maximum: restart the hold
        {
            hold = in;
            holdAge = 0.0f;
            return;
        }

        const float before = holdAge;
        holdAge += dt;
        if (holdAge > holdSec)                                 // fall only for the part of dt past the hold
            hold = std::max (bar, hold - holdFallDbPerSec * (holdAge - std::max (before, holdSec)));
    }

    float barDb() const noexcept  { return bar; }
    float holdDb() const noexcept { return hold; }

private:
    float bar = floorDb, hold = floorDb, holdAge = 0.0f;
};
} // namespace aec
