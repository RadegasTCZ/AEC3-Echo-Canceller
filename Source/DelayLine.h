#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <vector>

namespace aec
{
/** Integer-sample delay with a fixed maximum. prepare() allocates (message thread);
    the rest is allocation-free. process() may run in place (in == out). */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        jassert (maxDelaySamples >= 0);
        maxDelay = juce::jmax (0, maxDelaySamples);
        buffer.assign ((size_t) maxDelay + 1, 0.0f);   // +1: delay == max reads the oldest slot
        delay = 0;
        writePos = 0;
    }

    void setDelay (int samples) noexcept
    {
        jassert (samples >= 0 && samples <= maxDelay);
        delay = juce::jlimit (0, maxDelay, samples);
        reset();
    }

    int getDelay() const noexcept { return delay; }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void process (const float* in, float* out, int n) noexcept
    {
        const int len = (int) buffer.size();
        jassert (len > 0);   // prepare() not called
        if (len == 0)
            return;

        int readPos = writePos - delay;
        if (readPos < 0)
            readPos += len;

        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];                     // read before write: in == out is fine
            buffer[(size_t) writePos] = x;
            out[i] = buffer[(size_t) readPos];
            if (++writePos == len) writePos = 0;
            if (++readPos == len)  readPos = 0;
        }
    }

private:
    std::vector<float> buffer;
    int maxDelay = 0, delay = 0, writePos = 0;
};
} // namespace aec
