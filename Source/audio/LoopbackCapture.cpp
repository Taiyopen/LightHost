#include "LoopbackCapture.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <avrt.h>
#endif

LoopbackCapture::~LoopbackCapture()
{
    stop();
}

void LoopbackCapture::setReferenceConsumer (AecProcessor* processor)
{
    consumer.store (processor);
}

void LoopbackCapture::setReferenceDeviceId (const juce::String& deviceId)
{
    referenceDeviceId = deviceId;
}

void LoopbackCapture::setReferenceGainDb (float gainDb)
{
    referenceGainDb = gainDb;
}

void LoopbackCapture::stop()
{
    shouldStop.store (true);
    consumer.store (nullptr);

    if (captureThread != nullptr)
    {
        captureThread->stopThread (2000);
        captureThread.reset();
    }

    running.store (false);
}

#if JUCE_WINDOWS

class LoopbackThread : public juce::Thread
{
public:
    LoopbackThread (std::atomic<bool>& stopFlag,
                    std::atomic<AecProcessor*>& consumerRef,
                    juce::String deviceId,
                    float gainDb)
        : juce::Thread ("LoopbackCapture"),
          stopFlag (stopFlag),
          consumerRef (consumerRef),
          deviceId (std::move (deviceId)),
          gain (juce::Decibels::decibelsToGain (gainDb))
    {
    }

    void run() override
    {
        CoInitializeEx (nullptr, COINIT_MULTITHREADED);

        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioClient* audioClient = nullptr;
        IAudioCaptureClient* captureClient = nullptr;
        WAVEFORMATEX* format = nullptr;

        auto cleanup = [&]
        {
            if (captureClient != nullptr) captureClient->Release();
            if (audioClient != nullptr) audioClient->Release();
            if (device != nullptr) device->Release();
            if (enumerator != nullptr) enumerator->Release();
            if (format != nullptr) CoTaskMemFree (format);
            CoUninitialize();
        };

        if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof (IMMDeviceEnumerator),
                                      reinterpret_cast<void**> (&enumerator))))
        {
            cleanup();
            return;
        }

        const bool useDefault = deviceId.isEmpty();
        HRESULT endpointResult = E_FAIL;

        if (useDefault)
            endpointResult = enumerator->GetDefaultAudioEndpoint (eRender, eConsole, &device);
        else
            endpointResult = enumerator->GetDevice (deviceId.toWideCharPointer(), &device);

        if (FAILED (endpointResult) || device == nullptr)
        {
            cleanup();
            return;
        }

        if (FAILED (device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**> (&audioClient))))
        {
            cleanup();
            return;
        }

        if (FAILED (audioClient->GetMixFormat (&format)))
        {
            cleanup();
            return;
        }

        const REFERENCE_TIME bufferDuration = 10000000;
        if (FAILED (audioClient->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                             AUDCLNT_STREAMFLAGS_LOOPBACK,
                                             bufferDuration, 0, format, nullptr)))
        {
            cleanup();
            return;
        }

        if (FAILED (audioClient->GetService (__uuidof (IAudioCaptureClient),
                                              reinterpret_cast<void**> (&captureClient))))
        {
            cleanup();
            return;
        }

        if (FAILED (audioClient->Start()))
        {
            cleanup();
            return;
        }

        const bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
                          || (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->wBitsPerSample == 32);
        const int numChannels = juce::jmax (1, static_cast<int> (format->nChannels));
        std::vector<float> monoBuffer (4096);

        while (! stopFlag.load() && ! threadShouldExit())
        {
            UINT32 packetLength = 0;
            if (FAILED (captureClient->GetNextPacketSize (&packetLength)) || packetLength == 0)
            {
                sleep (5);
                continue;
            }

            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            if (FAILED (captureClient->GetBuffer (&data, &numFrames, &flags, nullptr, nullptr)))
            {
                sleep (5);
                continue;
            }

            if (numFrames > 0)
            {
                if (auto* aec = consumerRef.load())
                {
                    monoBuffer.resize (static_cast<size_t> (numFrames));

                    // 標成靜音的封包也要送同樣長度的靜音，參考訊號的時間軸才不會出現缺口
                    if (data == nullptr || (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
                    {
                        std::fill (monoBuffer.begin(), monoBuffer.end(), 0.0f);
                    }
                    else if (isFloat)
                    {
                        const auto* interleaved = reinterpret_cast<const float*> (data);
                        for (UINT32 frame = 0; frame < numFrames; ++frame)
                        {
                            double sum = 0.0;
                            for (int ch = 0; ch < numChannels; ++ch)
                                sum += interleaved[frame * static_cast<UINT32> (numChannels) + static_cast<UINT32> (ch)];

                            monoBuffer[static_cast<size_t> (frame)] = static_cast<float> (sum / static_cast<double> (numChannels)) * gain;
                        }
                    }
                    else
                    {
                        const auto* interleaved = reinterpret_cast<const int16_t*> (data);
                        for (UINT32 frame = 0; frame < numFrames; ++frame)
                        {
                            double sum = 0.0;
                            for (int ch = 0; ch < numChannels; ++ch)
                            {
                                const int16_t sample = interleaved[frame * static_cast<UINT32> (numChannels) + static_cast<UINT32> (ch)];
                                sum += static_cast<double> (sample) / 32768.0;
                            }

                            monoBuffer[static_cast<size_t> (frame)] = static_cast<float> (sum / static_cast<double> (numChannels)) * gain;
                        }
                    }

                    aec->pushReference (monoBuffer.data(), static_cast<int> (numFrames));
                }
            }

            captureClient->ReleaseBuffer (numFrames);
        }

        audioClient->Stop();
        cleanup();
    }

private:
    std::atomic<bool>& stopFlag;
    std::atomic<AecProcessor*>& consumerRef;
    juce::String deviceId;
    float gain;
};

void LoopbackCapture::start()
{
    if (running.load())
        return;

    shouldStop.store (false);
    captureThread = std::make_unique<LoopbackThread> (shouldStop, consumer, referenceDeviceId, referenceGainDb);
    captureThread->startThread();
    running.store (true);
}

#else

void LoopbackCapture::start()
{
    juce::ignoreUnused (consumer, referenceDeviceId, referenceGainDb);
}

#endif
