#pragma once

#include <cstdint>

namespace aec
{
/** The far-end reference as the engine sees it (WasapiLoopbackCapture in the plugin, a fake in tests).
    All three calls are made on the audio thread and must be real-time safe. */
struct LoopbackSource
{
    virtual ~LoopbackSource() = default;
    virtual int  readSamples (int16_t* dest, int maxSamples) = 0;  // mono int16
    virtual int  getCapturedSampleRate() const = 0;
    virtual void setCaptureActive (bool) = 0;
};
} // namespace aec
