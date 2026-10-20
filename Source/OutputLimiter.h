#pragma once

#include <atomic>
#include <memory>

namespace webrtc
{
class ApmDataDumper;
class Limiter;
}

namespace aec
{
/** Max gain reduction (>= 0 dB) since the last take. push() on the audio thread, takeDb() on the UI thread. */
class ReductionHold
{
public:
    void push (float reductionDb) noexcept
    {
        float prev = held.load (std::memory_order_relaxed);
        while (reductionDb > prev && ! held.compare_exchange_weak (prev, reductionDb, std::memory_order_relaxed))
        {
        }
    }

    float takeDb() noexcept { return held.exchange (0.0f, std::memory_order_relaxed); }

private:
    std::atomic<float> held { 0.0f };
};

/** AGC2 limiter with a -1 dBFS ceiling. 480-sample frames at 48 kHz, +-1.0 scale. */
class OutputLimiter
{
public:
    static constexpr int kFrameSize = 480;
    static constexpr float kCeilingLinear = 0.8912509f; // 10^(-1/20)

    OutputLimiter();  // allocates: call from prepare()/tests only
    ~OutputLimiter();

    /** in/out: kFrameSize samples; may alias. Never allocates. */
    void process (const float* in, float* out) noexcept;

    /** 20*log10 (max|in| / max|out|) of the last frame; 0 when silent. */
    float getLastReductionDb() const noexcept { return lastReductionDb; }

    void reset() noexcept;

private:
    std::unique_ptr<webrtc::ApmDataDumper> dumper;
    std::unique_ptr<webrtc::Limiter> limiter;
    float lastReductionDb = 0.0f;
    float buffer[kFrameSize] {};
};
} // namespace aec
