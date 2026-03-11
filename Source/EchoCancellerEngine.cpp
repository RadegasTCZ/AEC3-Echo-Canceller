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

// Link AEC3 static library (MSVC)
#pragma comment(lib, "AEC3.lib")

using namespace webrtc;

EchoCancellerEngine::EchoCancellerEngine() = default;

EchoCancellerEngine::~EchoCancellerEngine()
{
    release();
}

void EchoCancellerEngine::createAEC3 (bool aggressive)
{
    EchoCanceller3Config config;

    if (aggressive)
    {
        // More aggressive suppression for speaker setups
        config.suppressor.normal_tuning.mask_lf =
            EchoCanceller3Config::Suppressor::MaskingThresholds (0.1f, 0.15f, 0.1f);
        config.suppressor.normal_tuning.mask_hf =
            EchoCanceller3Config::Suppressor::MaskingThresholds (0.03f, 0.05f, 0.1f);
        config.suppressor.normal_tuning.max_dec_factor_lf = 0.1f;

        config.suppressor.nearend_tuning.mask_lf =
            EchoCanceller3Config::Suppressor::MaskingThresholds (0.5f, 0.6f, 0.1f);
        config.suppressor.nearend_tuning.mask_hf =
            EchoCanceller3Config::Suppressor::MaskingThresholds (0.05f, 0.1f, 0.1f);

        config.suppressor.high_bands_suppression.max_gain_during_echo = 0.1f;
        config.suppressor.high_bands_suppression.enr_threshold = 0.5f;

        config.ep_strength.bounded_erl = true;
    }

    auto env = webrtc::CreateEnvironment();
    EchoCanceller3Factory factory (config);
    echoController = factory.Create (env, kAecRate, 1, 1);

    // Create audio buffers for render (far-end) and capture (near-end)
    renderAudio = std::make_unique<AudioBuffer> (
        kAecRate, 1, kAecRate, 1, kAecRate, 1);
    captureAudio = std::make_unique<AudioBuffer> (
        kAecRate, 1, kAecRate, 1, kAecRate, 1);

    hpFilter = std::make_unique<HighPassFilter> (kAecRate, 1);

    currentAggressiveMode = aggressive;
}

void EchoCancellerEngine::prepare (double sampleRate, int maxBlockSize)
{
    release();

    hostSampleRate = static_cast<int> (sampleRate);
    needsHostResampling = (hostSampleRate != kAecRate);

    createAEC3 (aggressiveMode.load (std::memory_order_acquire));

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
}

void EchoCancellerEngine::release()
{
    echoController.reset();
    renderAudio.reset();
    captureAudio.reset();
    hpFilter.reset();
}

void EchoCancellerEngine::process (const float* micIn, float* out, int numSamples,
                                   WasapiLoopbackCapture& loopback)
{
    if (! echoController || ! enabled.load (std::memory_order_acquire))
    {
        std::memcpy (out, micIn, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    // Check if aggressive mode changed — recreate AEC3 if so
    bool wantAggressive = aggressiveMode.load (std::memory_order_acquire);
    if (wantAggressive != currentAggressiveMode)
        createAEC3 (wantAggressive);

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
        echoController->SetAudioBufferDelay (0);
        echoController->ProcessCapture (captureAudio.get(), nullptr, false);
        captureAudio->MergeFrequencyBands();

        // Get output
        float* outChannels[1] = { tempOutFrame.data() };
        captureAudio->CopyTo (streamConfig, outChannels);

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
