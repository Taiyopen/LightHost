#pragma once

#include <JuceHeader.h>
#include "AecStats.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace webrtc
{
class AudioBuffer;
class EchoControl;
class EchoCanceller3Config;
class HighPassFilter;
class PushSincResampler;
class StreamConfig;
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

    /** AEC 強度 25–150（100 = WebRTC 預設） */
    void setStrength (float strengthPercent);
    float getStrength() const;

    AecProcessorStats getStats() const;

private:
    static webrtc::EchoCanceller3Config makeAecConfig (float strengthPercent);
    static constexpr int referenceRingSize = 65536;
    static constexpr int numChannels = 1;

    static int pickProcessingSampleRate (int hostSampleRate);
    void createAec3 (int processingRate);
    void destroyAec3();
    void resyncReferenceReadPointer();
    bool readReferenceFrame (float* dest, int numSamples);
    void feedRenderFrame (const float* frame, int frameSize);
    void processCaptureFrame (const float* micFrame, float* outputFrame, int frameSize);
    void processAlignedFramePair (const float* refHostFrame, const float* micHostFrame, int hostFrameSize);
    void updateLevelDb (std::atomic<float>& target, float blockRms);
    void updateAttenuationDb (std::atomic<float>& target, float attenuationDb);
    void updateSmoothedDb (std::atomic<float>& target, float db);
    int getReferenceLeadSamples() const;
    int getMaxAllowedReferenceLeadSamples() const;

    double hostSampleRate = 44100.0;
    int blockSize = 512;
    int processingSampleRate = 48000;
    int frameSize = 480;
    int hostFrameSize = 480;
    int referenceDelaySamples = 512;
    int lastReferenceLeadForAec = 0;
    bool resampling = false;
    float strengthPercent = 100.0f;

    std::unique_ptr<webrtc::EchoControl> echoController;
    std::unique_ptr<webrtc::HighPassFilter> hpFilter;
    std::unique_ptr<webrtc::AudioBuffer> renderBuffer;
    std::unique_ptr<webrtc::AudioBuffer> captureBuffer;
    std::unique_ptr<webrtc::PushSincResampler> refResampler;
    std::unique_ptr<webrtc::PushSincResampler> capInResampler;
    std::unique_ptr<webrtc::PushSincResampler> capOutResampler;

    std::vector<float> referenceRing;
    std::atomic<int> referenceWritePos { 0 };
    std::atomic<int> referenceReadPos  { 0 };

    std::vector<float> capHostPending;
    std::vector<float> capOutPending;
    std::vector<float> refHostFrameScratch;
    std::vector<float> refResampleScratch;
    std::vector<float> capResampleScratch;
    std::vector<float> capFrameScratch;
    std::vector<float> capOutResampleScratch;
    std::vector<float*> channelPtrScratch;

    std::mutex captureMutex;

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
