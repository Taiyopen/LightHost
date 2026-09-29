#pragma once

#include <JuceHeader.h>
#include "../dsp/AecProcessor.h"

/** 擷取指定喇叭的 WASAPI loopback 作為 AEC 參考訊號 */
class LoopbackCapture
{
public:
    LoopbackCapture() = default;
    ~LoopbackCapture();

    void setReferenceConsumer (AecProcessor* processor);
    void setReferenceDeviceId (const juce::String& deviceId);
    void setReferenceGainDb (float gainDb);

    juce::String getReferenceDeviceId() const { return referenceDeviceId; }
    float getReferenceGainDb() const { return referenceGainDb; }

    void start();
    void stop();
    bool isRunning() const { return running.load(); }

private:
    std::atomic<AecProcessor*> consumer { nullptr };
    std::atomic<bool> running { false };
    std::atomic<bool> shouldStop { false };
    std::unique_ptr<juce::Thread> captureThread;

    juce::String referenceDeviceId;
    float referenceGainDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopbackCapture)
};
