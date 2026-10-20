#include "OutputLimiter.h"

#include <algorithm>
#include <cmath>

#include "api/audio/audio_view.h"
#include "modules/audio_processing/agc2/limiter.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"

namespace aec
{
namespace
{
constexpr float kFullScale = 32768.0f;
constexpr float kUp = kFullScale / OutputLimiter::kCeilingLinear;
constexpr float kDown = OutputLimiter::kCeilingLinear / kFullScale;
}

OutputLimiter::OutputLimiter()
    : dumper (std::make_unique<webrtc::ApmDataDumper> (0)),
      limiter (std::make_unique<webrtc::Limiter> (dumper.get(), kFrameSize, "Agc2"))
{
}
OutputLimiter::~OutputLimiter() = default;

void OutputLimiter::reset() noexcept
{
    limiter->Reset();
    lastReductionDb = 0.0f;
}

void OutputLimiter::process (const float* in, float* out) noexcept
{
    float inPeak = 0.0f;
    for (int i = 0; i < kFrameSize; ++i)
    {
        inPeak = std::max (inPeak, std::abs (in[i]));
        buffer[i] = in[i] * kUp;
    }

    limiter->Process (webrtc::DeinterleavedView<float> (buffer, kFrameSize, 1));

    float outPeak = 0.0f;
    for (int i = 0; i < kFrameSize; ++i)
    {
        out[i] = buffer[i] * kDown;
        outPeak = std::max (outPeak, std::abs (out[i]));
    }

    lastReductionDb = (inPeak > 0.0f && outPeak > 0.0f) ? std::max (0.0f, 20.0f * std::log10 (inPeak / outPeak)) : 0.0f;
}
} // namespace aec
