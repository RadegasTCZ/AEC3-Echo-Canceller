#pragma once

#include <JuceHeader.h>
#include "LoopbackSource.h"
#include <cstdint>
#include <atomic>
#include <vector>

class WasapiLoopbackCapture : public juce::Thread,
                              public aec::LoopbackSource
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
    int readSamples (int16_t* dest, int maxSamples) override;

    Status getStatus() const { return status.load (std::memory_order_acquire); }
    int getCapturedSampleRate() const override { return capturedSampleRate.load (std::memory_order_acquire); }
    int getCapturedChannels() const { return capturedChannels.load (std::memory_order_acquire); }

    // Pause/resume writing to the ring buffer (real-time safe, just an atomic store).
    // When paused, the capture thread still drains WASAPI but discards data.
    // On resume, the ring buffer is flushed by the capture thread before new writes.
    void setCaptureActive (bool active) override { captureActive.store (active, std::memory_order_release); }
    bool isCaptureActive() const { return captureActive.load (std::memory_order_acquire); }

    // Heartbeat — updated each capture loop iteration. Returns high-resolution ticks.
    int64_t getLastHeartbeatTicks() const { return lastHeartbeatTicks.load (std::memory_order_acquire); }

    // Check capture thread health and attempt restart if it died.
    // Call from the message thread (e.g. UI timer), NOT the audio thread.
    void checkHealthAndRecover();

    // Device selection — empty string means system default
    void setDeviceId (const juce::String& deviceId);
    juce::String getDeviceId() const;

    // Enumerate available render (output) devices. Safe to call from any thread.
    static std::vector<DeviceInfo> enumerateDevices();

    // Returns true if the device list has changed since last call (resets flag)
    bool hasDeviceListChanged();

    /** Sample formats the capture loop converts to mono int16. */
    enum class SampleFormat { Unsupported, Float32, Int16 };

    /** The fields of a WAVEFORMATEX / WAVEFORMATEXTENSIBLE mix format that decide how it is read. */
    struct MixFormat
    {
        enum class Sub { Other, Pcm, IeeeFloat };

        int formatTag = 0;           // wFormatTag
        int channels = 0;            // nChannels
        int bitsPerSample = 0;       // wBitsPerSample
        int extraSize = 0;           // cbSize
        int validBitsPerSample = 0;  // Samples.wValidBitsPerSample (EXTENSIBLE only)
        Sub subFormat = Sub::Other;  // SubFormat (EXTENSIBLE only)
    };

    static constexpr int kFormatTagPcm = 0x0001, kFormatTagIeeeFloat = 0x0003, kFormatTagExtensible = 0xFFFE;

    /** Float32 for 32-bit IEEE float, Int16 for 16-bit integer PCM (EXTENSIBLE: 16 valid bits too),
        Unsupported for anything else, including no channels; an unsupported mix format fails the
        capture init, which then retries with backoff. */
    static SampleFormat classifyMixFormat (const MixFormat& f);

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

    // Capture pause/resume — audio thread sets, capture thread reads
    std::atomic<bool> captureActive { true };

    // Heartbeat — capture thread writes, UI/audio thread reads
    std::atomic<int64_t> lastHeartbeatTicks { 0 };

    // Device selection
    mutable juce::CriticalSection deviceIdLock;
    juce::String selectedDeviceId;           // protected by deviceIdLock
    std::atomic<bool> deviceChangeRequest { false };  // signal to reconnect

    // Device change notification
    std::atomic<bool> deviceListChanged { false };

    // Recovery state (message thread only — accessed from checkHealthAndRecover)
    int64_t lastRecoveryAttemptTicks = 0;
    int recoveryBackoffMs = 500;
    static constexpr int kMaxRecoveryBackoffMs = 10000;

    // IMMNotificationClient prevent forward declaration issues - stored as void*
    void* notificationClient = nullptr;
    void* deviceEnumerator = nullptr;

    void registerNotificationClient();
    void unregisterNotificationClient();

    friend class DeviceNotificationClient;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WasapiLoopbackCapture)
};
