#include "WasapiLoopbackCapture.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>

#pragma comment(lib, "avrt.lib")

// RAII wrapper for COM pointers — guarantees Release on all exit paths
template <typename T>
struct ComPtr
{
    T* ptr = nullptr;
    ~ComPtr() { if (ptr) ptr->Release(); }
    T** operator& () { return &ptr; }
    T* operator-> () { return ptr; }
    operator T* () { return ptr; }
    explicit operator bool () const { return ptr != nullptr; }
};

// RAII wrapper for COM initialization on the current thread
struct ComScope
{
    bool ok;
    ComScope()
    {
        HRESULT hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        if (FAILED (hr))
            hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        ok = SUCCEEDED (hr);
    }
    ~ComScope() { if (ok) CoUninitialize(); }
};

// RAII wrapper for AVRT thread priority boost
struct AvrtScope
{
    HANDLE task = nullptr;
    AvrtScope()
    {
        DWORD index = 0;
        task = AvSetMmThreadCharacteristicsW (L"Pro Audio", &index);
    }
    ~AvrtScope() { if (task) AvRevertMmThreadCharacteristics (task); }
};

// ============================================================================
// IMMNotificationClient implementation — COM callback for device changes
// ============================================================================

class DeviceNotificationClient : public IMMNotificationClient
{
public:
    DeviceNotificationClient (WasapiLoopbackCapture& owner) : owner (owner) {}

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override  { return InterlockedIncrement (&refCount); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = InterlockedDecrement (&refCount);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
    {
        if (riid == __uuidof (IUnknown) || riid == __uuidof (IMMNotificationClient))
        {
            *ppv = static_cast<IMMNotificationClient*> (this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // IMMNotificationClient — flag device list change on any event
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged (LPCWSTR, DWORD) override
    {
        owner.deviceListChanged.store (true, std::memory_order_release);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded (LPCWSTR) override
    {
        owner.deviceListChanged.store (true, std::memory_order_release);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved (LPCWSTR) override
    {
        owner.deviceListChanged.store (true, std::memory_order_release);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged (EDataFlow, ERole, LPCWSTR) override
    {
        owner.deviceListChanged.store (true, std::memory_order_release);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged (LPCWSTR, const PROPERTYKEY) override
    {
        return S_OK;
    }

private:
    WasapiLoopbackCapture& owner;
    LONG refCount = 1;
};

// ============================================================================
// WasapiLoopbackCapture
// ============================================================================

WasapiLoopbackCapture::WasapiLoopbackCapture()
    : Thread ("WASAPI Loopback")
{
    ringBuffer.resize (ringBufferSize, 0);
    registerNotificationClient();
}

WasapiLoopbackCapture::~WasapiLoopbackCapture()
{
    stopCapture();
    unregisterNotificationClient();
}

void WasapiLoopbackCapture::registerNotificationClient()
{
    ComScope com;
    if (! com.ok)
        return;

    IMMDeviceEnumerator* enumeratorPtr = nullptr;
    HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, __uuidof (IMMDeviceEnumerator),
                                   (void**) &enumeratorPtr);
    if (FAILED (hr) || enumeratorPtr == nullptr)
        return;

    auto* client = new DeviceNotificationClient (*this);
    hr = enumeratorPtr->RegisterEndpointNotificationCallback (client);

    if (SUCCEEDED (hr))
    {
        deviceEnumerator = enumeratorPtr;
        notificationClient = client;
    }
    else
    {
        client->Release();
        enumeratorPtr->Release();
    }
}

void WasapiLoopbackCapture::unregisterNotificationClient()
{
    if (deviceEnumerator != nullptr && notificationClient != nullptr)
    {
        auto* enumeratorPtr = static_cast<IMMDeviceEnumerator*> (deviceEnumerator);
        auto* client = static_cast<IMMNotificationClient*> (notificationClient);
        enumeratorPtr->UnregisterEndpointNotificationCallback (client);
        client->Release();
        enumeratorPtr->Release();
        deviceEnumerator = nullptr;
        notificationClient = nullptr;
    }
}

void WasapiLoopbackCapture::setDeviceId (const juce::String& deviceId)
{
    {
        const juce::ScopedLock sl (deviceIdLock);
        if (selectedDeviceId == deviceId)
            return;
        selectedDeviceId = deviceId;
    }
    deviceChangeRequest.store (true, std::memory_order_release);
}

juce::String WasapiLoopbackCapture::getDeviceId() const
{
    const juce::ScopedLock sl (deviceIdLock);
    return selectedDeviceId;
}

bool WasapiLoopbackCapture::hasDeviceListChanged()
{
    return deviceListChanged.exchange (false, std::memory_order_acq_rel);
}

std::vector<WasapiLoopbackCapture::DeviceInfo> WasapiLoopbackCapture::enumerateDevices()
{
    std::vector<DeviceInfo> result;

    ComScope com;
    if (! com.ok)
        return result;

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, __uuidof (IMMDeviceEnumerator),
                                   (void**) &enumerator);
    if (FAILED (hr) || ! enumerator)
        return result;

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED (hr) || ! collection)
        return result;

    UINT count = 0;
    collection->GetCount (&count);

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        hr = collection->Item (i, &device);
        if (FAILED (hr) || ! device)
            continue;

        // Get device ID
        LPWSTR idStr = nullptr;
        hr = device->GetId (&idStr);
        if (FAILED (hr) || idStr == nullptr)
            continue;

        juce::String id (idStr);
        CoTaskMemFree (idStr);

        // Get friendly name
        ComPtr<IPropertyStore> props;
        hr = device->OpenPropertyStore (STGM_READ, &props);
        juce::String name = id; // fallback

        if (SUCCEEDED (hr) && props)
        {
            PROPVARIANT varName;
            PropVariantInit (&varName);

            hr = props->GetValue (PKEY_Device_FriendlyName, &varName);
            if (SUCCEEDED (hr) && varName.vt == VT_LPWSTR && varName.pwszVal != nullptr)
                name = juce::String (varName.pwszVal);

            PropVariantClear (&varName);
        }

        result.push_back ({ id, name });
    }

    return result;
}

void WasapiLoopbackCapture::startCapture()
{
    if (isThreadRunning())
        return;

    fifo.reset();
    status.store (Status::Connecting, std::memory_order_release);
    startThread (juce::Thread::Priority::highest);
}

void WasapiLoopbackCapture::stopCapture()
{
    signalThreadShouldExit();
    stopThread (2000); // 2s timeout — don't freeze the DAW
    status.store (Status::Disconnected, std::memory_order_release);
    capturedSampleRate.store (0, std::memory_order_release);
    capturedChannels.store (0, std::memory_order_release);
}

int WasapiLoopbackCapture::readSamples (int16_t* dest, int maxSamples)
{
    int ready = fifo.getNumReady();
    int toRead = juce::jmin (ready, maxSamples);
    if (toRead <= 0)
        return 0;

    int start1, size1, start2, size2;
    fifo.prepareToRead (toRead, start1, size1, start2, size2);

    if (size1 > 0)
        std::memcpy (dest, ringBuffer.data() + start1, (size_t) size1 * sizeof (int16_t));
    if (size2 > 0)
        std::memcpy (dest + size1, ringBuffer.data() + start2, (size_t) size2 * sizeof (int16_t));

    fifo.finishedRead (size1 + size2);
    return size1 + size2;
}

void WasapiLoopbackCapture::run()
{
    // All COM objects are created and released within this thread scope
    ComScope com;
    if (! com.ok)
    {
        status.store (Status::Error, std::memory_order_release);
        return;
    }

    // AVRT priority — RAII will revert on any exit path
    AvrtScope avrt;

    // Outer loop: reconnects when device errors out or device change requested
    while (! threadShouldExit())
    {
        deviceChangeRequest.store (false, std::memory_order_release);

        bool success = initAndCapture();

        if (threadShouldExit())
            break;

        if (! success)
        {
            // Wait ~1 second before retry, checking for exit
            status.store (Status::Connecting, std::memory_order_release);
            capturedSampleRate.store (0, std::memory_order_release);
            capturedChannels.store (0, std::memory_order_release);

            for (int i = 0; i < 200 && ! threadShouldExit(); ++i)
                Thread::sleep (5);
        }
    }
}

bool WasapiLoopbackCapture::initAndCapture()
{
    // Get the selected or default audio render endpoint
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, __uuidof (IMMDeviceEnumerator),
                                   (void**) &enumerator);
    if (FAILED (hr) || ! enumerator)
        return false;

    ComPtr<IMMDevice> device;

    // Try selected device first, fall back to default
    {
        const juce::ScopedLock sl (deviceIdLock);
        if (selectedDeviceId.isNotEmpty())
        {
            hr = enumerator->GetDevice (selectedDeviceId.toWideCharPointer(), &device);
            if (FAILED (hr) || ! device)
                device.ptr = nullptr; // fall through to default
        }
    }

    if (! device)
    {
        hr = enumerator->GetDefaultAudioEndpoint (eRender, eConsole, &device);
        if (FAILED (hr) || ! device)
            return false;
    }

    ComPtr<IAudioClient> audioClient;
    hr = device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &audioClient);
    if (FAILED (hr) || ! audioClient)
        return false;

    // Discover the device's mix format
    WAVEFORMATEX* mixFormat = nullptr;
    hr = audioClient->GetMixFormat (&mixFormat);
    if (FAILED (hr) || mixFormat == nullptr)
        return false;

    int deviceSampleRate = (int) mixFormat->nSamplesPerSec;
    int deviceChannels = (int) mixFormat->nChannels;

    // Determine if the format is IEEE float
    bool isFloat = false;
    if (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        isFloat = true;
    }
    else if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mixFormat->cbSize >= 22)
    {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*> (mixFormat);
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            isFloat = true;
    }

    capturedSampleRate.store (deviceSampleRate, std::memory_order_release);
    capturedChannels.store (deviceChannels, std::memory_order_release);

    // Create event for event-driven capture (no polling, instant wake on exit)
    HANDLE captureEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    if (captureEvent == nullptr)
        return false;

    // Initialize in shared mode with loopback + event-driven flags, 50ms buffer
    REFERENCE_TIME bufferDuration = 500000; // 50ms in 100ns units
    hr = audioClient->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  bufferDuration, 0, mixFormat, nullptr);
    CoTaskMemFree (mixFormat);
    mixFormat = nullptr;

    if (FAILED (hr))
    {
        CloseHandle (captureEvent);
        return false;
    }

    hr = audioClient->SetEventHandle (captureEvent);
    if (FAILED (hr))
    {
        CloseHandle (captureEvent);
        return false;
    }

    ComPtr<IAudioCaptureClient> captureClient;
    hr = audioClient->GetService (__uuidof (IAudioCaptureClient), (void**) &captureClient);
    if (FAILED (hr) || ! captureClient)
    {
        CloseHandle (captureEvent);
        return false;
    }

    hr = audioClient->Start();
    if (FAILED (hr))
    {
        CloseHandle (captureEvent);
        return false;
    }

    bool started = true;
    status.store (Status::Connected, std::memory_order_release);

    // Reset ring buffer so stale data from a previous session isn't used
    fifo.reset();

    // Capture loop — event-driven, no polling
    while (! threadShouldExit() && ! deviceChangeRequest.load (std::memory_order_acquire))
    {
        // Wait for WASAPI to signal data ready, with 100ms timeout to check exit flag
        DWORD waitResult = WaitForSingleObject (captureEvent, 100);

        if (threadShouldExit() || deviceChangeRequest.load (std::memory_order_acquire))
            break;

        if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_TIMEOUT)
            break; // unexpected error

        // Drain all available packets
        UINT32 packetLength = 0;
        hr = captureClient->GetNextPacketSize (&packetLength);

        if (FAILED (hr))
            break; // device lost — return false to trigger reconnect

        while (packetLength > 0)
        {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            hr = captureClient->GetBuffer (&data, &numFrames, &flags, nullptr, nullptr);
            if (FAILED (hr))
                break;

            int samplesToWrite = (int) numFrames;
            int freeSpace = fifo.getFreeSpace();
            if (samplesToWrite > freeSpace)
                samplesToWrite = freeSpace; // drop oldest implicitly by not writing excess

            if (samplesToWrite > 0 && ! (flags & AUDCLNT_BUFFERFLAGS_SILENT))
            {
                int start1, size1, start2, size2;
                fifo.prepareToWrite (samplesToWrite, start1, size1, start2, size2);

                auto writeMono = [&] (int16_t* dest, int offset, int count)
                {
                    if (isFloat)
                    {
                        auto* src = reinterpret_cast<const float*> (data);
                        for (int i = 0; i < count; ++i)
                        {
                            int srcFrame = offset + i;
                            float mono = 0.0f;
                            for (int ch = 0; ch < deviceChannels; ++ch)
                                mono += src[srcFrame * deviceChannels + ch];
                            mono /= (float) deviceChannels;

                            // Clamp and convert to int16
                            if (mono > 1.0f) mono = 1.0f;
                            if (mono < -1.0f) mono = -1.0f;
                            dest[i] = (int16_t) (mono * 32767.0f);
                        }
                    }
                    else // 16-bit PCM fallback
                    {
                        auto* src = reinterpret_cast<const int16_t*> (data);
                        for (int i = 0; i < count; ++i)
                        {
                            int srcFrame = offset + i;
                            int32_t mono = 0;
                            for (int ch = 0; ch < deviceChannels; ++ch)
                                mono += src[srcFrame * deviceChannels + ch];
                            dest[i] = (int16_t) (mono / deviceChannels);
                        }
                    }
                };

                if (size1 > 0) writeMono (ringBuffer.data() + start1, 0, size1);
                if (size2 > 0) writeMono (ringBuffer.data() + start2, size1, size2);

                fifo.finishedWrite (size1 + size2);
            }
            else if (samplesToWrite > 0) // silent buffer — write zeros
            {
                int start1, size1, start2, size2;
                fifo.prepareToWrite (samplesToWrite, start1, size1, start2, size2);

                if (size1 > 0) std::memset (ringBuffer.data() + start1, 0, (size_t) size1 * sizeof (int16_t));
                if (size2 > 0) std::memset (ringBuffer.data() + start2, 0, (size_t) size2 * sizeof (int16_t));

                fifo.finishedWrite (size1 + size2);
            }

            captureClient->ReleaseBuffer (numFrames);

            hr = captureClient->GetNextPacketSize (&packetLength);
            if (FAILED (hr))
                break;
        }

        if (FAILED (hr))
            break; // device error — break to reconnect
    }

    // Stop capture safely — only if we successfully started
    if (started)
        audioClient->Stop();

    CloseHandle (captureEvent);

    // If device change was requested, return true (clean exit, immediate reconnect)
    if (deviceChangeRequest.load (std::memory_order_acquire))
        return true;

    // COM pointers released by RAII destructors when method returns
    return threadShouldExit(); // true = clean exit, false = error (will retry)
}
