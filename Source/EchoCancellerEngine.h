#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <atomic>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

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
    // All tunable AEC3 parameters, read from APVTS atomics each process block
    struct Params
    {
        // Normal tuning
        float lfSuppression   = 75.0f;   // 0-100%, step 5
        float lfTransparency  = 80.0f;
        float hfSuppression   = 80.0f;
        float hfTransparency  = 85.0f;
        float lfAttack        = 30.0f;
        float lfDecay         = 25.0f;
        float hfDucking       = 0.0f;
        float hfEchoThreshold = 50.0f;

        // Double-talk (nearend) tuning
        float dtLfSuppression  = 25.0f;
        float dtLfTransparency = 30.0f;
        float dtHfSuppression  = 40.0f;
        float dtHfTransparency = 80.0f;
        float dtLfAttack       = 30.0f;
        float dtLfDecay        = 25.0f;

        // System
        float roomReverb       = 13.0f;   // 1-40 blocks
        bool  boundedErl       = false;
        bool  clockDrift       = false;

        bool operator== (const Params& o) const
        {
            return lfSuppression == o.lfSuppression
                && lfTransparency == o.lfTransparency
                && hfSuppression == o.hfSuppression
                && hfTransparency == o.hfTransparency
                && lfAttack == o.lfAttack
                && lfDecay == o.lfDecay
                && hfDucking == o.hfDucking
                && hfEchoThreshold == o.hfEchoThreshold
                && dtLfSuppression == o.dtLfSuppression
                && dtLfTransparency == o.dtLfTransparency
                && dtHfSuppression == o.dtHfSuppression
                && dtHfTransparency == o.dtHfTransparency
                && dtLfAttack == o.dtLfAttack
                && dtLfDecay == o.dtLfDecay
                && roomReverb == o.roomReverb
                && boundedErl == o.boundedErl
                && clockDrift == o.clockDrift;
        }
        bool operator!= (const Params& o) const { return ! (*this == o); }
    };

    EchoCancellerEngine();
    ~EchoCancellerEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void release();

    void process (const float* micIn, float* out, int numSamples,
                  WasapiLoopbackCapture& loopback);

    void setEnabled (bool shouldBeEnabled) { enabled.store (shouldBeEnabled, std::memory_order_release); }
    bool isEnabled() const { return enabled.load (std::memory_order_acquire); }

    void setMonitorFarEnd (bool on) { monitorFarEnd.store (on, std::memory_order_release); }
    bool getMonitorFarEnd() const { return monitorFarEnd.load (std::memory_order_acquire); }

    // Latency compensation — fed to AEC3 per-frame via SetAudioBufferDelay, no recreation needed
    void setLatencyCompMs (float ms) { latencyCompMs.store (ms, std::memory_order_relaxed); }

    // Called from the audio thread each block with current APVTS values
    void setParams (const Params& p);

    // Request full re-initialization from the UI thread (executed by audio thread)
    void requestReset() { resetRequested.store (true, std::memory_order_release); }

private:
    // Holds a complete AEC3 instance for atomic swap
    struct AEC3Instance
    {
        std::unique_ptr<webrtc::EchoControl> controller;
        std::unique_ptr<webrtc::AudioBuffer> render;
        std::unique_ptr<webrtc::AudioBuffer> capture;
        std::unique_ptr<webrtc::HighPassFilter> hp;
    };

    AEC3Instance createAEC3Instance (const Params& p);

    // Map 0-100% slider values to AEC3 config values
    static float mapSuppression (float pct);       // 0-100% → 1.0-0.01 (inverted)
    static float mapHfSuppression (float pct);     // 0-100% → 0.5-0.01 (inverted, narrower range)
    static float mapTransparency (float pct);      // 0-100% → 1.0-0.01 (inverted)
    static float mapHfTransparency (float pct);    // 0-100% → 0.5-0.005 (inverted)
    static float mapAttack (float pct);            // 0-100% → 0.5-5.0
    static float mapDecay (float pct);             // 0-100% → 0.01-1.0
    static float mapHfDucking (float pct);         // 0-100% → 1.0-0.0 (inverted)
    static float mapHfEchoThreshold (float pct);   // 0-100% → 2.0-0.1 (inverted)

    static constexpr int kAecRate = 48000;
    static constexpr int kAecFrameSize = 480; // 10ms at 48000
    static constexpr int kMaxLoopbackRead = 8192;

    // AEC3 state
    std::unique_ptr<webrtc::EchoControl> echoController;
    std::unique_ptr<webrtc::AudioBuffer> renderAudio;
    std::unique_ptr<webrtc::AudioBuffer> captureAudio;
    std::unique_ptr<webrtc::HighPassFilter> hpFilter;

    int hostSampleRate = 0;
    int hostBlockSize = 0;
    bool needsHostResampling = false;

    // Current applied params (for dirty check)
    Params currentParams;

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
    bool wasEnabled = true; // transition detection — audio thread only
    std::atomic<bool> monitorFarEnd { false };
    std::atomic<float> latencyCompMs { 0.0f };

    // Factory reset — signalled by UI thread, executed by audio thread
    std::atomic<bool> resetRequested { false };

    // Pending params from audio thread (debounced)
    Params pendingParams;
    bool paramsDirty = false;
    int debounceCountdown = 0;
    static constexpr int kDebounceBlocks = 24; // ~500ms at 48kHz/480 samples per block

    // Background AEC3 creation — avoids allocation on the audio thread
    std::thread aec3Worker;
    std::mutex aec3Mutex;
    std::condition_variable aec3Cv;
    bool aec3WorkerExit = false;
    bool aec3WorkerHasWork = false;
    Params aec3WorkerParams;

    // Staged AEC3 — written by worker thread, swapped in by audio thread after aec3Ready
    AEC3Instance stagedInstance;
    std::atomic<bool> aec3Ready { false };

    void aec3WorkerLoop();
    void startWorker();
    void stopWorker();

    // FFT for far-end spectrum display
    static constexpr int kFFTOrder = 11;  // 2048-point FFT (~23Hz per bin at 48kHz)
    static constexpr int kFFTSize = 1 << kFFTOrder; // 2048
    juce::dsp::FFT fft { kFFTOrder };
    juce::dsp::WindowingFunction<float> fftWindow { kFFTSize, juce::dsp::WindowingFunction<float>::hann };
    float fftData[kFFTSize * 2] = {};
    int fftFillIndex = 0;

public:
    // Spectrum display bands — written by audio thread, read by UI timer
    static constexpr int kSpectrumBands = 16;
    std::atomic<float> displaySpectrumBands[kSpectrumBands] = {};

    // Peak levels (dBFS) — written by audio thread, read by UI timer
    std::atomic<float> peakInDb  { -100.0f };  // mic input
    std::atomic<float> peakOutDb { -100.0f };  // AEC output
    std::atomic<float> peakRefDb { -100.0f };  // far-end reference

private:
    void updateSpectrum (const float* farFrame, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoCancellerEngine)
};
