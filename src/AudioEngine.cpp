#include "AudioEngine.h"

#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {
constexpr REFERENCE_TIME kBufferDuration100ns = 200000;  // 20 ms.
constexpr int kNoDeviceChannels = 2;

// Follows the Windows default output, such as when headphones are plugged in.
class DeviceWatcher : public IMMNotificationClient {
public:
    explicit DeviceWatcher(std::atomic<bool>& reopen) : reopen_(reopen) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    // The engine owns this object and unregisters it before it goes away.
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) reopen_.store(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<bool>& reopen_;
};

int64_t qpcNow100ns() {
    static const int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return int64_t(double(counter.QuadPart) * 1e7 / double(frequency));
}
}  // namespace

double AudioEngine::nowSeconds() { return double(qpcNow100ns()) * 1e-7; }

AudioEngine::AudioEngine() { thread_ = std::thread([this] { threadMain(); }); }

AudioEngine::~AudioEngine() {
    quit_.store(true);
    thread_.join();
}

void AudioEngine::setConfig(const SequenceConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    pendingConfig_ = config;
    configDirty_ = true;
}

void AudioEngine::syncConfig() {
    // try_lock: the audio thread must never wait on the UI. A miss is picked up next block.
    if (!configMutex_.try_lock()) return;
    if (configDirty_) {
        sequencer_.setConfig(pendingConfig_);
        configDirty_ = false;
    }
    configMutex_.unlock();
}

void AudioEngine::publishClock(int64_t frames, int64_t qpc100ns) {
    clockSeq_.fetch_add(1);
    clockFrames_.store(frames);
    clockQpc100ns_.store(qpc100ns);
    clockRate_.store(sequencer_.sampleRate());
    clockSeq_.fetch_add(1);
}

AudioEngine::Clock AudioEngine::clock() const {
    uint32_t seq;
    int64_t frames, qpc;
    double rate;
    do {
        seq = clockSeq_.load();
        frames = clockFrames_.load();
        qpc = clockQpc100ns_.load();
        rate = clockRate_.load();
    } while ((seq & 1) || seq != clockSeq_.load());
    return {double(frames) + double(qpcNow100ns() - qpc) * 1e-7 * rate, rate};
}

void AudioEngine::threadMain() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    HANDLE wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    ComPtr<IMMDeviceEnumerator> enumerator;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    DeviceWatcher watcher(reopen_);
    if (enumerator) enumerator->RegisterEndpointNotificationCallback(&watcher);

    while (!quit_.load()) {
        reopen_.store(false);
        if (!enumerator || !runDevice(enumerator.Get(), wakeEvent)) runWithoutDevice();
    }

    if (enumerator) enumerator->UnregisterEndpointNotificationCallback(&watcher);
    enumerator.Reset();
    CloseHandle(wakeEvent);
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

// Returns false if the device could not be used, and true if it should simply be reopened.
bool AudioEngine::runDevice(void* enumeratorPtr, void* wakeEvent) {
    auto* enumerator = static_cast<IMMDeviceEnumerator*>(enumeratorPtr);
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> renderClient;
    ComPtr<IAudioClock> audioClock;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) return false;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) return false;

    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix))) return false;
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = mix->nChannels;
    format.Format.nSamplesPerSec = mix->nSamplesPerSec;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = WORD(mix->nChannels * 4);
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                               ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask
                               : (mix->nChannels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    CoTaskMemFree(mix);

    const int channels = format.Format.nChannels;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration100ns, 0,
                                  reinterpret_cast<WAVEFORMATEX*>(&format), nullptr)))
        return false;
    if (FAILED(client->SetEventHandle(static_cast<HANDLE>(wakeEvent)))) return false;
    UINT32 bufferFrames = 0;
    UINT64 clockFrequency = 0;
    if (FAILED(client->GetBufferSize(&bufferFrames))) return false;
    if (FAILED(client->GetService(IID_PPV_ARGS(&renderClient)))) return false;
    if (FAILED(client->GetService(IID_PPV_ARGS(&audioClock)))) return false;
    if (FAILED(audioClock->GetFrequency(&clockFrequency)) || clockFrequency == 0) return false;

    sequencer_.setSampleRate(double(format.Format.nSamplesPerSec));

    // The device counts from zero on each open. Offsetting it keeps stream time continuous across device changes.
    const int64_t baseFrames = sequencer_.position();
    const double framesPerClockUnit = double(format.Format.nSamplesPerSec) / double(clockFrequency);

    auto fill = [&](UINT32 frames) -> bool {
        BYTE* data = nullptr;
        if (FAILED(renderClient->GetBuffer(frames, &data))) return false;
        syncConfig();
        sequencer_.render(reinterpret_cast<float*>(data), int(frames), channels);
        return SUCCEEDED(renderClient->ReleaseBuffer(frames, 0));
    };

    if (!fill(bufferFrames)) return false;
    publishClock(baseFrames, qpcNow100ns());
    if (FAILED(client->Start())) return false;
    hasDevice_.store(true);

    bool keepGoing = true;
    while (keepGoing && !quit_.load() && !reopen_.load()) {
        if (WaitForSingleObject(static_cast<HANDLE>(wakeEvent), 200) != WAIT_OBJECT_0) {
            keepGoing = false;
            break;
        }
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) {
            keepGoing = false;
            break;
        }
        if (padding == 0) underruns_.fetch_add(1);
        const UINT32 available = bufferFrames - padding;
        if (available > 0 && !fill(available)) {
            keepGoing = false;
            break;
        }
        UINT64 position = 0, qpc = 0;
        if (SUCCEEDED(audioClock->GetPosition(&position, &qpc)))
            publishClock(baseFrames + int64_t(double(position) * framesPerClockUnit), int64_t(qpc));
    }
    client->Stop();
    hasDevice_.store(false);
    return keepGoing;
}

// With no usable output, time still passes silently so the visuals keep working. Retries shortly.
void AudioEngine::runWithoutDevice() {
    float scratch[480 * kNoDeviceChannels];
    const double rate = sequencer_.sampleRate();
    int64_t last = qpcNow100ns();
    double owed = 0;
    for (int i = 0; i < 50 && !quit_.load() && !reopen_.load(); ++i) {
        Sleep(5);
        const int64_t now = qpcNow100ns();
        owed += double(now - last) * 1e-7 * rate;
        last = now;
        syncConfig();
        while (owed >= 1.0) {
            const int frames = int(std::min(owed, 480.0));
            sequencer_.render(scratch, frames, kNoDeviceChannels);
            owed -= frames;
        }
        publishClock(sequencer_.position(), now);
    }
}
