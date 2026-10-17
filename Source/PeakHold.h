#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace aec
{
/** Peak since the last take. push() on the audio thread, takeDb() on the UI thread. */
class PeakHold
{
public:
    void push (const float* data, int n) noexcept
    {
        float m = 0.0f;
        for (int i = 0; i < n; ++i)
            m = juce::jmax (m, std::abs (data[i]));

        float prev = peak.load (std::memory_order_relaxed);
        while (m > prev && ! peak.compare_exchange_weak (prev, m, std::memory_order_relaxed))
        {
        }
    }

    /** 20*log10(max) floored at -100 dB; resets the held peak to 0. */
    float takeDb() noexcept
    {
        const float v = peak.exchange (0.0f, std::memory_order_relaxed);
        return juce::Decibels::gainToDecibels (v, -100.0f);
    }

private:
    std::atomic<float> peak { 0.0f };
};
} // namespace aec
