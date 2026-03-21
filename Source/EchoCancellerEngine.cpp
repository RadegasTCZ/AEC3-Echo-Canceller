#include "EchoCancellerEngine.h"
#include "WasapiLoopbackCapture.h"

#include "api/audio/echo_canceller3_factory.h"
#include "api/audio/echo_canceller3_config.h"
#include "api/audio/audio_processing.h"
#include "api/environment/environment_factory.h"
#include "modules/audio_processing/audio_buffer.h"
#include "modules/audio_processing/high_pass_filter.h"

#include <algorithm>
#include <cstring>
#include <cmath>

// AEC3.lib is built in Release mode only — Debug builds will fail with CRT mismatches
#if defined(_MSC_VER) && defined(_DEBUG)
#error "AEC3.lib requires Release configuration. Switch to Release|x64 to build."
#endif

// Link AEC3 static library (MSVC)
#pragma comment(lib, "AEC3.lib")

using namespace webrtc;

// ============================================================================
// Slider → AEC3 value mapping functions
// ============================================================================

// LF suppression: 0% → 1.5 (very permissive), 100% → 0.01 (max suppression)
float EchoCancellerEngine::mapSuppression (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 1.5f - t * 1.49f;  // 1.5 → 0.01
}

// HF suppression: 0% → 0.5, 100% → 0.01 (narrower range, HF is more sensitive)
float EchoCancellerEngine::mapHfSuppression (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 0.5f - t * 0.49f;  // 0.5 → 0.01
}

// LF transparency: 0% → 1.5 (very permissive), 100% → 0.01
float EchoCancellerEngine::mapTransparency (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 1.5f - t * 1.49f;  // 1.5 → 0.01
}

// HF transparency: 0% → 0.5, 100% → 0.005
float EchoCancellerEngine::mapHfTransparency (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 0.5f - t * 0.495f;  // 0.5 → 0.005
}

// Attack (max_inc_factor): 0% → 0.5, 100% → 5.0
float EchoCancellerEngine::mapAttack (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 0.5f + t * 4.5f;
}

// Decay (max_dec_factor_lf): 0% → 0.01, 100% → 1.0
float EchoCancellerEngine::mapDecay (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 0.01f + t * 0.99f;
}

// HF ducking (max_gain_during_echo): 0% → 1.0 (no ducking), 100% → 0.0 (full ducking)
float EchoCancellerEngine::mapHfDucking (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 1.0f - t;
}

// HF echo threshold (enr_threshold): 0% → 2.0 (less sensitive), 100% → 0.1 (very sensitive)
float EchoCancellerEngine::mapHfEchoThreshold (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 2.0f - t * 1.9f;  // 2.0 → 0.1
}

// ============================================================================
// Engine lifecycle
// ============================================================================

EchoCancellerEngine::EchoCancellerEngine() = default;

EchoCancellerEngine::~EchoCancellerEngine()
{
    release();
}

void EchoCancellerEngine::setParams (const Params& p)
{
    if (p != pendingParams)
    {
        pendingParams = p;
        paramsDirty = true;
        debounceCountdown = kDebounceBlocks;
    }
}

EchoCancellerEngine::AEC3Instance EchoCancellerEngine::createAEC3Instance (const Params& p)
{
    EchoCanceller3Config config;

    // Map slider values to AEC3 thresholds, clamping to ensure
    // enr_transparent ≤ enr_suppress (required by AEC3 suppressor)
    float lfTransp  = mapTransparency (p.lfTransparency);
    float lfSupp    = mapSuppression (p.lfSuppression);
    float hfTransp  = mapHfTransparency (p.hfTransparency);
    float hfSupp    = mapHfSuppression (p.hfSuppression);
    lfTransp = std::min (lfTransp, lfSupp);
    hfTransp = std::min (hfTransp, hfSupp);

    float dtLfTransp = mapTransparency (p.dtLfTransparency);
    float dtLfSupp   = mapSuppression (p.dtLfSuppression);
    float dtHfTransp = mapHfTransparency (p.dtHfTransparency);
    float dtHfSupp   = mapHfSuppression (p.dtHfSuppression);
    dtLfTransp = std::min (dtLfTransp, dtLfSupp);
    dtHfTransp = std::min (dtHfTransp, dtHfSupp);

    // Normal tuning
    config.suppressor.normal_tuning.mask_lf =
        EchoCanceller3Config::Suppressor::MaskingThresholds (
            lfTransp, lfSupp, 0.3f);
    config.suppressor.normal_tuning.mask_hf =
        EchoCanceller3Config::Suppressor::MaskingThresholds (
            hfTransp, hfSupp, 0.3f);
    config.suppressor.normal_tuning.max_inc_factor = mapAttack (p.lfAttack);
    config.suppressor.normal_tuning.max_dec_factor_lf = mapDecay (p.lfDecay);

    // Nearend (double-talk) tuning
    config.suppressor.nearend_tuning.mask_lf =
        EchoCanceller3Config::Suppressor::MaskingThresholds (
            dtLfTransp, dtLfSupp, 0.3f);
    config.suppressor.nearend_tuning.mask_hf =
        EchoCanceller3Config::Suppressor::MaskingThresholds (
            dtHfTransp, dtHfSupp, 0.3f);
    config.suppressor.nearend_tuning.max_inc_factor = mapAttack (p.dtLfAttack);
    config.suppressor.nearend_tuning.max_dec_factor_lf = mapDecay (p.dtLfDecay);

    // High-bands suppression
    config.suppressor.high_bands_suppression.max_gain_during_echo =
        mapHfDucking (p.hfDucking);
    config.suppressor.high_bands_suppression.enr_threshold =
        mapHfEchoThreshold (p.hfEchoThreshold);

    // System params
    config.filter.refined.length_blocks =
        static_cast<size_t> (juce::jlimit (1.0f, 40.0f, p.roomReverb));
    config.filter.coarse.length_blocks = config.filter.refined.length_blocks;
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

    inst.hp = std::make_unique<HighPassFilter> (kAecRate, 1);

    return inst;
}

void EchoCancellerEngine::aec3WorkerLoop()
{
    while (true)
    {
        Params p;
        {
            std::unique_lock<std::mutex> lock (aec3Mutex);
            aec3Cv.wait (lock, [this] { return aec3WorkerHasWork || aec3WorkerExit; });

            if (aec3WorkerExit)
                return;

            aec3WorkerHasWork = false;
            p = aec3WorkerParams;
        }

        auto inst = createAEC3Instance (p);
        stagedInstance = std::move (inst);
        aec3Ready.store (true, std::memory_order_release);
    }
}

void EchoCancellerEngine::startWorker()
{
    aec3WorkerExit = false;
    aec3WorkerHasWork = false;
    aec3Ready.store (false, std::memory_order_relaxed);
    aec3Worker = std::thread (&EchoCancellerEngine::aec3WorkerLoop, this);
}

void EchoCancellerEngine::stopWorker()
{
    if (aec3Worker.joinable())
    {
        {
            std::lock_guard<std::mutex> lock (aec3Mutex);
            aec3WorkerExit = true;
        }
        aec3Cv.notify_one();
        aec3Worker.join();
    }
}

void EchoCancellerEngine::prepare (double sampleRate, int maxBlockSize)
{
    release();

    hostSampleRate = static_cast<int> (sampleRate);
    hostBlockSize = maxBlockSize;
    needsHostResampling = (hostSampleRate != kAecRate);

    // Create initial AEC3 synchronously (prepare is not real-time)
    auto inst = createAEC3Instance (currentParams);
    echoController = std::move (inst.controller);
    renderAudio = std::move (inst.render);
    captureAudio = std::move (inst.capture);
    hpFilter = std::move (inst.hp);

    // Size FIFOs — generous for buffering
    int fifoSize = std::max (8192, maxBlockSize * 8);
    int p2 = 1;
    while (p2 < fifoSize) p2 <<= 1;
    fifoSize = p2;

    nearEndFifo.setTotalSize (fifoSize);
    nearEndBuffer.resize (static_cast<size_t> (fifoSize), 0.0f);

    farEndFifo.setTotalSize (fifoSize);
    farEndBuffer.resize (static_cast<size_t> (fifoSize), 0.0f);

    outputFifo.setTotalSize (fifoSize);
    outputBuffer.resize (static_cast<size_t> (fifoSize), 0.0f);

    nearEndFifo.reset();
    farEndFifo.reset();
    outputFifo.reset();

    // Frame buffers
    tempNearFrame.resize (kAecFrameSize);
    tempFarFrame.resize (kAecFrameSize);
    tempOutFrame.resize (kAecFrameSize);

    // Loopback temp buffers
    tempLoopbackRaw.resize (kMaxLoopbackRead);
    tempLoopbackFloat.resize (kMaxLoopbackRead);
    tempLoopbackResampled.resize (kMaxLoopbackRead * 2);

    // Host resampling buffers
    if (needsHostResampling)
    {
        int maxResampled = static_cast<int> (maxBlockSize * (double) kAecRate / hostSampleRate) + 64;
        tempMicResampled.resize (static_cast<size_t> (maxResampled));
        tempOutputPull.resize (static_cast<size_t> (maxBlockSize * 2));
        inputResampler.reset();
        outputResampler.reset();
    }

    lastLoopbackRate = 0;
    loopbackResampler.reset();

    // Reset debounce and transition state
    paramsDirty = false;
    debounceCountdown = 0;
    wasEnabled = true;

    // Start background worker for AEC3 recreation
    startWorker();
}

void EchoCancellerEngine::release()
{
    stopWorker();

    echoController.reset();
    renderAudio.reset();
    captureAudio.reset();
    hpFilter.reset();

    stagedInstance = {};
}

void EchoCancellerEngine::process (const float* micIn, float* out, int numSamples,
                                   WasapiLoopbackCapture& loopback)
{
    // Handle factory reset request from UI thread (safe: runs on audio thread)
    if (resetRequested.load (std::memory_order_acquire))
    {
        int savedRate = hostSampleRate;
        int savedBlock = hostBlockSize;
        release();
        prepare (savedRate > 0 ? (double) savedRate : 48000.0,
                 savedBlock > 0 ? savedBlock : 480);
        resetRequested.store (false, std::memory_order_release);
    }

    bool isEnabled = enabled.load (std::memory_order_acquire);

    if (! echoController || ! isEnabled)
    {
        // Drain loopback so the ring buffer doesn't overflow with stale data
        loopback.readSamples (tempLoopbackRaw.data(), kMaxLoopbackRead);

        std::memcpy (out, micIn, sizeof (float) * static_cast<size_t> (numSamples));
        wasEnabled = false;
        return;
    }

    // Detect disabled→enabled transition — reset FIFOs to discard stale data
    if (! wasEnabled)
    {
        nearEndFifo.reset();
        farEndFifo.reset();
        outputFifo.reset();
        wasEnabled = true;
    }

    // Swap in new AEC3 instance from background worker if ready
    if (aec3Ready.load (std::memory_order_acquire))
    {
        echoController = std::move (stagedInstance.controller);
        renderAudio    = std::move (stagedInstance.render);
        captureAudio   = std::move (stagedInstance.capture);
        hpFilter       = std::move (stagedInstance.hp);

        // Reset FIFOs to prevent stale data accumulation / latency drift
        nearEndFifo.reset();
        farEndFifo.reset();
        outputFifo.reset();

        aec3Ready.store (false, std::memory_order_release);
    }

    // Check if params changed — debounce and signal background worker
    if (paramsDirty && debounceCountdown > 0)
        --debounceCountdown;

    if (paramsDirty && debounceCountdown <= 0)
    {
        if (pendingParams != currentParams)
        {
            currentParams = pendingParams;

            // Signal worker to create new AEC3 (brief lock, sub-microsecond)
            {
                std::lock_guard<std::mutex> lock (aec3Mutex);
                aec3WorkerParams = currentParams;
                aec3WorkerHasWork = true;
            }
            aec3Cv.notify_one();
        }
        paramsDirty = false;
    }

    // 1. Push mic input into nearEnd FIFO (resample to 48k if needed)
    {
        const float* srcData = micIn;
        int srcCount = numSamples;

        if (needsHostResampling)
        {
            double ratio = (double) hostSampleRate / (double) kAecRate;
            int maxOutput = static_cast<int> (numSamples / ratio) + 16;
            if (maxOutput > static_cast<int> (tempMicResampled.size()))
                tempMicResampled.resize (static_cast<size_t> (maxOutput));

            inputResampler.process (ratio, micIn, tempMicResampled.data(),
                                   maxOutput, numSamples, 0);
            srcData = tempMicResampled.data();
            srcCount = maxOutput;
        }

        int space = nearEndFifo.getFreeSpace();
        int toPush = std::min (srcCount, space);

        int start1, size1, start2, size2;
        nearEndFifo.prepareToWrite (toPush, start1, size1, start2, size2);

        std::memcpy (nearEndBuffer.data() + start1, srcData,
                     sizeof (float) * static_cast<size_t> (size1));
        if (size2 > 0)
            std::memcpy (nearEndBuffer.data() + start2, srcData + size1,
                         sizeof (float) * static_cast<size_t> (size2));

        nearEndFifo.finishedWrite (size1 + size2);
    }

    // 2. Read loopback, resample to 48k, push into farEnd FIFO
    {
        int rawRead = loopback.readSamples (tempLoopbackRaw.data(), kMaxLoopbackRead);

        if (rawRead > 0)
        {
            int loopbackRate = loopback.getCapturedSampleRate();

            // Convert int16 to float
            for (int i = 0; i < rawRead; ++i)
                tempLoopbackFloat[static_cast<size_t> (i)] =
                    static_cast<float> (tempLoopbackRaw[static_cast<size_t> (i)]) / 32768.0f;

            const float* farSamples = tempLoopbackFloat.data();
            int farCount = rawRead;

            // Resample if loopback rate differs from AEC rate
            if (loopbackRate > 0 && loopbackRate != kAecRate)
            {
                if (loopbackRate != lastLoopbackRate)
                {
                    loopbackResampler.reset();
                    lastLoopbackRate = loopbackRate;
                }

                double ratio = (double) loopbackRate / (double) kAecRate;
                int maxOutput = static_cast<int> (rawRead / ratio) + 16;
                if (maxOutput > static_cast<int> (tempLoopbackResampled.size()))
                    tempLoopbackResampled.resize (static_cast<size_t> (maxOutput));

                loopbackResampler.process (ratio, tempLoopbackFloat.data(),
                                          tempLoopbackResampled.data(),
                                          maxOutput, rawRead, 0);
                farSamples = tempLoopbackResampled.data();
                farCount = maxOutput;
            }
            else
            {
                lastLoopbackRate = loopbackRate;
            }

            int space = farEndFifo.getFreeSpace();
            int toPush = std::min (farCount, space);

            int start1, size1, start2, size2;
            farEndFifo.prepareToWrite (toPush, start1, size1, start2, size2);

            std::memcpy (farEndBuffer.data() + start1, farSamples,
                         sizeof (float) * static_cast<size_t> (size1));
            if (size2 > 0)
                std::memcpy (farEndBuffer.data() + start2, farSamples + size1,
                             sizeof (float) * static_cast<size_t> (size2));

            farEndFifo.finishedWrite (size1 + size2);
        }
    }

    // 3. Processing loop: while nearEnd has kAecFrameSize (480) samples
    StreamConfig streamConfig (kAecRate, 1);

    while (nearEndFifo.getNumReady() >= kAecFrameSize)
    {
        // Pop nearEnd frame
        {
            int start1, size1, start2, size2;
            nearEndFifo.prepareToRead (kAecFrameSize, start1, size1, start2, size2);

            std::memcpy (tempNearFrame.data(), nearEndBuffer.data() + start1,
                         sizeof (float) * static_cast<size_t> (size1));
            if (size2 > 0)
                std::memcpy (tempNearFrame.data() + size1, nearEndBuffer.data() + start2,
                             sizeof (float) * static_cast<size_t> (size2));

            nearEndFifo.finishedRead (size1 + size2);
        }

        // Pop or zero-fill farEnd frame
        if (farEndFifo.getNumReady() >= kAecFrameSize)
        {
            int start1, size1, start2, size2;
            farEndFifo.prepareToRead (kAecFrameSize, start1, size1, start2, size2);

            std::memcpy (tempFarFrame.data(), farEndBuffer.data() + start1,
                         sizeof (float) * static_cast<size_t> (size1));
            if (size2 > 0)
                std::memcpy (tempFarFrame.data() + size1, farEndBuffer.data() + start2,
                             sizeof (float) * static_cast<size_t> (size2));

            farEndFifo.finishedRead (size1 + size2);
        }
        else
        {
            std::memset (tempFarFrame.data(), 0, sizeof (float) * kAecFrameSize);
        }

        // Update spectrum display from far-end frame
        updateSpectrum (tempFarFrame.data(), kAecFrameSize);

        // Update peak levels
        {
            float peakIn = 0.0f, peakFar = 0.0f;
            for (int i = 0; i < kAecFrameSize; ++i)
            {
                float absNear = std::abs (tempNearFrame[(size_t) i]);
                float absFar  = std::abs (tempFarFrame[(size_t) i]);
                if (absNear > peakIn)  peakIn  = absNear;
                if (absFar  > peakFar) peakFar = absFar;
            }
            float inDb  = 20.0f * std::log10 (std::max (peakIn,  1e-7f));
            float refDb = 20.0f * std::log10 (std::max (peakFar, 1e-7f));

            // Smooth with decay (fast attack, slow release)
            float prevIn  = peakInDb.load (std::memory_order_relaxed);
            float prevRef = peakRefDb.load (std::memory_order_relaxed);
            peakInDb.store  (inDb  > prevIn  ? inDb  : prevIn  * 0.95f + inDb  * 0.05f, std::memory_order_relaxed);
            peakRefDb.store (refDb > prevRef ? refDb : prevRef * 0.95f + refDb * 0.05f, std::memory_order_relaxed);
        }

        // Feed render (far-end) to AEC3
        const float* renderChannels[1] = { tempFarFrame.data() };
        renderAudio->CopyFrom (renderChannels, streamConfig);
        renderAudio->SplitIntoFrequencyBands();
        echoController->AnalyzeRender (renderAudio.get());
        renderAudio->MergeFrequencyBands();

        // Feed capture (near-end) and process
        const float* captureInChannels[1] = { tempNearFrame.data() };
        captureAudio->CopyFrom (captureInChannels, streamConfig);
        echoController->AnalyzeCapture (captureAudio.get());
        captureAudio->SplitIntoFrequencyBands();
        hpFilter->Process (captureAudio.get(), true);
        echoController->SetAudioBufferDelay (
            static_cast<int> (latencyCompMs.load (std::memory_order_relaxed)));
        echoController->ProcessCapture (captureAudio.get(), nullptr, false);
        captureAudio->MergeFrequencyBands();

        // Get output
        float* outChannels[1] = { tempOutFrame.data() };
        captureAudio->CopyTo (streamConfig, outChannels);

        // Measure output peak
        {
            float peakO = 0.0f;
            for (int i = 0; i < kAecFrameSize; ++i)
            {
                float absOut = std::abs (tempOutFrame[(size_t) i]);
                if (absOut > peakO) peakO = absOut;
            }
            float outDb = 20.0f * std::log10 (std::max (peakO, 1e-7f));
            float prevOut = peakOutDb.load (std::memory_order_relaxed);
            peakOutDb.store (outDb > prevOut ? outDb : prevOut * 0.95f + outDb * 0.05f, std::memory_order_relaxed);
        }

        // Push into output FIFO (far-end if monitoring, otherwise AEC output)
        {
            const float* outputSrc = monitorFarEnd.load (std::memory_order_acquire)
                                         ? tempFarFrame.data()
                                         : tempOutFrame.data();

            int space = outputFifo.getFreeSpace();
            int toPush = std::min (kAecFrameSize, space);

            int start1, size1, start2, size2;
            outputFifo.prepareToWrite (toPush, start1, size1, start2, size2);

            std::memcpy (outputBuffer.data() + start1, outputSrc,
                         sizeof (float) * static_cast<size_t> (size1));
            if (size2 > 0)
                std::memcpy (outputBuffer.data() + start2, outputSrc + size1,
                             sizeof (float) * static_cast<size_t> (size2));

            outputFifo.finishedWrite (size1 + size2);
        }
    }

    // 4. Pull from output FIFO -> float output
    if (needsHostResampling)
    {
        // Pull enough 48k samples to produce numSamples at hostRate
        int inputNeeded = static_cast<int> (std::ceil (numSamples * (double) kAecRate / hostSampleRate)) + 16;
        int ready = outputFifo.getNumReady();
        int toPull = std::min (inputNeeded, ready);

        if (toPull > 0)
        {
            if (toPull > static_cast<int> (tempOutputPull.size()))
                tempOutputPull.resize (static_cast<size_t> (toPull));

            int start1, size1, start2, size2;
            outputFifo.prepareToRead (toPull, start1, size1, start2, size2);

            std::memcpy (tempOutputPull.data(), outputBuffer.data() + start1,
                         sizeof (float) * static_cast<size_t> (size1));
            if (size2 > 0)
                std::memcpy (tempOutputPull.data() + size1, outputBuffer.data() + start2,
                             sizeof (float) * static_cast<size_t> (size2));

            outputFifo.finishedRead (size1 + size2);

            // Resample 48k -> hostRate
            double ratio = (double) kAecRate / (double) hostSampleRate;
            outputResampler.process (ratio, tempOutputPull.data(), out,
                                    numSamples, toPull, 0);
        }
        else
        {
            std::memset (out, 0, sizeof (float) * static_cast<size_t> (numSamples));
        }
    }
    else
    {
        // No resampling — direct pull
        int ready = outputFifo.getNumReady();
        int toPull = std::min (numSamples, ready);

        if (toPull > 0)
        {
            int start1, size1, start2, size2;
            outputFifo.prepareToRead (toPull, start1, size1, start2, size2);

            std::memcpy (out, outputBuffer.data() + start1,
                         sizeof (float) * static_cast<size_t> (size1));
            if (size2 > 0)
                std::memcpy (out + size1, outputBuffer.data() + start2,
                             sizeof (float) * static_cast<size_t> (size2));

            outputFifo.finishedRead (size1 + size2);
        }

        // Fill remaining with silence (startup latency)
        for (int i = toPull; i < numSamples; ++i)
            out[i] = 0.0f;
    }
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
