#include "Resampler.h"
#include "speex/speex_resampler.h"   // has its own extern "C" guard
#include <cstring>

namespace aec
{
Resampler::~Resampler()
{
    destroy();
}

void Resampler::destroy() noexcept
{
    if (state != nullptr)
    {
        speex_resampler_destroy (state);
        state = nullptr;
    }
}

bool Resampler::prepare (int inRate, int outRate)
{
    destroy();
    passthrough = false;

    if (inRate <= 0 || outRate <= 0)
        return false;

    if (inRate == outRate)
    {
        passthrough = true;
        return true;
    }

    int err = RESAMPLER_ERR_SUCCESS;
    state = speex_resampler_init (1, (spx_uint32_t) inRate, (spx_uint32_t) outRate,
                                  SPEEX_RESAMPLER_QUALITY_DESKTOP, &err);
    if (state == nullptr || err != RESAMPLER_ERR_SUCCESS)
    {
        destroy();
        return false;
    }
    return true;
}

void Resampler::reset() noexcept
{
    if (state != nullptr)
        speex_resampler_reset_mem (state);
}

int Resampler::process (const float* in, int numIn, float* out, int maxOut) noexcept
{
    if (numIn <= 0 || maxOut <= 0)
        return 0;

    if (passthrough)
    {
        jassert (numIn <= maxOut);
        const int n = juce::jmin (numIn, maxOut);
        if (out != in)
            std::memmove (out, in, (size_t) n * sizeof (float));
        return n;
    }

    if (state == nullptr)
        return 0;

    spx_uint32_t inLen = (spx_uint32_t) numIn;
    spx_uint32_t outLen = (spx_uint32_t) maxOut;
    speex_resampler_process_float (state, 0, in, &inLen, out, &outLen);
    jassert ((int) inLen == numIn);   // maxOut too small: input was not fully consumed
    return (int) outLen;
}

int Resampler::inputLatency() const noexcept
{
    return state != nullptr ? speex_resampler_get_input_latency (state) : 0;
}

int Resampler::outputLatency() const noexcept
{
    return state != nullptr ? speex_resampler_get_output_latency (state) : 0;
}

int Resampler::maxOutputFor (int numIn, int inRate, int outRate) noexcept
{
    if (numIn <= 0 || inRate <= 0 || outRate <= 0)
        return 16;
    const auto num = (juce::int64) numIn * outRate;
    return (int) ((num + inRate - 1) / inRate) + 16;
}
} // namespace aec
