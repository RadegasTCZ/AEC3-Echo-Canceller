#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <atomic>
#include <vector>

class WasapiLoopbackCapture : public juce::Thread
{
public:
    enum class Status
    {
        Disconnected,
        Connecting,
        Connected,
        Error
    };

    WasapiLoopbackCapture();
    ~WasapiLoopbackCapture() override;

    void startCapture();
    void stopCapture();

    // Lock-free read for the audio thread. Returns number of samples actually read.
    int readSamples (int16_t* dest, int maxSamples);

    Status getStatus() const { return status.load (std::memory_order_acquire); }
    int getCapturedSampleRate() const { return capturedSampleRate.load (std::memory_order_acquire); }
    int getCapturedChannels() const { return capturedChannels.load (std::memory_order_acquire); }

private:
    void run() override;
    bool initAndCapture(); // returns true on clean exit, false on error (triggers reconnect)

    // Ring buffer (lock-free SPSC)
    static constexpr int ringBufferSize = 65536;
    juce::AbstractFifo fifo { ringBufferSize };
    std::vector<int16_t> ringBuffer;

    std::atomic<Status> status { Status::Disconnected };
    std::atomic<int> capturedSampleRate { 0 };
    std::atomic<int> capturedChannels { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WasapiLoopbackCapture)
};
