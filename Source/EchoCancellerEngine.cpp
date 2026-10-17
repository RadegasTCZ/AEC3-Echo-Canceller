#include "EchoCancellerEngine.h"
#include "AecMapping.h"

#include "api/audio/echo_canceller3_factory.h"
#include "api/audio/echo_canceller3_config.h"
#include "api/audio/audio_processing.h"
#include "api/environment/environment_factory.h"
#include "modules/audio_processing/audio_buffer.h"
#include "modules/audio_processing/high_pass_filter.h"
#include "modules/audio_processing/ns/noise_suppressor.h"
#include "modules/audio_processing/ns/ns_config.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>

// Link AEC3 static library (MSVC) — separate libs for Release/Debug CRT
#if defined(_MSC_VER)
  #if defined(_DEBUG)
    #pragma comment(lib, "AEC3_debug.lib")
  #else
    #pragma comment(lib, "AEC3.lib")
  #endif
#endif

using namespace webrtc;

namespace
{
    constexpr int kSplitBands = 3;

    /** Copies the split bands of `from` into a [3][160] array (and back). */
    void bandsToArray (AudioBuffer& from, float (*dst)[160])
    {
        auto* const* bands = from.split_bands (0);
        for (int b = 0; b < kSplitBands; ++b)
            std::memcpy (dst[b], bands[b], sizeof (float) * 160);
    }

    void arrayToBands (const float (*src)[160], AudioBuffer& to)
    {
        auto* const* bands = to.split_bands (0);
        for (int b = 0; b < kSplitBands; ++b)
            std::memcpy (bands[b], src[b], sizeof (float) * 160);
    }

    void copyBands (AudioBuffer& from, AudioBuffer& to)
    {
        auto* const* src = from.split_bands (0);
        auto* const* dst = to.split_bands (0);
        for (int b = 0; b < kSplitBands; ++b)
            std::memcpy (dst[b], src[b], sizeof (float) * 160);
    }
}

// ============================================================================
// Engine lifecycle
// ============================================================================

EchoCancellerEngine::EchoCancellerEngine()
{
    static_assert (kBandSize == 160 && kNumBands == kSplitBands, "48 kHz frame = 3 bands of 160");

    // Equal-power crossfade, t = (i + 0.5) / length, as Noise Suppression v2
    for (int i = 0; i < kAecFrameSize; ++i)
    {
        const double t = juce::MathConstants<double>::halfPi * ((double) i + 0.5) / (double) kAecFrameSize;
        fadeOut[i] = (float) std::cos (t);
        fadeIn[i]  = (float) std::sin (t);
    }

    for (int i = 0; i < kBandSize; ++i)
    {
        const double t = juce::MathConstants<double>::halfPi * ((double) i + 0.5) / (double) kBandSize;
        bandFadeOut[i] = (float) std::cos (t);
        bandFadeIn[i]  = (float) std::sin (t);
    }
}

EchoCancellerEngine::~EchoCancellerEngine()
{
    release();
}

void EchoCancellerEngine::setParams (const Params& params)
{
    // NaN never reaches AEC3, and a NaN (never equal to itself) cannot re-arm the debounce every block
    const Params p = params.sanitised();

    if (p != pendingParams)
    {
        pendingParams = p;
        paramsDirty = true;
        debounceCountdown = kDebounceBlocks;
    }
}

void EchoCancellerEngine::setNoiseLevel (int choiceIndex)
{
    const int level = juce::jlimit (0, kNumNsLevels - 1, choiceIndex);

    if (level != pendingNsLevel)
    {
        pendingNsLevel = level;
        nsLevelDirty = true;
        nsDebounceCountdown = kDebounceBlocks;
    }
}

EchoCancellerEngine::AEC3Instance EchoCancellerEngine::createAEC3Instance (const Params& p)
{
    EchoCanceller3Config config;

    // Map slider values to AEC3 thresholds, clamping to ensure
    // enr_transparent < enr_suppress (strict, AEC3 asserts this in debug)
    auto ensureStrictLT = [] (float& transp, float& supp)
    {
        constexpr float kMinGap = 0.001f;
        if (transp >= supp)
            transp = supp - kMinGap;
        if (transp < 0.0f)
            transp = 0.0f;
    };

    float lfTransp  = aec::mapping::mapTransparency (p.lfTransparency);
    float lfSupp    = aec::mapping::mapSuppression (p.lfSuppression);
    float hfTransp  = aec::mapping::mapHfTransparency (p.hfTransparency);
    float hfSupp    = aec::mapping::mapHfSuppression (p.hfSuppression);
    ensureStrictLT (lfTransp, lfSupp);
    ensureStrictLT (hfTransp, hfSupp);

    float dtLfTransp = aec::mapping::mapTransparency (p.dtLfTransparency);
    float dtLfSupp   = aec::mapping::mapSuppression (p.dtLfSuppression);
    float dtHfTransp = aec::mapping::mapHfTransparency (p.dtHfTransparency);
    float dtHfSupp   = aec::mapping::mapHfSuppression (p.dtHfSuppression);
    ensureStrictLT (dtLfTransp, dtLfSupp);
    ensureStrictLT (dtHfTransp, dtHfSupp);

    // Normal tuning
    config.suppressor.normal_tuning.mask_lf =
        EchoCanceller3Config::Suppressor::MaskingThresholds (
            lfTransp, lfSupp, 0.3f);
    config.suppressor.normal_tuning.mask_hf =
        EchoCanceller3Config::Suppressor::MaskingThresholds (
            hfTransp, hfSupp, 0.3f);
    config.suppressor.normal_tuning.max_inc_factor = aec::mapping::mapAttack (p.lfAttack);
    config.suppressor.normal_tuning.max_dec_factor_lf = aec::mapping::mapDecay (p.lfDecay);

    // Nearend (double-talk) tuning
    config.suppressor.nearend_tuning.mask_lf =
        EchoCanceller3Config::Suppressor::MaskingThresholds (
            dtLfTransp, dtLfSupp, 0.3f);
    config.suppressor.nearend_tuning.mask_hf =
        EchoCanceller3Config::Suppressor::MaskingThresholds (
            dtHfTransp, dtHfSupp, 0.3f);
    config.suppressor.nearend_tuning.max_inc_factor = aec::mapping::mapAttack (p.dtLfAttack);
    config.suppressor.nearend_tuning.max_dec_factor_lf = aec::mapping::mapDecay (p.dtLfDecay);

    // High-bands suppression
    config.suppressor.high_bands_suppression.max_gain_during_echo =
        aec::mapping::mapHfDucking (p.hfDucking);
    config.suppressor.high_bands_suppression.enr_threshold =
        aec::mapping::mapHfEchoThreshold (p.hfEchoThreshold);

    // System params
    const float room = std::isfinite (p.roomReverb) ? p.roomReverb : 13.0f;   // a NaN cast is undefined
    auto filterLen = static_cast<size_t> (juce::jlimit (13.0f, 40.0f, room));
    config.filter.refined.length_blocks = filterLen;
    config.filter.coarse.length_blocks  = filterLen;
    config.ep_strength.bounded_erl = p.boundedErl;
    config.echo_removal_control.has_clock_drift = p.clockDrift;

    AEC3Instance inst;

    auto env = webrtc::CreateEnvironment();
    EchoCanceller3Factory factory (config);
    inst.controller = factory.Create (env, kAecRate, 1, 1);

    inst.render = std::make_unique<AudioBuffer> (
        kAecRate, 1, kAecRate, 1, kAecRate, 1);
    inst.capture = std::make_unique<AudioBuffer> (
        kAecRate, 1, kAecRate, 1, kAecRate, 1);

    return inst;
}

std::unique_ptr<NoiseSuppressor> EchoCancellerEngine::createNoiseSuppressor (int choiceIndex)
{
    if (choiceIndex <= 0)
        return nullptr;   // Off

    NsConfig config;
    config.target_level = static_cast<NsConfig::SuppressionLevel> (juce::jlimit (0, 3, choiceIndex - 1));   // k6dB..k21dB
    return std::make_unique<NoiseSuppressor> (config, (size_t) kAecRate, (size_t) 1);
}

std::unique_ptr<aec::Resampler> EchoCancellerEngine::createLoopbackResampler (int rate)
{
    auto r = std::make_unique<aec::Resampler>();
    if (! r->prepare (rate, kAecRate))
        return nullptr;   // speex refused the rate: no resampler, the reference stays silent
    return r;
}

// ============================================================================
// Background worker
// ============================================================================

void EchoCancellerEngine::postAecJob (const Params& p)
{
    {
        std::lock_guard<std::mutex> lock (workerMutex);   // brief lock, sub-microsecond
        aecJobParams = p;
        aecJobPending = true;
    }
    workerCv.notify_one();
}

void EchoCancellerEngine::postLoopbackJob (int rate)
{
    {
        std::lock_guard<std::mutex> lock (workerMutex);
        loopbackJobRate = rate;
        loopbackJobPending = true;
    }
    workerCv.notify_one();
}

void EchoCancellerEngine::postNsJob (int level)
{
    {
        std::lock_guard<std::mutex> lock (workerMutex);
        nsJobLevel = level;
        nsJobPending = true;
    }
    workerCv.notify_one();
}

bool EchoCancellerEngine::waitUntilConsumed (const std::atomic<bool>& ready)
{
    // A running audio thread consumes a staged object within one block; a disabled or halted one may
    // not for a long time. The audio thread never signals (it must not lock), so re-check the flag
    // with a back-off from 1 ms to 100 ms; stopWorker() notifies the condition variable, so an exit
    // request is still seen at once.
    auto timeout = std::chrono::milliseconds (1);
    std::unique_lock<std::mutex> lock (workerMutex);

    while (ready.load (std::memory_order_acquire))
    {
        if (workerExit.load (std::memory_order_acquire))
            return false;
        workerPollCount.fetch_add (1, std::memory_order_relaxed);
        workerCv.wait_for (lock, timeout);
        timeout = std::min (timeout * 2, std::chrono::milliseconds (100));
    }
    return ! workerExit.load (std::memory_order_acquire);
}

void EchoCancellerEngine::workerLoop()
{
    while (true)
    {
        bool doAec = false, doNs = false, doLoopback = false;
        Params p;
        int level = 0, rate = 0;
        {
            std::unique_lock<std::mutex> lock (workerMutex);
            workerCv.wait (lock, [this] { return aecJobPending || nsJobPending || loopbackJobPending || workerExit.load(); });

            if (workerExit.load())
                return;

            if (aecJobPending)
            {
                doAec = true;
                p = aecJobParams;
                aecJobPending = false;
            }
            else if (nsJobPending)
            {
                doNs = true;
                level = nsJobLevel;
                nsJobPending = false;
            }
            else
            {
                doLoopback = true;
                rate = loopbackJobRate;
                loopbackJobPending = false;
            }
        }

        try
        {
            if (failNextJob.exchange (false, std::memory_order_acq_rel))
                throw std::runtime_error ("test: creation failed");

            if (doAec)
            {
                auto inst = createAEC3Instance (p);

                if (! waitUntilConsumed (aec3Ready))
                    return;

                stagedInstance = std::move (inst);   // frees the instance the audio thread retired
                aec3Ready.store (true, std::memory_order_release);
            }

            if (doNs)
            {
                auto ns = createNoiseSuppressor (level);

                if (! waitUntilConsumed (nsReady))
                    return;

                stagedSuppressor = std::move (ns);    // frees the suppressor the audio thread retired
                nsReady.store (true, std::memory_order_release);
            }

            if (doLoopback)
            {
                auto r = createLoopbackResampler (rate);

                if (! waitUntilConsumed (loopbackReady))
                    return;

                stagedLoopbackRate = r != nullptr ? rate : 0;   // 0: the build failed
                stagedLoopbackResampler = std::move (r);       // frees the resampler the audio thread retired
                loopbackReady.store (true, std::memory_order_release);
            }
        }
        catch (...)
        {
            // Creation failed — halt engine, user must factory reset to recover (requestReset restarts the worker)
            workerFailed.store (true, std::memory_order_release);
            aec3Error.store (true, std::memory_order_release);
            return;
        }
    }
}

void EchoCancellerEngine::startWorker()
{
    workerExit.store (false);
    workerFailed.store (false);
    aecJobPending = false;
    nsJobPending = false;
    loopbackJobPending = false;
    loopbackReady.store (false, std::memory_order_relaxed);
    aec3Ready.store (false, std::memory_order_relaxed);
    nsReady.store (false, std::memory_order_relaxed);
    worker = std::thread (&EchoCancellerEngine::workerLoop, this);
}

void EchoCancellerEngine::stopWorker()
{
    if (worker.joinable())
    {
        {
            std::lock_guard<std::mutex> lock (workerMutex);
            workerExit.store (true);
        }
        workerCv.notify_one();
        worker.join();
    }
}

void EchoCancellerEngine::requestReset()
{
    // Serialised with prepare() / release(), which also join and restart the worker thread.
    std::lock_guard<std::mutex> lifecycleLock (lifecycleMutex);

    // A worker that stopped on a creation error has finished (the join returns at once). Its slots
    // are left as they are: a staged object still belongs to the audio thread until it is consumed.
    if (workerFailed.load (std::memory_order_acquire) && worker.joinable())
    {
        worker.join();
        workerFailed.store (false, std::memory_order_release);
        worker = std::thread (&EchoCancellerEngine::workerLoop, this);
    }

    aec3Error.store (false, std::memory_order_release);   // the running instance is still valid
    resetRequested.store (true, std::memory_order_release);
}

// ============================================================================
// prepare / release
// ============================================================================

void EchoCancellerEngine::prepare (double sampleRate, int maxBlockSize, int loopbackRate)
{
    std::lock_guard<std::mutex> lifecycleLock (lifecycleMutex);
    releaseLocked();

    // A reset requested while unprepared has nothing to act on: the fresh instances below are the reset.
    resetRequested.store (false, std::memory_order_release);

    // The rate is only taken once the resamplers exist: an unusable rate reports latency 0, matching
    // the undelayed pass-through process() then gives.
    const int rate = juce::roundToInt (sampleRate);
    hostSampleRate = 0;
    latencySamples = 0;
    hostBlockSize = juce::jmax (1, maxBlockSize);

    if (! inputResampler.prepare (rate, kAecRate) || ! outputResampler.prepare (kAecRate, rate))
    {
        jassertfalse;   // unusable rate: echoController stays null and process() passes audio through
        return;
    }

    hostSampleRate = rate;

    // Initial AEC3 and suppressor synchronously (prepare is not real-time), from the latest values
    // A creation failure (e.g. out of memory) must not escape prepareToPlay into the host: the engine
    // stays not ready, so process() passes the mic through and the processor uses its dry path.
    currentParams = pendingParams;
    requestedNsLevel = pendingNsLevel;
    aec3Error.store (false, std::memory_order_release);   // fresh instances below: a previous fault no longer applies
    try
    {
        auto inst = createAEC3Instance (currentParams);
        echoController = std::move (inst.controller);
        renderAudio = std::move (inst.render);
        captureAudio = std::move (inst.capture);
        hpFilter = std::make_unique<HighPassFilter> (kAecRate, 1);   // parameter-free: kept across AEC3 swaps

        noiseSuppressor = createNoiseSuppressor (requestedNsLevel);
        preNsAudio = std::make_unique<AudioBuffer> (kAecRate, 1, kAecRate, 1, kAecRate, 1);
        nsScratchAudio = std::make_unique<AudioBuffer> (kAecRate, 1, kAecRate, 1, kAecRate, 1);
    }
    catch (...)
    {
        jassertfalse;
        releaseLocked();
        aec3Error.store (true, std::memory_order_release);
        return;
    }
    std::memset (analyzeHistory, 0, sizeof (analyzeHistory));
    std::memset (processHistory, 0, sizeof (processHistory));
    std::memset (renderHistory, 0, sizeof (renderHistory));
    std::memset (prevNearFrame, 0, sizeof (prevNearFrame));

    // Latency model (Noise Suppression v2): host output FIFO primed with P zeros, pad to the firm total
    const double ratio = (double) hostSampleRate / (double) kAecRate;
    outputPrefill = inputResampler.isPassthrough()
                      ? kAecFrameSize
                      : (int) std::ceil ((double) kAecFrameSize * ratio) + 1;
    naturalLatency = inputResampler.inputLatency() + outputPrefill + outputResampler.outputLatency()
                   + juce::roundToInt ((double) (kSplitDelay48k + kAecDelay48k + kNsDelay48k) * ratio);
    // 30 ms from 16 kHz up; below that the resamplers alone exceed it, so report the natural delay
    latencySamples = juce::jmax (juce::roundToInt (kLatencySeconds * (double) hostSampleRate), naturalLatency);
    const int pad = latencySamples - naturalLatency;
    jassert (pad >= 0);

    resetFadeLength = juce::jmax (1, juce::roundToInt (kResetFadeSeconds * (double) hostSampleRate));

    padDelay.prepare (juce::jmax (1, pad));
    padDelay.setDelay (juce::jmax (0, pad));
    nsCompDelay.prepare (kNsDelay48k);
    nsCompDelay.setDelay (kNsDelay48k);
    farMonitorDelay.prepare (kSplitDelay48k + kAecDelay48k + kNsDelay48k);
    farMonitorDelay.setDelay (kSplitDelay48k + kAecDelay48k + kNsDelay48k);

    // Buffers
    const int max48 = aec::Resampler::maxOutputFor (hostBlockSize, hostSampleRate, kAecRate);
    const int maxHostPerFrame = aec::Resampler::maxOutputFor (kAecFrameSize, kAecRate, hostSampleRate);
    tempMic48.assign ((size_t) max48, 0.0f);
    tempHostOut.assign ((size_t) maxHostPerFrame, 0.0f);

    auto sizeFifo = [] (juce::AbstractFifo& fifo, std::vector<float>& buffer, int capacity)
    {
        fifo.setTotalSize (capacity + 1);   // AbstractFifo holds size - 1
        buffer.assign ((size_t) capacity + 1, 0.0f);
    };

    sizeFifo (nearEndFifo, nearEndBuffer, max48 + kAecFrameSize);
    sizeFifo (farEndFifo, farEndBuffer, juce::jmax (16384, max48 * 8));
    const int maxFramesPerChunk = (max48 + kAecFrameSize) / kAecFrameSize + 1;
    sizeFifo (outputFifo, outputBuffer, outputPrefill + maxFramesPerChunk * maxHostPerFrame + hostBlockSize);

    for (auto* v : { &tempNearFrame, &tempFarFrame, &farDelayedFrame, &nsOutFrame, &preNsFrame,
                     &bypassFrame, &mainFrame, &residualFrame, &outFrame48 })
        v->assign ((size_t) kAecFrameSize, 0.0f);

    // Loopback temp buffers: room for the upsampled worst case (8 kHz loopback -> 48 kHz)
    tempLoopbackRaw.assign (kMaxLoopbackRead, 0);
    tempLoopbackFloat.assign (kMaxLoopbackRead, 0.0f);
    tempLoopbackResampled.assign ((size_t) aec::Resampler::maxOutputFor (kMaxLoopbackRead, 8000, kAecRate), 0.0f);
    requestedLoopbackRate = loopbackRate > 0 ? loopbackRate : kAecRate;
    loopbackResampler = createLoopbackResampler (requestedLoopbackRate);
    loopbackResamplerRate = loopbackResampler != nullptr ? requestedLoopbackRate : 0;
    loopbackRetryCountdown = loopbackResampler != nullptr ? 0 : hostSampleRate;

    resetFifos();

    // Reset debounce, transition, and gap detection state
    paramsDirty = false;
    debounceCountdown = 0;
    nsLevelDirty = false;
    nsDebounceCountdown = 0;
    wasEnabled = false;   // the first enabled block drains, (re)activates capture and re-primes
    lastProcessTicks = 0;
    aecSwapCount = 0;
    loopbackJobCount = 0;

    // Start background worker for AEC3 / suppressor recreation
    startWorker();
}

void EchoCancellerEngine::release()
{
    std::lock_guard<std::mutex> lifecycleLock (lifecycleMutex);
    releaseLocked();
}

void EchoCancellerEngine::releaseLocked()
{
    stopWorker();

    echoController.reset();
    renderAudio.reset();
    captureAudio.reset();
    hpFilter.reset();

    noiseSuppressor.reset();
    incomingSuppressor.reset();
    preNsAudio.reset();
    nsScratchAudio.reset();

    stagedInstance = {};
    stagedSuppressor.reset();
    stagedLoopbackResampler.reset();
    aec3Ready.store (false);
    nsReady.store (false);
    loopbackReady.store (false);
}

// ============================================================================
// FIFOs
// ============================================================================

void EchoCancellerEngine::fifoWrite (juce::AbstractFifo& fifo, std::vector<float>& buffer, const float* src, int n)
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite (juce::jmin (n, fifo.getFreeSpace()), start1, size1, start2, size2);

    if (src != nullptr)
    {
        std::memcpy (buffer.data() + start1, src, sizeof (float) * (size_t) size1);
        if (size2 > 0)
            std::memcpy (buffer.data() + start2, src + size1, sizeof (float) * (size_t) size2);
    }
    else
    {
        std::fill_n (buffer.data() + start1, size1, 0.0f);
        std::fill_n (buffer.data() + start2, size2, 0.0f);
    }

    fifo.finishedWrite (size1 + size2);
}

void EchoCancellerEngine::fifoRead (juce::AbstractFifo& fifo, const std::vector<float>& buffer, float* dst, int n)
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (n, start1, size1, start2, size2);

    std::memcpy (dst, buffer.data() + start1, sizeof (float) * (size_t) size1);
    if (size2 > 0)
        std::memcpy (dst + size1, buffer.data() + start2, sizeof (float) * (size_t) size2);

    fifo.finishedRead (size1 + size2);
}

void EchoCancellerEngine::resetFifos()
{
    // Every FIFO reset re-primes the pipeline, so the total delay is exactly getLatencySamples() again.
    nearEndFifo.reset();
    farEndFifo.reset();
    outputFifo.reset();

    inputResampler.reset();
    outputResampler.reset();
    if (loopbackResampler != nullptr)
        loopbackResampler->reset();

    // Nothing captured before the reset may come out after it. The delay lines and the high-pass are
    // cleared (allocation-free); the band split, AEC3 and suppressor cannot be, so the first
    // getLatencySamples() outputs - all pre-reset audio or priming - are muted, then faded in over
    // resetFadeLength to cover the filters' ringing of the last pre-reset samples.
    padDelay.reset();
    nsCompDelay.reset();
    farMonitorDelay.reset();
    if (hpFilter != nullptr)
        hpFilter->Reset();
    muteRemaining = getLatencySamples();
    resetFadePos = 0;

    fifoWrite (outputFifo, outputBuffer, nullptr, outputPrefill);
}

// ============================================================================
// Audio thread
// ============================================================================

void EchoCancellerEngine::process (const float* micIn, float* out, int numSamples,
                                   aec::LoopbackSource& loopback)
{
    // Factory reset: a fresh AEC3 instance and suppressor from the latest values, built by the worker
    // and swapped in (pre-roll + crossfade) like a parameter change. The error was cleared by
    // requestReset(); a paused reference is re-activated by the usual enable transition below.
    if (resetRequested.exchange (false, std::memory_order_acq_rel) && echoController != nullptr)
    {
        currentParams = pendingParams;
        paramsDirty = false;
        postAecJob (currentParams);

        requestedNsLevel = pendingNsLevel;
        nsLevelDirty = false;
        postNsJob (requestedNsLevel);
    }

    if (numSamples <= 0)
        return;

    // Gap detection — flush stale data if processBlock wasn't called for a while
    {
        auto now = juce::Time::getHighResolutionTicks();
        if (lastProcessTicks != 0)
        {
            double gapMs = juce::Time::highResolutionTicksToSeconds (now - lastProcessTicks) * 1000.0;
            if (gapMs > kGapThresholdMs && echoController != nullptr)
            {
                // Drain all stale loopback data
                while (loopback.readSamples (tempLoopbackRaw.data(), kMaxLoopbackRead) > 0) {}
                resetFifos();
            }
        }
        lastProcessTicks = now;
    }

    inPeak.push (micIn, numSamples);

    bool isEnabled = enabled.load (std::memory_order_acquire);
    bool hasErr = aec3Error.load (std::memory_order_acquire);

    if (! echoController || ! isEnabled || hasErr)
    {
        // Pause capture so loopback doesn't accumulate stale data while idle
        loopback.setCaptureActive (false);

        // Undelayed mic; the processor provides the latency-aligned dry signal
        if (out != micIn)
            std::memcpy (out, micIn, sizeof (float) * static_cast<size_t> (numSamples));
        wasEnabled = false;
        return;
    }

    // Detect disabled→enabled transition — drain loopback and restart capture fresh
    if (! wasEnabled)
    {
        while (loopback.readSamples (tempLoopbackRaw.data(), kMaxLoopbackRead) > 0) {}
        loopback.setCaptureActive (true);
        resetFifos();
        wasEnabled = true;
    }

    // Params changed — debounce and signal background worker
    if (paramsDirty && debounceCountdown > 0)
        --debounceCountdown;

    if (paramsDirty && debounceCountdown <= 0)
    {
        if (pendingParams != currentParams)
        {
            currentParams = pendingParams;
            postAecJob (currentParams);
        }
        paramsDirty = false;
    }

    // Noise level changed — debounce and signal background worker (AEC3 is not touched)
    if (nsLevelDirty && nsDebounceCountdown > 0)
        --nsDebounceCountdown;

    if (nsLevelDirty && nsDebounceCountdown <= 0)
    {
        if (pendingNsLevel != requestedNsLevel)
        {
            requestedNsLevel = pendingNsLevel;
            postNsJob (requestedNsLevel);
        }
        nsLevelDirty = false;
    }

    readLoopback (loopback, numSamples);

    // Blocks larger than announced are split into prepared-size chunks (no reallocation)
    for (int offset = 0; offset < numSamples; offset += hostBlockSize)
        processChunk (micIn + offset, out + offset, juce::jmin (hostBlockSize, numSamples - offset));

    outPeak.push (out, numSamples);
}

void EchoCancellerEngine::readLoopback (aec::LoopbackSource& loopback, int numSamples)
{
    const int reported = loopback.getCapturedSampleRate();
    const int loopbackRate = reported > 0 ? reported : kAecRate;

    // A resampler for a new loopback rate is built by the worker; take it once it is staged and
    // give the old one back to the worker to free.
    if (loopbackReady.load (std::memory_order_acquire))
    {
        std::swap (loopbackResampler, stagedLoopbackResampler);
        loopbackResamplerRate = stagedLoopbackRate;
        loopbackReady.store (false, std::memory_order_release);

        // A failed build (rate 0) is retried after about a second, never every block
        loopbackRetryCountdown = loopbackResamplerRate == 0 ? hostSampleRate : 0;
    }

    if (loopbackRetryCountdown > 0)
    {
        loopbackRetryCountdown -= numSamples;
        if (loopbackRetryCountdown <= 0)
        {
            loopbackRetryCountdown = 0;
            requestedLoopbackRate = 0;   // forget the failed request so it is posted again
        }
    }

    if (loopbackRate != loopbackResamplerRate && loopbackRate != requestedLoopbackRate)
    {
        requestedLoopbackRate = loopbackRate;
        ++loopbackJobCount;
        postLoopbackJob (loopbackRate);
    }

    // Never read more than the resample buffer can take at this rate
    int maxRead = kMaxLoopbackRead;
    if (loopbackRate < kAecRate)
        maxRead = juce::jlimit (1, kMaxLoopbackRead,
                                (int) ((double) ((int) tempLoopbackResampled.size() - 16) * loopbackRate / kAecRate) - 1);

    const int rawRead = loopback.readSamples (tempLoopbackRaw.data(), maxRead);

    // No resampler for this rate yet: the reference is silence (data discarded) until the swap
    if (rawRead <= 0 || loopbackRate != loopbackResamplerRate)
        return;

    for (int i = 0; i < rawRead; ++i)
        tempLoopbackFloat[(size_t) i] = static_cast<float> (tempLoopbackRaw[(size_t) i]) / 32768.0f;

    const int farCount = loopbackResampler->process (tempLoopbackFloat.data(), rawRead,
                                                     tempLoopbackResampled.data(), (int) tempLoopbackResampled.size());
    fifoWrite (farEndFifo, farEndBuffer, tempLoopbackResampled.data(), farCount);
}

void EchoCancellerEngine::processChunk (const float* micIn, float* out, int n)
{
    // 1. host -> 48 kHz into the near-end FIFO
    const int n48 = inputResampler.process (micIn, n, tempMic48.data(), (int) tempMic48.size());

    // Non-finite input (driver glitch) must not poison the recursive AEC3 / NS state
    for (int i = 0; i < n48; ++i)
        if (! std::isfinite (tempMic48[(size_t) i]))
            tempMic48[(size_t) i] = 0.0f;

    fifoWrite (nearEndFifo, nearEndBuffer, tempMic48.data(), n48);

    // 2. 480-sample frames -> 48 kHz -> host into the host-rate output FIFO
    while (nearEndFifo.getNumReady() >= kAecFrameSize)
    {
        fifoRead (nearEndFifo, nearEndBuffer, tempNearFrame.data(), kAecFrameSize);

        if (farEndFifo.getNumReady() >= kAecFrameSize)
            fifoRead (farEndFifo, farEndBuffer, tempFarFrame.data(), kAecFrameSize);
        else
            std::fill (tempFarFrame.begin(), tempFarFrame.end(), 0.0f);

        processFrame();

        const int m = outputResampler.process (outFrame48.data(), kAecFrameSize,
                                               tempHostOut.data(), (int) tempHostOut.size());
        fifoWrite (outputFifo, outputBuffer, tempHostOut.data(), m);
    }

    // 3. take n host samples (zeros on underrun), pad to the firm latency
    const int got = juce::jmin (n, outputFifo.getNumReady());
    fifoRead (outputFifo, outputBuffer, out, got);
    std::fill (out + got, out + n, 0.0f);

    padDelay.process (out, out, n);

    int i = 0;
    if (muteRemaining > 0)
    {
        i = juce::jmin (n, muteRemaining);
        std::fill (out, out + i, 0.0f);
        muteRemaining -= i;
    }

    for (; i < n && resetFadePos < resetFadeLength; ++i, ++resetFadePos)
        out[i] *= ((float) resetFadePos + 0.5f) / (float) resetFadeLength;
}

void EchoCancellerEngine::processFrame()
{
    const StreamConfig streamConfig (kAecRate, 1);

    updateSpectrum (tempFarFrame.data(), kAecFrameSize);
    refPeak.push (tempFarFrame.data(), kAecFrameSize);

    const int delayMs = static_cast<int> (latencyCompMs.load (std::memory_order_relaxed));

    // A new AEC3 instance from the worker (parameter change): pre-roll it over the previous frame so
    // its block framing is filled and its output is time-aligned, run both instances on this frame and
    // crossfade after ProcessCapture. The FIFOs, delay lines and band buffers are untouched, so the
    // latency and the audio stay continuous.
    const bool aecSwap = aec3Ready.load (std::memory_order_acquire);
    AEC3Instance& incoming = stagedInstance;   // the audio thread's while aec3Ready is true

    if (aecSwap)
        preRollAec (incoming, delayMs);

    // Feed render (far-end) to AEC3
    const float* renderChannels[1] = { tempFarFrame.data() };
    renderAudio->CopyFrom (renderChannels, streamConfig);
    renderAudio->SplitIntoFrequencyBands();
    echoController->AnalyzeRender (renderAudio.get());
    if (aecSwap)
        incoming.controller->AnalyzeRender (renderAudio.get());
    bandsToArray (*renderAudio, renderHistory);
    renderAudio->MergeFrequencyBands();

    // A new suppressor from the worker: pre-roll it over the previous frame so its overlap
    // memory is filled and its output is time-aligned, then crossfade to it in this frame.
    const bool levelChange = nsReady.load (std::memory_order_acquire);

    if (levelChange)
    {
        incomingSuppressor = std::move (stagedSuppressor);

        if (incomingSuppressor != nullptr)
        {
            arrayToBands (analyzeHistory, *nsScratchAudio);
            incomingSuppressor->Analyze (*nsScratchAudio);
            arrayToBands (processHistory, *nsScratchAudio);
            incomingSuppressor->Process (nsScratchAudio.get());   // output discarded
        }
    }

    const bool oldOn = noiseSuppressor != nullptr;
    const bool newOn = levelChange ? incomingSuppressor != nullptr : oldOn;

    // Capture: AnalyzeCapture -> Split -> HPF -> NS Analyze -> ProcessCapture -> NS Process -> Merge
    const float* captureInChannels[1] = { tempNearFrame.data() };
    captureAudio->CopyFrom (captureInChannels, streamConfig);
    echoController->AnalyzeCapture (captureAudio.get());
    if (aecSwap)
        incoming.controller->AnalyzeCapture (captureAudio.get());
    captureAudio->SplitIntoFrequencyBands();
    hpFilter->Process (captureAudio.get(), true);

    if (oldOn)
        noiseSuppressor->Analyze (*captureAudio);
    if (levelChange && newOn)
        incomingSuppressor->Analyze (*captureAudio);
    bandsToArray (*captureAudio, analyzeHistory);

    if (aecSwap)
        copyBands (*captureAudio, *incoming.capture);

    echoController->SetAudioBufferDelay (delayMs);
    echoController->ProcessCapture (captureAudio.get(), nullptr, false);

    if (aecSwap)
    {
        incoming.controller->SetAudioBufferDelay (delayMs);
        incoming.controller->ProcessCapture (incoming.capture.get(), nullptr, false);

        // Equal-gain crossfade over the 160-sample band frame: both outputs are the same signal
        auto* const* a = captureAudio->split_bands (0);
        auto* const* b = incoming.capture->split_bands (0);
        for (int band = 0; band < kNumBands; ++band)
            for (int i = 0; i < kBandSize; ++i)
            {
                const float t = ((float) i + 0.5f) / (float) kBandSize;
                a[band][i] = a[band][i] * (1.0f - t) + b[band][i] * t;
            }

        // Retire the old controller through the staged slot; the worker frees it together with the
        // incoming instance's scratch buffers.
        std::swap (echoController, incoming.controller);
        ++aecSwapCount;
        aec3Ready.store (false, std::memory_order_release);
    }

    // Post-AEC3, pre-NS bands: bypass (NS Off) and residual reference, and pre-roll history
    copyBands (*captureAudio, *preNsAudio);
    bandsToArray (*captureAudio, processHistory);

    if (levelChange && oldOn && newOn)
    {
        // Level change: both suppressors for this frame, equal-power crossfade in the band domain
        copyBands (*captureAudio, *nsScratchAudio);
        noiseSuppressor->Process (captureAudio.get());
        incomingSuppressor->Process (nsScratchAudio.get());

        auto* const* a = captureAudio->split_bands (0);
        auto* const* b = nsScratchAudio->split_bands (0);
        for (int band = 0; band < kNumBands; ++band)
            for (int i = 0; i < kBandSize; ++i)
                a[band][i] = a[band][i] * bandFadeOut[i] + b[band][i] * bandFadeIn[i];
    }
    else if (oldOn)
    {
        noiseSuppressor->Process (captureAudio.get());
    }
    else if (newOn)
    {
        incomingSuppressor->Process (captureAudio.get());
    }

    captureAudio->MergeFrequencyBands();
    preNsAudio->MergeFrequencyBands();

    float* nsOutChannels[1] = { nsOutFrame.data() };
    captureAudio->CopyTo (streamConfig, nsOutChannels);
    float* preNsChannels[1] = { preNsFrame.data() };
    preNsAudio->CopyTo (streamConfig, preNsChannels);

    // Bypass = preNs delayed by the suppressor's own delay, so Off and on have the same latency
    nsCompDelay.process (preNsFrame.data(), bypassFrame.data(), kAecFrameSize);

    const float* bypass = bypassFrame.data();
    const float* nsOut = nsOutFrame.data();
    float* mainOut = mainFrame.data();

    if (levelChange && oldOn && ! newOn)          // on -> Off
        for (int i = 0; i < kAecFrameSize; ++i)
            mainOut[i] = nsOut[i] * fadeOut[i] + bypass[i] * fadeIn[i];
    else if (levelChange && ! oldOn && newOn)     // Off -> on
        for (int i = 0; i < kAecFrameSize; ++i)
            mainOut[i] = bypass[i] * fadeOut[i] + nsOut[i] * fadeIn[i];
    else
        std::memcpy (mainOut, newOn ? nsOut : bypass, sizeof (float) * kAecFrameSize);

    // Residual ("hear removed") = delay-aligned input-to-NS minus NS output
    for (int i = 0; i < kAecFrameSize; ++i)
        residualFrame[(size_t) i] = bypass[i] - mainOut[i];

    // Monitor reference gets the same processing delay, so latency never changes
    farMonitorDelay.process (tempFarFrame.data(), farDelayedFrame.data(), kAecFrameSize);

    const float* selected = monitorFarEnd.load (std::memory_order_acquire) ? farDelayedFrame.data()
                          : monitorResidual.load (std::memory_order_acquire) ? residualFrame.data()
                          : mainOut;
    std::memcpy (outFrame48.data(), selected, sizeof (float) * kAecFrameSize);

    if (levelChange)
    {
        // Retire the old suppressor through the staged slot; the worker frees it
        stagedSuppressor = std::move (noiseSuppressor);
        noiseSuppressor = std::move (incomingSuppressor);
        nsReady.store (false, std::memory_order_release);
    }

    std::memcpy (prevNearFrame, tempNearFrame.data(), sizeof (prevNearFrame));
}

void EchoCancellerEngine::preRollAec (AEC3Instance& inst, int delayMs)
{
    // The previous frame exactly as the running instance saw it: render bands, raw capture for
    // AnalyzeCapture, then the high-passed capture bands.
    const StreamConfig streamConfig (kAecRate, 1);

    arrayToBands (renderHistory, *inst.render);
    inst.controller->AnalyzeRender (inst.render.get());

    const float* nearChannels[1] = { prevNearFrame };
    inst.capture->CopyFrom (nearChannels, streamConfig);
    inst.controller->AnalyzeCapture (inst.capture.get());
    arrayToBands (analyzeHistory, *inst.capture);

    inst.controller->SetAudioBufferDelay (delayMs);
    inst.controller->ProcessCapture (inst.capture.get(), nullptr, false);   // output discarded
}

void EchoCancellerEngine::updateSpectrum (const float* farFrame, int numSamples)
{
    // Accumulate samples into FFT buffer
    for (int i = 0; i < numSamples; ++i)
    {
        fftData[fftFillIndex] = farFrame[i];
        ++fftFillIndex;

        if (fftFillIndex >= kFFTSize)
        {
            fftFillIndex = 0;

            // Apply window
            fftWindow.multiplyWithWindowingTable (fftData, kFFTSize);

            // Zero the second half (FFT needs 2x buffer)
            std::memset (fftData + kFFTSize, 0, sizeof (float) * kFFTSize);

            // Perform FFT (in-place, outputs interleaved real/imag pairs)
            fft.performRealOnlyForwardTransform (fftData);

            // Convert to magnitudes in-place (store in first half)
            for (int bin = 0; bin < kFFTSize / 2; ++bin)
            {
                float re = fftData[bin * 2];
                float im = fftData[bin * 2 + 1];
                fftData[bin] = std::sqrt (re * re + im * im) / (float) kFFTSize;
            }

            // Map FFT bins to display bands (logarithmic spacing)
            // 512 bins covers 0-24kHz at 48kHz sample rate
            // Band i covers frequency range: 20 * (24000/20)^(i/N) to 20 * (24000/20)^((i+1)/N)
            const float minFreq = 20.0f;
            const float maxFreq = 24000.0f;
            const float freqPerBin = (float) kAecRate / (float) kFFTSize;

            for (int b = 0; b < kSpectrumBands; ++b)
            {
                float fLow  = minFreq * std::pow (maxFreq / minFreq, (float) b / kSpectrumBands);
                float fHigh = minFreq * std::pow (maxFreq / minFreq, (float) (b + 1) / kSpectrumBands);

                int binLow  = std::max (1, (int) (fLow / freqPerBin));
                int binHigh = std::min (kFFTSize / 2, (int) (fHigh / freqPerBin) + 1);

                float sum = 0.0f;
                int count = 0;
                for (int bin = binLow; bin < binHigh; ++bin)
                {
                    sum += fftData[bin];
                    ++count;
                }

                float avg = count > 0 ? sum / count : 0.0f;
                // Smooth with previous value (exponential decay)
                float prev = displaySpectrumBands[b].load (std::memory_order_relaxed);
                float smoothed = prev * 0.7f + avg * 0.3f;
                displaySpectrumBands[b].store (smoothed, std::memory_order_relaxed);
            }
        }
    }
}
