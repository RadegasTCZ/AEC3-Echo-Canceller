#pragma once

#include <memory>

namespace webrtc
{
class ApmDataDumper;
class VoiceActivityDetectorWrapper;
class SpeechLevelEstimator;
class NoiseLevelEstimator;
class SaturationProtector;
class AdaptiveDigitalGainController;
class FixedDigitalLevelEstimator;
}

namespace aec
{
struct AgcSettings
{
    bool enabled = false;
    float headroomDb = 5.0f;          // 0..20
    float maxGainDb = 30.0f;          // 1..50
    float adaptSpeedDbPerSec = 6.0f;  // 1..30
    float noiseCeilingDbfs = -50.0f;  // -80..-20

    bool operator== (const AgcSettings&) const = default;

    /** Non-finite fields become the defaults above; every field is clamped to its range. */
    AgcSettings sanitised() const;
};

/** WebRTC AGC2 adaptive digital gain (no input volume controller, no fixed gain, no limiter).
    480-sample frames at 48 kHz, +-1.0 scale, processed in place. */
class AgcStage
{
public:
    static constexpr int kFrameSize = 480;
    static constexpr int kSampleRate = 48000;

    /** Allocates: call from prepare()/the worker/tests only. initialGainDb < 0 (or non-finite): the default
        start gain min (15, maxGain) dB; otherwise the stage starts at clamp (initialGainDb, 0, maxGain), e.g.
        the gain a running stage applies when its settings change (no re-learn from 15 dB). */
    explicit AgcStage (const AgcSettings&, float initialGainDb = -1.0f);
    ~AgcStage();

    /** In place, never allocates. speechProbabilityOverride >= 0 replaces the VAD (test hook). */
    void process (float* frame, float speechProbabilityOverride = -1.0f) noexcept;

    /** Mean gain (dB) applied to the last frame; keeps the previous value for silent frames. Before the
        first non-silent frame: the initial gain. */
    float getLastGainDb() const noexcept { return lastGainDb; }

private:
    std::unique_ptr<webrtc::ApmDataDumper> dumper;
    std::unique_ptr<webrtc::VoiceActivityDetectorWrapper> vad;
    std::unique_ptr<webrtc::SpeechLevelEstimator> speechLevel;
    std::unique_ptr<webrtc::NoiseLevelEstimator> noiseLevel;
    std::unique_ptr<webrtc::SaturationProtector> saturationProtector;
    std::unique_ptr<webrtc::AdaptiveDigitalGainController> controller;
    std::unique_ptr<webrtc::FixedDigitalLevelEstimator> outputLevel;
    float lastGainDb = 0.0f;
    float buffer[kFrameSize] {};
};
} // namespace aec
