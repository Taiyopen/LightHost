#include "ExternalDevice.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <functiondiscoverykeys_devpkey.h>
 #include <ksmedia.h>
 #include <shellapi.h>

namespace
{
    /** 只在本次呼叫真正初始化 COM 時才 Uninitialize */
    struct ComScope
    {
        explicit ComScope (DWORD model) { hr = CoInitializeEx (nullptr, model); }
        ~ComScope() { if (hr == S_OK || hr == S_FALSE) CoUninitialize(); }
        bool isReady() const noexcept { return hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE; }
        HRESULT hr = E_FAIL;
    };

    template <typename T>
    struct ComPtr
    {
        T* p = nullptr;
        ComPtr() = default;
        ComPtr (const ComPtr&) = delete;
        ~ComPtr() { reset(); }
        void reset() { if (p != nullptr) { p->Release(); p = nullptr; } }
        T** operator&() { reset(); return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    struct SampleFormat
    {
        int rate = 0, channels = 0, bits = 0, blockAlign = 0;
        bool isFloat = false;
    };

    bool parseFormat (const WAVEFORMATEX* wf, SampleFormat& f)
    {
        if (wf == nullptr)
            return false;

        f.rate = (int) wf->nSamplesPerSec;
        f.channels = (int) wf->nChannels;
        f.bits = (int) wf->wBitsPerSample;
        f.blockAlign = (int) wf->nBlockAlign;
        f.isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;

        if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*> (wf);

            if (IsEqualGUID (ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
                f.isFloat = true;
            else if (! IsEqualGUID (ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
                return false;
        }
        else if (wf->wFormatTag != WAVE_FORMAT_PCM && wf->wFormatTag != WAVE_FORMAT_IEEE_FLOAT)
        {
            return false;
        }

        if (f.isFloat)
            return f.bits == 32;

        return f.bits == 16 || f.bits == 24 || f.bits == 32;
    }

    /** 裝置原生格式 → float（交錯） */
    void toFloat (const BYTE* src, float* dst, int numSamples, const SampleFormat& f)
    {
        if (f.isFloat)
        {
            std::memcpy (dst, src, (size_t) numSamples * sizeof (float));
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (f.bits == 16)
                dst[i] = (float) reinterpret_cast<const int16_t*> (src)[i] / 32768.0f;
            else if (f.bits == 24)
            {
                const BYTE* s = src + i * 3;
                const int32_t v = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8;
                dst[i] = (float) v / 8388608.0f;
            }
            else
                dst[i] = (float) ((double) reinterpret_cast<const int32_t*> (src)[i] / 2147483648.0);
        }
    }

    /** float（交錯）→ 裝置原生格式 */
    void fromFloat (const float* src, BYTE* dst, int numSamples, const SampleFormat& f)
    {
        if (f.isFloat)
        {
            std::memcpy (dst, src, (size_t) numSamples * sizeof (float));
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const double v = juce::jlimit (-1.0, 1.0, (double) src[i]);

            if (f.bits == 16)
                reinterpret_cast<int16_t*> (dst)[i] = (int16_t) juce::roundToInt (v * 32767.0);
            else if (f.bits == 24)
            {
                const int32_t s = (int32_t) juce::roundToInt (v * 8388607.0);
                dst[i * 3] = (BYTE) (s & 0xff);
                dst[i * 3 + 1] = (BYTE) ((s >> 8) & 0xff);
                dst[i * 3 + 2] = (BYTE) ((s >> 16) & 0xff);
            }
            else
                reinterpret_cast<int32_t*> (dst)[i] = (int32_t) juce::jlimit (-2147483647.0, 2147483647.0, v * 2147483647.0);
        }
    }

    juce::String friendlyNameOf (IMMDevice* device)
    {
        juce::String result;
        ComPtr<IPropertyStore> props;

        if (SUCCEEDED (device->OpenPropertyStore (STGM_READ, &props)))
        {
            PROPVARIANT value;
            PropVariantInit (&value);

            if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr)
                result = juce::String (value.pwszVal);

            PropVariantClear (&value);
        }

        return result;
    }
}

juce::Array<WasapiEndpointInfo> enumerateWasapiEndpoints (bool inputs)
{
    juce::Array<WasapiEndpointInfo> result;
    const ComScope com (COINIT_APARTMENTTHREADED);

    if (! com.isReady())
        return result;

    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> collection;

    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator),
                                  reinterpret_cast<void**> (&enumerator)))
        || FAILED (enumerator->EnumAudioEndpoints (inputs ? eCapture : eRender, DEVICE_STATE_ACTIVE, &collection)))
        return result;

    UINT count = 0;
    collection->GetCount (&count);

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;

        if (FAILED (collection->Item (i, &device)))
            continue;

        WasapiEndpointInfo info;
        info.isInput = inputs;

        LPWSTR id = nullptr;
        if (SUCCEEDED (device->GetId (&id)) && id != nullptr)
        {
            info.id = juce::String (id);
            CoTaskMemFree (id);
        }

        info.name = friendlyNameOf (device.p);
        if (info.name.isEmpty())
            info.name = info.id;

        // 順便讀出 Windows 設定的格式，清單上就能顯示
        ComPtr<IAudioClient> client;
        if (SUCCEEDED (device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**> (&client))))
        {
            WAVEFORMATEX* mix = nullptr;
            if (SUCCEEDED (client->GetMixFormat (&mix)) && mix != nullptr)
            {
                info.sampleRate = (int) mix->nSamplesPerSec;
                info.numChannels = (int) mix->nChannels;

                // 只查詢低延遲共用模式能用的週期，不開串流
                ComPtr<IAudioClient3> client3;
                UINT32 defaultFrames = 0, fundamentalFrames = 0, minFrames = 0, maxFrames = 0;

                if (SUCCEEDED (device->Activate (__uuidof (IAudioClient3), CLSCTX_ALL, nullptr, reinterpret_cast<void**> (&client3)))
                    && SUCCEEDED (client3->GetSharedModeEnginePeriod (mix, &defaultFrames, &fundamentalFrames, &minFrames, &maxFrames))
                    && info.sampleRate > 0)
                {
                    info.defaultPeriodMs = 1000.0 * defaultFrames / info.sampleRate;
                    info.minLowLatencyPeriodMs = 1000.0 * minFrames / info.sampleRate;
                }

                CoTaskMemFree (mix);
            }
        }

        result.add (info);
    }

    return result;
}

void openWindowsSoundSettings()
{
    ShellExecuteW (nullptr, L"open", L"control.exe", L"mmsys.cpl", nullptr, SW_SHOWNORMAL);
}

//==============================================================================
struct ExternalDevice::Impl
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient> render;
    HANDLE event = nullptr;
    SampleFormat format;
    UINT32 bufferFrames = 0;
    std::vector<float> scratch;

    ~Impl()
    {
        if (event != nullptr)
            CloseHandle (event);
    }
};

ExternalDevice::ExternalDevice (int uidIn, const juce::String& endpointIdIn, const juce::String& nameIn, bool isInput,
                                bool lowLatencyIn, double safetyPeriodsIn)
    : juce::Thread ("ExternalDevice " + nameIn), uid (uidIn), endpointId (endpointIdIn), name (nameIn), input (isInput),
      lowLatency (lowLatencyIn)
{
    safetyPeriods.store (safetyPeriodsIn);

    // 開裝置、讀格式都在自己的執行緒上做（WASAPI 物件留在同一個 COM 環境），這裡等它回報
    startThread (juce::Thread::Priority::highest);

    for (int waited = 0; waited < 3000 && state.load() != State::running && getError().isEmpty(); waited += 10)
        juce::Thread::sleep (10);

    if (state.load() != State::running && getError().isEmpty())
        fail ("The device did not start in time.");
}

ExternalDevice::~ExternalDevice()
{
    stopThread (3000);
}

juce::String ExternalDevice::getError() const
{
    const juce::ScopedLock sl (errorLock);
    return error;
}

void ExternalDevice::fail (const juce::String& message)
{
    {
        const juce::ScopedLock sl (errorLock);
        error = message;
    }

    state.store (State::error);
}

void ExternalDevice::setMasterFormat (double rate, int blockSize)
{
    masterBlock.store (blockSize);
    masterRate.store (rate);
}

void ExternalDevice::run()
{
    // WASAPI 物件要在同一條執行緒、COM 還在時釋放
    const ComScope com (COINIT_MULTITHREADED);
    impl = std::make_unique<Impl>();
    stream();
    impl.reset();
}

void ExternalDevice::stream()
{
    auto& d = *impl;

    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator),
                                  reinterpret_cast<void**> (&d.enumerator)))
        || FAILED (d.enumerator->GetDevice (endpointId.toWideCharPointer(), &d.device))
        || FAILED (d.device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**> (&d.client))))
    {
        fail ("Device not found (unplugged or disabled?).");
        return;
    }

    WAVEFORMATEX* mix = nullptr;

    if (FAILED (d.client->GetMixFormat (&mix)) || ! parseFormat (mix, d.format))
    {
        if (mix != nullptr) CoTaskMemFree (mix);
        fail ("Unsupported audio format.");
        return;
    }

    HRESULT hr = E_FAIL;

    // 低延遲共用模式（Windows 10 起）：驅動允許的話用最短的引擎週期；不支援就退回一般模式
    if (lowLatency)
    {
        ComPtr<IAudioClient3> client3;
        UINT32 defaultFrames = 0, fundamentalFrames = 0, minFrames = 0, maxFrames = 0;

        if (SUCCEEDED (d.device->Activate (__uuidof (IAudioClient3), CLSCTX_ALL, nullptr, reinterpret_cast<void**> (&client3)))
            && SUCCEEDED (client3->GetSharedModeEnginePeriod (mix, &defaultFrames, &fundamentalFrames, &minFrames, &maxFrames))
            && minFrames > 0 && minFrames < defaultFrames
            && SUCCEEDED (client3->InitializeSharedAudioStream (AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minFrames, mix, nullptr)))
        {
            d.client.reset();
            d.client.p = client3.p;   // IAudioClient3 也是 IAudioClient，接手它的參照
            client3.p = nullptr;
            devicePeriodSeconds = (double) minFrames / (double) d.format.rate;
            lowLatencyActive = true;
            hr = S_OK;
        }
    }

    if (! lowLatencyActive)
    {
        REFERENCE_TIME defaultPeriod = 100000, minPeriod = 0;
        d.client->GetDevicePeriod (&defaultPeriod, &minPeriod);
        devicePeriodSeconds = (double) defaultPeriod / 1.0e7;

        // 一般共用模式、事件驅動；緩衝 2 個週期
        hr = d.client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                   defaultPeriod * 2, 0, mix, nullptr);
    }

    CoTaskMemFree (mix);

    d.event = CreateEventW (nullptr, FALSE, FALSE, nullptr);

    if (FAILED (hr) || d.event == nullptr || FAILED (d.client->SetEventHandle (d.event))
        || FAILED (d.client->GetBufferSize (&d.bufferFrames)))
    {
        fail ("Could not open the device (in use by another program in exclusive mode?).");
        return;
    }

    if (input ? FAILED (d.client->GetService (__uuidof (IAudioCaptureClient), reinterpret_cast<void**> (&d.capture)))
              : FAILED (d.client->GetService (__uuidof (IAudioRenderClient), reinterpret_cast<void**> (&d.render))))
    {
        fail ("Could not open the device stream.");
        return;
    }

    numChannels = d.format.channels;
    d.scratch.assign ((size_t) d.bufferFrames * (size_t) numChannels, 0.0f);
    // 一秒的緩衝：輸入存裝置取樣率的樣本，輸出存主裝置取樣率的樣本（最高 192 kHz）
    bridge.prepare (numChannels, input ? d.format.rate : 192000);

    if (! input)
    {
        // 輸出要先填滿一次靜音才能開始
        BYTE* data = nullptr;
        if (SUCCEEDED (d.render->GetBuffer (d.bufferFrames, &data)))
            d.render->ReleaseBuffer (d.bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    }

    if (FAILED (d.client->Start()))
    {
        fail ("Could not start the device.");
        return;
    }

    sampleRate = d.format.rate;   // 建構子等的就是這個
    state.store (State::running);

    double configuredMasterRate = 0.0;
    int configuredMasterBlock = 0;
    double configuredSafety = 0.0;

    while (! threadShouldExit())
    {
        if (WaitForSingleObject (d.event, 200) != WAIT_OBJECT_0)
            continue;

        if (input)
        {
            UINT32 packet = 0;

            if (FAILED (d.capture->GetNextPacketSize (&packet)))
            {
                fail ("The device stopped (unplugged?).");
                return;
            }

            while (packet > 0)
            {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;

                if (FAILED (d.capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr)))
                {
                    fail ("The device stopped (unplugged?).");
                    return;
                }

                frames = juce::jmin (frames, d.bufferFrames);

                // 標成靜音的封包也送同長度的靜音，時間軸才不會有缺口
                if (data == nullptr || (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
                    bridge.writeSilence ((int) frames);
                else
                {
                    toFloat (data, d.scratch.data(), (int) frames * numChannels, d.format);
                    bridge.writeInterleaved (d.scratch.data(), (int) frames);
                }

                d.capture->ReleaseBuffer (frames);

                if (FAILED (d.capture->GetNextPacketSize (&packet)))
                    packet = 0;
            }
        }
        else
        {
            // 主裝置取樣率或區塊大小變了：重新設定讀取端（目標緩衝 = 主裝置一個區塊＋本裝置 1.5 個週期＋3 ms）
            const double mRate = masterRate.load();
            const int mBlock = masterBlock.load();
            const double safety = safetyPeriods.load();

            if (mRate > 0.0 && (mRate != configuredMasterRate || mBlock != configuredMasterBlock || safety != configuredSafety))
            {
                const int target = (int) (mBlock + mRate * (devicePeriodSeconds * safety + 0.003));
                bridge.configureReader (mRate, d.format.rate, target);
                targetSeconds.store (target / mRate);
                configuredMasterRate = mRate;
                configuredMasterBlock = mBlock;
                configuredSafety = safety;
            }

            UINT32 padding = 0;
            if (FAILED (d.client->GetCurrentPadding (&padding)))
            {
                fail ("The device stopped (unplugged?).");
                return;
            }

            const UINT32 frames = d.bufferFrames - padding;
            BYTE* data = nullptr;

            if (frames > 0 && SUCCEEDED (d.render->GetBuffer (frames, &data)))
            {
                if (configuredMasterRate > 0.0)
                    bridge.readInterleaved (d.scratch.data(), (int) frames);
                else
                    std::fill (d.scratch.begin(), d.scratch.begin() + (size_t) frames * (size_t) numChannels, 0.0f);

                fromFloat (d.scratch.data(), data, (int) frames * numChannels, d.format);
                d.render->ReleaseBuffer (frames, 0);
            }
        }
    }

    d.client->Stop();
}

#else

juce::Array<WasapiEndpointInfo> enumerateWasapiEndpoints (bool) { return {}; }
void openWindowsSoundSettings() {}

struct ExternalDevice::Impl {};
ExternalDevice::ExternalDevice (int uidIn, const juce::String& id, const juce::String& n, bool isInput, bool lowLatencyIn, double)
    : juce::Thread ("ExternalDevice"), uid (uidIn), endpointId (id), name (n), input (isInput), lowLatency (lowLatencyIn) { fail ("WASAPI is only available on Windows."); }
ExternalDevice::~ExternalDevice() = default;
juce::String ExternalDevice::getError() const { const juce::ScopedLock sl (errorLock); return error; }
void ExternalDevice::fail (const juce::String& m) { const juce::ScopedLock sl (errorLock); error = m; state.store (State::error); }
void ExternalDevice::setMasterFormat (double, int) {}
void ExternalDevice::run() {}
void ExternalDevice::stream() {}

#endif
