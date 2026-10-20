#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <limits>
#include <vector>

namespace aec::test
{
    /** Uniform white noise with the given peak amplitude, uniform in [-amp, amp). Deterministic per seed. */
    inline std::vector<float> whiteNoise (int n, float amp, juce::uint32 seed)
    {
        juce::Random random ((juce::int64) seed);
        std::vector<float> v ((size_t) juce::jmax (0, n));

        for (auto& x : v)
            x = amp * (2.0f * random.nextFloat() - 1.0f);

        return v;
    }

    /** amp * sin(2*pi*hz*i/sr), i = 0..n-1, computed in double. */
    inline std::vector<float> sine (int n, float hz, float amp, double sr)
    {
        std::vector<float> v ((size_t) juce::jmax (0, n));
        const double w = juce::MathConstants<double>::twoPi * (double) hz / sr;

        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) ((double) amp * std::sin (w * (double) i));

        return v;
    }

    /** True in the "on" part of the speech-like gating: 10 ms frame index % 40 < 30 (300 ms on, 100 ms gap). */
    inline bool speechLikeOn (size_t i, double sr)
    {
        const auto frame = (long long) ((double) i * 100.0 / sr);
        return frame % 40 < 30;
    }

    /** Speech-like gated sine: 300 ms at rmsDbfs, 100 ms gap at rmsDbfs - gapDb, repeating. A continuous sine
        looks like stationary noise to AGC2's noise-floor estimator, which then forbids any gain above the
        noise ceiling; tests that expect the AGC to raise a signal use this instead. */
    inline std::vector<float> speechLikeSine (int n, float hz, float rmsDbfs, double sr, float gapDb = 35.0f)
    {
        const float amp = std::pow (10.0f, rmsDbfs / 20.0f) * 1.41421356f;
        const float g = std::pow (10.0f, -gapDb / 20.0f);
        auto v = sine (n, hz, amp, sr);

        for (size_t i = 0; i < v.size(); ++i)
            if (! speechLikeOn (i, sr))
                v[i] *= g;

        return v;
    }

    /** The lag in [0, maxLag] maximising sum_i a[i] * b[i + lag] (first maximum wins). */
    inline int crossCorrelationLag (const std::vector<float>& a, const std::vector<float>& b, int maxLag)
    {
        int bestLag = 0;
        double bestSum = -std::numeric_limits<double>::infinity();

        for (int lag = 0; lag <= maxLag; ++lag)
        {
            const int count = juce::jmin ((int) a.size(), (int) b.size() - lag);
            double sum = 0.0;

            for (int i = 0; i < count; ++i)
                sum += (double) a[(size_t) i] * (double) b[(size_t) (i + lag)];

            if (sum > bestSum)
            {
                bestSum = sum;
                bestLag = lag;
            }
        }

        return bestLag;
    }

    /** 20*log10(rms of x[0..n)); -200 if the rms is 0 or n <= 0. */
    inline float rmsDb (const float* x, int n)
    {
        if (n <= 0)
            return -200.0f;

        double sumSq = 0.0;

        for (int i = 0; i < n; ++i)
            sumSq += (double) x[i] * (double) x[i];

        if (sumSq <= 0.0)
            return -200.0f;

        return (float) (10.0 * std::log10 (sumSq / (double) n));
    }
}
