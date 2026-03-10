#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <atomic>
#include <vector>
#include <memory>

// Forward declarations for WebRTC AEC3
namespace webrtc {
    class EchoControl;
    class AudioBuffer;
    class HighPassFilter;
}

class WasapiLoopbackCapture;

class EchoCancellerEngine
{
public:
    EchoCancellerEngine();
    ~EchoCancellerEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void release();

    void process (const float* micIn, float* out, int numSamples,
                  WasapiLoopbackCapture& loopback);

    void setEnabled (bool shouldBeEnabled) { enabled.store (shouldBeEnabled, std::memory_order_release); }
    bool isEnabled() const { return enabled.load (std::memory_order_acquire); }

    void setAggressiveMode (bool on) { aggressiveMode.store (on, std::memory_order_release); }
    bool getAggressiveMode() const { return aggressiveMode.load (std::memory_order_acquire); }

    void setMonitorFarEnd (bool on) { monitorFarEnd.store (on, std::memory_order_release); }
    bool getMonitorFarEnd() const { return monitorFarEnd.load (std::memory_order_acquire); }

private:
    void createAEC3 (bool aggressive);

    static constexpr int kAecRate = 48000;
    static constexpr int kAecFrameSize = 480; // 10ms at 48000
    static constexpr int kMaxLoopbackRead = 8192;

    // AEC3 state
    std::unique_ptr<webrtc::EchoControl> echoController;
    std::unique_ptr<webrtc::AudioBuffer> renderAudio;
    std::unique_ptr<webrtc::AudioBuffer> captureAudio;
    std::unique_ptr<webrtc::HighPassFilter> hpFilter;

    int hostSampleRate = 0;
    bool needsHostResampling = false;
    bool currentAggressiveMode = false;

    // FIFOs (float, at kAecRate = 48000)
    juce::AbstractFifo nearEndFifo { 1 };
    std::vector<float> nearEndBuffer;

    juce::AbstractFifo farEndFifo { 1 };
    std::vector<float> farEndBuffer;

    juce::AbstractFifo outputFifo { 1 };
    std::vector<float> outputBuffer;

    // Temp frame buffers (kAecFrameSize = 480 each)
    std::vector<float> tempNearFrame;
    std::vector<float> tempFarFrame;
    std::vector<float> tempOutFrame;

    // Loopback temp buffers
    std::vector<int16_t> tempLoopbackRaw;
    std::vector<float> tempLoopbackFloat;
    std::vector<float> tempLoopbackResampled;

    // Host rate resampling temp buffers
    std::vector<float> tempMicResampled;
    std::vector<float> tempOutputPull;

    // Resamplers (JUCE LagrangeInterpolator)
    juce::LagrangeInterpolator inputResampler;    // host -> 48k
    juce::LagrangeInterpolator outputResampler;   // 48k -> host
    juce::LagrangeInterpolator loopbackResampler; // loopback -> 48k
    int lastLoopbackRate = 0;

    std::atomic<bool> enabled { true };
    std::atomic<bool> aggressiveMode { false };
    std::atomic<bool> monitorFarEnd { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoCancellerEngine)
};
