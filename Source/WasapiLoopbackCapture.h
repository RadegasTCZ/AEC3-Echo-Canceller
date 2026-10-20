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

    /** Starts the capture thread and lets the watchdog keep it alive. Not the audio thread. */
    void startCapture();
    /** Stops the capture thread; afterwards checkHealthAndRecover() never restarts it until the next
        startCapture(). Not the audio thread (joins the capture thread, up to 2 s). */
    void stopCapture();

    // Lock-free read for the audio thread. Returns number of samples actually read.
    int readSamples (int16_t* dest, int maxSamples) override;

    Status getStatus() const { return status.load (std::memory_order_acquire); }
    int getCapturedSampleRate() const override { return capturedSampleRate.load (std::memory_order_acquire); }
    int getCapturedChannels() const { return capturedChannels.load (std::memory_order_acquire); }

    // Pause/resume writing to the ring buffer (real-time safe, just an atomic store).
    // When paused, the capture thread still drains WASAPI but discards data.
    // On resume the capture thread calls requestFlush(); the reader (audio thread) discards the stale
    // samples on its next readSamples(), so only the reader ever moves the read index.
    void setCaptureActive (bool active) override { captureActive.store (active, std::memory_order_release); }
    bool isCaptureActive() const { return captureActive.load (std::memory_order_acquire); }

    // Heartbeat — updated each capture loop iteration. Returns high-resolution ticks.
    int64_t getLastHeartbeatTicks() const { return lastHeartbeatTicks.load (std::memory_order_acquire); }

    // Ask for the ring buffer to be emptied. Any thread. The reader (audio thread) performs the
    // discard on its next readSamples(), so the SPSC contract holds (only the reader moves the read index).
    void requestFlush() noexcept { flushRequested.store (true, std::memory_order_release); }

    // Test hook: write into the ring exactly as the capture thread does. Returns samples written.
    int writeForTesting (const int16_t* src, int numSamples);

    // Check capture thread health and attempt restart if it died (exponential backoff 500 ms .. 10 s).
    // Does nothing between stopCapture() and the next startCapture().
    // Call from the message thread (the processor's watchdog timer), NOT the audio thread.
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

    /** Test hook: set the status as the capture thread would (e.g. Connecting during its error backoff). */
    void setStatusForTesting (Status s) { status.store (s, std::memory_order_release); }

protected:
    // Test seam (test-only overrides; production uses these defaults): lets CaptureTests drive
    // startCapture / checkHealthAndRecover without WASAPI or a real thread, and with a fake clock.
    virtual void startCaptureThread() { startThread (juce::Thread::Priority::highest); }
    virtual bool isCaptureThreadRunning() const { return isThreadRunning(); }
    virtual int64_t recoveryNowTicks() const { return juce::Time::getHighResolutionTicks(); }

private:
    void run() override;
    bool initAndCapture();

    std::atomic<bool> flushRequested { false };

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

    // Lifecycle: wantRunning is true from startCapture() to stopCapture(). lifecycleLock serialises
    // start / stop / a watchdog restart (never taken on the audio thread; the watchdog only try-locks).
    std::atomic<bool> wantRunning { false };
    juce::CriticalSection lifecycleLock;

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
