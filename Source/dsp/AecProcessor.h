#pragma once

#include <JuceHeader.h>
#include "AecStats.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace webrtc
{
class EchoCanceller3Config;
}

/** 以 WebRTC AEC3 做回音消除；參考訊號來自喇叭 loopback */
class AecProcessor : public juce::AudioProcessor
{
public:
    AecProcessor();
    ~AecProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    const juce::String getName() const override { return "Echo Cancellation (AEC3)"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 0; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    /** 由 LoopbackCapture 或 OutputReferenceTap 呼叫，餵入喇叭參考訊號 */
    void pushReference (const float* samples, int numSamples);

    /** AEC 強度 25–150（100 = WebRTC 預設）。可在 UI 執行緒呼叫，會在背後換一組新引擎 */
    void setStrength (float strengthPercent);
    float getStrength() const;

    AecProcessorStats getStats() const;

private:
    /** 一組 AEC3 物件與音訊執行緒用的暫存區；整組建好後才換上，音訊執行緒不會看到建到一半的狀態 */
    struct Engine;

    static webrtc::EchoCanceller3Config makeAecConfig (float strengthPercent);
    static constexpr int referenceRingSize = 65536;
    static constexpr int numChannels = 1;

    static int pickProcessingSampleRate (int hostSampleRate);
    std::unique_ptr<Engine> createEngine() const;
    int measureEngineLatency();
    void installEngine (std::unique_ptr<Engine> newEngine);
    void resetReferenceReader (int writeIndex);
    bool readReferenceFrame (float* dest, int numSamples);
    void feedRenderFrame (Engine& engine, const float* frame);
    void processCaptureFrame (Engine& engine, const float* micFrame, float* outputFrame);
    void processAlignedFramePair (Engine& engine, const float* refHostFrame, const float* micHostFrame);
    void updateLevelDb (std::atomic<float>& target, float blockRms);
    void updateAttenuationDb (std::atomic<float>& target, float attenuationDb);
    void updateSmoothedDb (std::atomic<float>& target, float db);
    int getReferenceLeadSamples() const;
    int getReferenceSlackSamples() const;
    int getMaxAllowedReferenceLeadSamples() const;

    // 以下只在 prepareToPlay 改動
    double hostSampleRate = 44100.0;
    int blockSize = 512;
    int processingSampleRate = 48000;
    int frameSize = 480;
    int hostFrameSize = 480;
    int referenceDelaySamples = 512;
    bool resampling = false;
    std::atomic<bool> prepared { false };

    int lastReferenceLeadForAec = 0;

    // 參考訊號的讀取端（只在音訊執行緒使用）：可落在樣本之間的讀取位置，用來追蹤兩邊時脈
    double referenceReadPosition = 0.0;
    double smoothedReferenceLead = 0.0;
    double referenceDriftIntegral = 0.0;
    int lastStoredReadPos = -1;
    bool referenceStarved = false;   // 喇叭參考斷了，正在等新資料累積回目標差距
    std::atomic<float> referenceDriftPpm { 0.0f };
    std::atomic<float> strengthPercent { 100.0f };

    // 音訊執行緒只用 try_lock 拿這把鎖；拿不到就讓該段原音通過
    std::unique_ptr<Engine> engine;
    std::mutex engineMutex;

    std::vector<float> referenceRing;
    std::atomic<int> referenceWritePos { 0 };
    std::atomic<int> referenceReadPos  { 0 };

    std::atomic<float> referenceLevelDb { -100.0f };
    std::atomic<float> micRawLevelDb { -100.0f };
    std::atomic<float> micLevelDb { -100.0f };
    std::atomic<float> outputLevelDb { -100.0f };
    std::atomic<float> echoRemovedDb { 0.0f };
    std::atomic<float> erleDb { 0.0f };
    std::atomic<juce::int64> referenceSamplesReceived { 0 };
    std::atomic<juce::int64> samplesProcessed { 0 };
    std::atomic<juce::int64> referenceUnderruns { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AecProcessor)
};
