#pragma once

#include <JuceHeader.h>
#include "../Source/LoopbackSource.h"
#include <cstdint>
#include <vector>

namespace aec::test
{
    /** LoopbackSource fed by the test. feed() queues mono float samples (converted to int16 as
        WASAPI delivers them); readSamples() hands them out in order. Never fed = "no reference".
        While capture is inactive (setCaptureActive (false)) fed samples are discarded, as WASAPI does.
        Storage is reserved up front, so readSamples() never allocates. */
    class FakeLoopback final : public aec::LoopbackSource
    {
    public:
        explicit FakeLoopback (int sampleRate = 48000, int capacity = 48000 * 30)
            : rate (sampleRate)
        {
            data.reserve ((size_t) capacity);
        }

        void feed (const float* x, int n)
        {
            if (! active)
                return;

            for (int i = 0; i < n; ++i)
                data.push_back ((int16_t) juce::jlimit (-32768, 32767, juce::roundToInt (x[i] * 32768.0f)));
        }

        int readSamples (int16_t* dest, int maxSamples) override
        {
            const int n = juce::jmin (maxSamples, (int) data.size() - readPos);
            for (int i = 0; i < n; ++i)
                dest[i] = data[(size_t) (readPos + i)];
            readPos += n;
            return n;
        }

        int  getCapturedSampleRate() const override { return rate; }
        void setRate (int newRate)                  { rate = newRate; }
        void setCaptureActive (bool a) override     { active = a; }

        bool active = true;

    private:
        int rate;
        std::vector<int16_t> data;
        int readPos = 0;
    };
}
