#pragma once

#include <JuceHeader.h>

struct SpeexResamplerState_;   // SpeexDSP opaque state (speex/speex_resampler.h)

namespace aec
{
/** One mono SpeexDSP resampler, quality 5 (SPEEX_RESAMPLER_QUALITY_DESKTOP).
    prepare() allocates (message thread); process()/reset() never allocate. */
class Resampler
{
public:
    Resampler() = default;
    ~Resampler();

    /** Returns false if speex init fails or a rate is <= 0 (then isPassthrough() is false
        and process() writes nothing). Equal rates → passthrough, no speex state. */
    bool prepare (int inRate, int outRate);
    void reset() noexcept;
    bool isPassthrough() const noexcept { return passthrough; }

    /** Consumes all numIn samples; returns the number written to out.
        maxOut must be >= maxOutputFor (numIn, inRate, outRate). */
    int process (const float* in, int numIn, float* out, int maxOut) noexcept;

    int inputLatency() const noexcept;    // input samples, 0 if passthrough
    int outputLatency() const noexcept;   // output samples, 0 if passthrough

    /** ceil (numIn * outRate / inRate) + 16 */
    static int maxOutputFor (int numIn, int inRate, int outRate) noexcept;

private:
    void destroy() noexcept;

    SpeexResamplerState_* state = nullptr;
    bool passthrough = true;

    JUCE_DECLARE_NON_COPYABLE (Resampler)
};
} // namespace aec
