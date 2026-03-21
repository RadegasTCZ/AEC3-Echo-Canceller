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

    // Device info for enumeration
    struct DeviceInfo
    {
        juce::String id;    // WASAPI endpoint ID
        juce::String name;  // Friendly name
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

    // Device selection — empty string means system default
    void setDeviceId (const juce::String& deviceId);
    juce::String getDeviceId() const;

    // Enumerate available render (output) devices. Safe to call from any thread.
    static std::vector<DeviceInfo> enumerateDevices();

    // Returns true if the device list has changed since last call (resets flag)
    bool hasDeviceListChanged();

private:
    void run() override;
    bool initAndCapture();

    // Ring buffer (lock-free SPSC)
    static constexpr int ringBufferSize = 65536;
    juce::AbstractFifo fifo { ringBufferSize };
    std::vector<int16_t> ringBuffer;

    std::atomic<Status> status { Status::Disconnected };
    std::atomic<int> capturedSampleRate { 0 };
    std::atomic<int> capturedChannels { 0 };

    // Device selection
    mutable juce::CriticalSection deviceIdLock;
    juce::String selectedDeviceId;           // protected by deviceIdLock
    std::atomic<bool> deviceChangeRequest { false };  // signal to reconnect

    // Device change notification
    std::atomic<bool> deviceListChanged { false };

    // IMMNotificationClient prevent forward declaration issues - stored as void*
    void* notificationClient = nullptr;
    void* deviceEnumerator = nullptr;

    void registerNotificationClient();
    void unregisterNotificationClient();

    friend class DeviceNotificationClient;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WasapiLoopbackCapture)
};
