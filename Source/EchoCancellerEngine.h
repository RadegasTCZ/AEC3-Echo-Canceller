#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <cmath>
#include <utility>
#include <atomic>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "LoopbackSource.h"
#include "DelayLine.h"
#include "PeakHold.h"
#include "Resampler.h"
#include "AgcStage.h"
#include "OutputLimiter.h"

// Forward declarations for WebRTC AEC3
namespace webrtc {
    class EchoControl;
    class AudioBuffer;
    class HighPassFilter;
    class NoiseSuppressor;
}

/** Mono echo canceller (WebRTC AEC3) followed by the WebRTC noise suppressor.

    Host audio is resampled to 48 kHz (SpeexDSP), processed in 480-sample frames and resampled back
    into a host-rate output FIFO primed with P zeros. A host-rate pad delay line tops the natural
    delay up to a constant getLatencySamples() in every mode: 30 ms at 16 kHz and above, the natural
    delay below 16 kHz (where it is longer than 30 ms). */
class EchoCancellerEngine
{
public:
    // All tunable AEC3 parameters, read from APVTS atomics each process block.
    // The 14 tuning defaults are the Normal strength row (aec::tuningFor (Strength::Normal)).
    struct Params
    {
        // Normal tuning
        float lfSuppression   = 75.0f;   // 0-100%, step 5
        float lfTransparency  = 80.0f;
        float hfSuppression   = 80.0f;
        float hfTransparency  = 85.0f;
        float lfAttack        = 35.0f;
        float lfDecay         = 25.0f;
        float hfDucking       = 0.0f;
        float hfEchoThreshold = 55.0f;

        // Double-talk (nearend) tuning
        float dtLfSuppression  = 25.0f;
        float dtLfTransparency = 30.0f;
        float dtHfSuppression  = 40.0f;
        float dtHfTransparency = 80.0f;
        float dtLfAttack       = 35.0f;
        float dtLfDecay        = 25.0f;

        // System
        float roomReverb       = 13.0f;   // 13-40 blocks (AEC3 minimum is 13)
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

        /** Every non-finite value (NaN host automation or state) replaced by its default. */
        Params sanitised() const
        {
            const Params d;
            Params r = *this;
            for (auto [v, def] : { std::pair<float*, float> { &r.lfSuppression, d.lfSuppression },
                                   { &r.lfTransparency, d.lfTransparency }, { &r.hfSuppression, d.hfSuppression },
                                   { &r.hfTransparency, d.hfTransparency }, { &r.lfAttack, d.lfAttack },
                                   { &r.lfDecay, d.lfDecay }, { &r.hfDucking, d.hfDucking },
                                   { &r.hfEchoThreshold, d.hfEchoThreshold }, { &r.dtLfSuppression, d.dtLfSuppression },
                                   { &r.dtLfTransparency, d.dtLfTransparency }, { &r.dtHfSuppression, d.dtHfSuppression },
                                   { &r.dtHfTransparency, d.dtHfTransparency }, { &r.dtLfAttack, d.dtLfAttack },
                                   { &r.dtLfDecay, d.dtLfDecay }, { &r.roomReverb, d.roomReverb } })
                if (! std::isfinite (*v))
                    *v = def;
            return r;
        }
    };

    static constexpr int kAecRate = 48000;
    static constexpr int kAecFrameSize = 480; // 10ms at 48000

    /** Band split + merge (three-band filter bank), measured. */
    static constexpr int kSplitDelay48k = 46;
    /** AEC3 ProcessCapture block framing (64-sample blocks per band), measured. */
    static constexpr int kAecDelay48k = 384;
    /** Noise suppressor overlap (96 samples per 16 kHz band), measured. */
    static constexpr int kNsDelay48k = 288;
    /** Firm reported latency at every host rate and in every mode. */
    static constexpr double kLatencySeconds = 0.030;

    static constexpr int naturalLatency48k() { return kAecFrameSize + kSplitDelay48k + kAecDelay48k + kNsDelay48k; }

    EchoCancellerEngine();
    ~EchoCancellerEngine();

    /** loopbackRate: the reference's current rate if known (> 0), so its resampler is built here and
        not on the audio thread. A later rate change is rebuilt by the worker. */
    void prepare (double sampleRate, int maxBlockSize, int loopbackRate = 0);

    /** Rate the loopback resampler currently converts from (48000 = passthrough, 0 = none: its build
        failed, the reference is silence and the build is retried about once a second). Audio thread / tests. */
    int getLoopbackResamplerRate() const { return loopbackResamplerRate; }

    /** Loopback resampler rebuild jobs posted since prepare(). Audio thread / tests. */
    int getLoopbackJobCount() const { return loopbackJobCount; }
    void release();

    void process (const float* micIn, float* out, int numSamples,
                  aec::LoopbackSource& loopback);

    /** roundToInt (kLatencySeconds * host rate) from 16 kHz up. Below 16 kHz the resamplers' own delay
        exceeds 30 ms, so the latency is the natural delay instead (never a negative pad). 0 before
        prepare() and at an unusable rate. */
    int getLatencySamples() const { return latencySamples; }

    /** Host samples after a re-prime (enable transition, gap) until the output is fully the processed
        signal again: one latency muted, then the reset fade-in. */
    int getPrimingSamples() const { return getLatencySamples() + resetFadeLength; }

    void setEnabled (bool shouldBeEnabled) { enabled.store (shouldBeEnabled, std::memory_order_release); }
    bool isEnabled() const { return enabled.load (std::memory_order_acquire); }

    void setMonitorFarEnd (bool on) { monitorFarEnd.store (on, std::memory_order_release); }
    bool getMonitorFarEnd() const { return monitorFarEnd.load (std::memory_order_acquire); }

    /** "Hear removed": output the delay-aligned input-to-NS minus the NS output. */
    void setMonitorNoiseResidual (bool on) { monitorResidual.store (on, std::memory_order_release); }

    // Latency compensation — fed to AEC3 per-frame via SetAudioBufferDelay, no recreation needed.
    // A non-finite value (NaN automation) is taken as 0.
    void setLatencyCompMs (float ms) { latencyCompMs.store (std::isfinite (ms) ? ms : 0.0f, std::memory_order_relaxed); }

    // Called from the audio thread each block with current APVTS values (sanitised: non-finite -> default)
    void setParams (const Params& p);

    /** nsLevel choice index: 0 = Off, 1..4 = 6/12/18/21 dB. Called from the audio thread each block;
        a change is debounced, then the worker builds the new suppressor (AEC3 is not recreated).
        A value set before prepare() is used directly by prepare(). */
    void setNoiseLevel (int choiceIndex);

    /** Auto gain (AGC2 adaptive digital) on the voice path, after NS. Called from the audio thread each
        block; the settings are sanitised and a change is debounced, then the worker builds the new stage
        (nullptr when disabled; AEC3 is not recreated). A value set before prepare() is built by prepare(). */
    void setAgc (const aec::AgcSettings& settings);

    /** Output limiter (-1 dBFS ceiling) after the monitor selection. Any thread; no worker involved. */
    void setLimiterEnabled (bool on) { limiterEnabled.store (on, std::memory_order_release); }

    /** Test hook: >= 0 replaces the AGC's voice activity probability, < 0 runs the real VAD. */
    void setAgcSpeechProbabilityForTesting (float p) { agcSpeechProbability.store (p, std::memory_order_relaxed); }

    /** AGC stages swapped in from the worker since prepare(). Audio thread / tests. */
    int getAgcSwapCount() const { return agcSwapCount; }

    /** Factory reset, message thread. Clears the AEC3 error (restarting the worker if it stopped on
        it) and has the worker build a fresh AEC3 instance and suppressor from the current values,
        swapped in by the audio thread as on a parameter change: no allocation or join on the audio
        thread, latency and audio continuous. */
    void requestReset();

    bool hasError() const { return aec3Error.load (std::memory_order_acquire); }

    /** False while process() cannot run AEC3 (not prepared / unusable rate): it then passes the mic through
        undelayed. Audio thread only. */
    bool isReady() const { return echoController != nullptr; }

    /** AEC3 instances swapped in from the worker since prepare(). Audio thread / tests. */
    int getAecSwapCount() const { return aecSwapCount; }

    /** Test hook: raises or clears the AEC3 error halt state. */
    void setErrorForTesting (bool on) { aec3Error.store (on, std::memory_order_release); }

    /** Times the worker has checked an occupied hand-over slot while waiting for the audio thread. Tests. */
    int getWorkerPollCount() const { return workerPollCount.load (std::memory_order_relaxed); }

    /** True while a built AEC3 instance waits in the hand-over slot for the audio thread. Tests. */
    bool isAecInstanceStagedForTesting() const { return aec3Ready.load (std::memory_order_acquire); }

    /** Test hook: the worker's next job throws, as a failed AEC3 / suppressor creation does. */
    void failNextJobForTesting() { failNextJob.store (true, std::memory_order_release); }

    // Meters — pushed by the audio thread, taken by the UI timer
    aec::PeakHold inPeak, refPeak, outPeak;
    aec::ReductionHold limiterReduction;   // output limiter gain reduction (dB), pushed while the limiter is on

    // Spectrum display bands — written by audio thread, read by UI timer
    static constexpr int kSpectrumBands = 16;
    std::atomic<float> displaySpectrumBands[kSpectrumBands] = {};

private:
    // An AEC3 controller with its own band buffers. prepare() keeps the buffers as the engine's
    // render/capture buffers; for a swapped-in instance they are scratch for its pre-roll and
    // crossfade frame and go back to the worker with the retired controller.
    struct AEC3Instance
    {
        std::unique_ptr<webrtc::EchoControl> controller;
        std::unique_ptr<webrtc::AudioBuffer> render;
        std::unique_ptr<webrtc::AudioBuffer> capture;
    };

    AEC3Instance createAEC3Instance (const Params& p);
    static std::unique_ptr<webrtc::NoiseSuppressor> createNoiseSuppressor (int choiceIndex);

    static constexpr int kMaxLoopbackRead = 8192;
    static constexpr int kNumBands = 3;
    static constexpr int kBandSize = kAecFrameSize / kNumBands;   // 160 at 16 kHz
    static constexpr int kNumNsLevels = 5;                         // Off + 4 levels

    // AEC3 state
    std::unique_ptr<webrtc::EchoControl> echoController;
    std::unique_ptr<webrtc::AudioBuffer> renderAudio;
    std::unique_ptr<webrtc::AudioBuffer> captureAudio;
    std::unique_ptr<webrtc::HighPassFilter> hpFilter;

    // Noise stage (audio thread). nullptr = Off.
    std::unique_ptr<webrtc::NoiseSuppressor> noiseSuppressor;
    std::unique_ptr<webrtc::NoiseSuppressor> incomingSuppressor;   // only during a level-change frame
    std::unique_ptr<webrtc::AudioBuffer> preNsAudio;               // post-AEC3 bands, merged for bypass/residual
    std::unique_ptr<webrtc::AudioBuffer> nsScratchAudio;           // pre-roll and incoming suppressor bands
    float analyzeHistory[kNumBands][kBandSize] = {};               // last frame's bands before ProcessCapture
    float processHistory[kNumBands][kBandSize] = {};               // last frame's bands after ProcessCapture
    float renderHistory[kNumBands][kBandSize] = {};                // last frame's render bands
    float prevNearFrame[kAecFrameSize] = {};                       // last frame's raw near end (AnalyzeCapture)

    // Level stage (audio thread). agcStage nullptr = AGC off; the limiter always runs (its state stays warm).
    std::unique_ptr<aec::AgcStage> agcStage;
    std::unique_ptr<aec::AgcStage> incomingAgc;                    // only during an AGC change frame
    std::unique_ptr<aec::OutputLimiter> outputLimiter;
    bool limiterOn = true;          // limiter state applied to the last frame (audio thread)
    bool clampHostOutput = true;    // limiter on for the whole last frame: clamp its host-rate samples

    int hostSampleRate = 0;
    int hostBlockSize = 0;
    int outputPrefill = 0;      // P
    int muteRemaining = 0;      // host samples still muted after a FIFO reset (audio thread)
    int resetFadeLength = 1;    // fade-in after the mute, host samples
    int resetFadePos = 1;       // == resetFadeLength: no fade running
    static constexpr double kResetFadeSeconds = 0.010;
    int naturalLatency = 0;     // N, host samples
    int latencySamples = 0;     // reported: max (30 ms, N); 0 = not prepared / unusable rate

    // Current applied params (for dirty check)
    Params currentParams;

    // FIFOs: near/far at kAecRate, output at host rate
    juce::AbstractFifo nearEndFifo { 1 };
    std::vector<float> nearEndBuffer;

    juce::AbstractFifo farEndFifo { 1 };
    std::vector<float> farEndBuffer;

    juce::AbstractFifo outputFifo { 1 };
    std::vector<float> outputBuffer;

    void resetFifos();
    static void fifoWrite (juce::AbstractFifo& fifo, std::vector<float>& buffer, const float* src, int n);
    static void fifoRead (juce::AbstractFifo& fifo, const std::vector<float>& buffer, float* dst, int n);

    // Frame buffers (kAecFrameSize each)
    std::vector<float> tempNearFrame, tempFarFrame, farDelayedFrame;
    std::vector<float> nsOutFrame, preNsFrame, bypassFrame, mainFrame, residualFrame, outFrame48;
    std::vector<float> agcFrame, agcScratchFrame, limitedFrame;
    float fadeIn[kAecFrameSize] = {}, fadeOut[kAecFrameSize] = {};
    float bandFadeIn[kBandSize] = {}, bandFadeOut[kBandSize] = {};

    // Delay lines: 48 kHz bypass (NS Off) and monitor reference, host-rate pad
    aec::DelayLine nsCompDelay;
    aec::DelayLine farMonitorDelay;
    aec::DelayLine padDelay;

    // Loopback temp buffers
    std::vector<int16_t> tempLoopbackRaw;
    std::vector<float> tempLoopbackFloat;
    std::vector<float> tempLoopbackResampled;

    // Host rate resampling temp buffers
    std::vector<float> tempMic48;
    std::vector<float> tempHostOut;

    // Resamplers (SpeexDSP quality 5)
    aec::Resampler inputResampler;    // host -> 48k
    aec::Resampler outputResampler;   // 48k -> host
    // loopback -> 48k. Built in prepare() or by the worker, never on the audio thread; while the
    // loopback rate differs from loopbackResamplerRate the reference is fed as silence.
    std::unique_ptr<aec::Resampler> loopbackResampler;   // nullptr while loopbackResamplerRate == 0
    int loopbackResamplerRate = kAecRate;   // 0 = build failed
    int requestedLoopbackRate = kAecRate;   // last rate sent to the worker
    int loopbackRetryCountdown = 0;         // host samples until a failed build is retried
    int loopbackJobCount = 0;
    int aecSwapCount = 0;
    int agcSwapCount = 0;

    std::atomic<bool> enabled { true };
    bool wasEnabled = false; // transition detection — audio thread only
    std::atomic<bool> monitorFarEnd { false };
    std::atomic<bool> monitorResidual { false };
    std::atomic<float> latencyCompMs { 0.0f };
    std::atomic<bool> limiterEnabled { true };
    std::atomic<float> agcSpeechProbability { -1.0f };

    // processBlock gap detection — audio thread only
    int64_t lastProcessTicks = 0;
    static constexpr double kGapThresholdMs = 100.0;

    // Factory reset — signalled by the message thread; the audio thread posts the rebuild jobs
    std::atomic<bool> resetRequested { false };

    // AEC3 / suppressor creation error — set by worker thread on exception, cleared by factory reset
    std::atomic<bool> aec3Error { false };

    // Pending values from audio thread (debounced)
    Params pendingParams;
    bool paramsDirty = false;
    int debounceCountdown = 0;
    static constexpr int kDebounceBlocks = 24; // ~500ms at 48kHz/480 samples per block

    int pendingNsLevel = 2;     // last value given to setNoiseLevel (12 dB default)
    int requestedNsLevel = 2;   // last value sent to the worker
    bool nsLevelDirty = false;
    int nsDebounceCountdown = 0;

    aec::AgcSettings pendingAgc;     // last value given to setAgc (sanitised)
    aec::AgcSettings requestedAgc;   // last value sent to the worker
    bool agcDirty = false;
    int agcDebounceCountdown = 0;

    // Background worker: one thread, "latest wins" job slots (AEC3 params, NS level, AGC, loopback rate)
    std::thread worker;
    std::mutex workerMutex;
    void releaseLocked();   // release() body; lifecycleMutex held
    std::mutex lifecycleMutex;   // serialises requestReset / prepare / release (message / host threads, never the audio thread)
    std::condition_variable workerCv;
    std::atomic<bool> workerExit { false };
    std::atomic<bool> workerFailed { false };   // the worker stopped on a creation error (thread finished)
    std::atomic<bool> failNextJob { false };    // test hook
    std::atomic<int> workerPollCount { 0 };
    bool aecJobPending = false;
    Params aecJobParams;
    bool nsJobPending = false;
    int nsJobLevel = 0;
    bool agcJobPending = false;
    aec::AgcSettings agcJobSettings;
    bool loopbackJobPending = false;
    int loopbackJobRate = 0;

    // Hand-over slots. The worker writes a slot only while its flag is false, the audio thread only
    // while it is true. The audio thread swaps the retired object back in; the worker frees it.
    AEC3Instance stagedInstance;
    std::atomic<bool> aec3Ready { false };
    std::unique_ptr<webrtc::NoiseSuppressor> stagedSuppressor;
    std::atomic<bool> nsReady { false };
    std::unique_ptr<aec::AgcStage> stagedAgc;
    std::atomic<bool> agcReady { false };
    // Gain (dB) the running AGC stage applied to the last frame, -1 while AGC is off. Audio thread writes
    // each frame; the worker reads it to start an on -> on replacement stage at that gain (no re-learn surge).
    std::atomic<float> runningAgcGainDb { -1.0f };
    std::unique_ptr<aec::Resampler> stagedLoopbackResampler;
    int stagedLoopbackRate = 0;
    std::atomic<bool> loopbackReady { false };

    void postAecJob (const Params& p);
    void postNsJob (int level);
    void postAgcJob (const aec::AgcSettings& s);
    /** nullptr when s is off. initialGainDb as AgcStage's (< 0: the default start gain). */
    static std::unique_ptr<aec::AgcStage> createAgcStage (const aec::AgcSettings& s, float initialGainDb = -1.0f);
    /** Same effective AGC: equal settings, or both disabled (the other values then do not matter). */
    static bool sameAgc (const aec::AgcSettings& a, const aec::AgcSettings& b) { return a == b || (! a.enabled && ! b.enabled); }
    void processAgc (const float* mainOut);   // mainOut -> agcFrame (AGC stage, change crossfade)
    void applyLimiter();                      // outFrame48 in place (limited / unlimited, switch crossfade)
    void postLoopbackJob (int rate);
    static std::unique_ptr<aec::Resampler> createLoopbackResampler (int rate);
    void workerLoop();
    bool waitUntilConsumed (const std::atomic<bool>& ready);   // false: exit requested
    void startWorker();
    void stopWorker();

    void readLoopback (aec::LoopbackSource& loopback, int numSamples);
    void processChunk (const float* micIn, float* out, int n);
    void processFrame();   // tempNearFrame + tempFarFrame -> outFrame48 (main, residual or reference)
    void preRollAec (AEC3Instance& inst, int delayMs);   // feeds inst the previous frame, output discarded

    // FFT for far-end spectrum display
    static constexpr int kFFTOrder = 11;  // 2048-point FFT (~23Hz per bin at 48kHz)
    static constexpr int kFFTSize = 1 << kFFTOrder; // 2048
    juce::dsp::FFT fft { kFFTOrder };
    juce::dsp::WindowingFunction<float> fftWindow { kFFTSize, juce::dsp::WindowingFunction<float>::hann };
    float fftData[kFFTSize * 2] = {};
    int fftFillIndex = 0;

    void updateSpectrum (const float* farFrame, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoCancellerEngine)
};
