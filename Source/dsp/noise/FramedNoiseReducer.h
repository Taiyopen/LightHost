#pragma once

#include "../INoiseReducer.h"
#include <atomic>
#include <memory>
#include <vector>

namespace webrtc { class PushSincResampler; }

/** 固定容量的單聲道 FIFO；prepare 後不再配置記憶體 */
class SampleFifo
{
public:
    void setCapacity (int capacity);
    void clear();

    int size() const  { return count; }
    void push (const float* data, int numSamples);
    void pushZeros (int numSamples);
    void pop (float* dest, int numSamples);

private:
    std::vector<float> buffer;
    int readPos = 0, writePos = 0, count = 0;
};

/**
 * 模型型降噪的共用外殼：雙聲道合成單聲道、主機與模型取樣率互轉（10 ms 為一段）、
 * 切成模型的固定長度（hop）、以及「最多壓幾 dB」。子類別只要處理一個 hop。
 */
class FramedNoiseReducer : public INoiseReducer
{
public:
    FramedNoiseReducer();
    ~FramedNoiseReducer() override;

    void prepare (double sampleRate, int maxBlockSize) final;
    void reset() final;
    void process (juce::AudioBuffer<float>& buffer) final;
    void setMaxAttenuationDb (float db) final { maxAttenuationDb.store (db); }
    double getLatencySeconds() const final;

protected:
    struct Format
    {
        int sampleRate = 48000;
        int hopSize = 480;
        int latencySamples = 0;               // 模型輸出比輸入晚幾個樣本（模型取樣率）；用來對齊混回的原音
        bool hasNativeAttenuationLimit = false;
    };

    virtual Format getFormat() const = 0;
    /** 在音訊執行緒上處理一個 hop（模型取樣率）；in、out 長度皆為 hopSize */
    virtual void processHop (const float* in, float* out) = 0;
    /** 清掉模型的歷史狀態；在音訊執行緒上呼叫，不可配置記憶體 */
    virtual void resetModel() = 0;
    /** hasNativeAttenuationLimit 為 true 時由音訊執行緒呼叫 */
    virtual void applyNativeAttenuationLimit (float) {}

private:
    void moveHostToModel();
    void runModel (float dryGain);
    void moveModelToHost();

    Format format;
    bool ready = false;
    bool resampling = false;
    int hostChunk = 0, modelChunk = 0;
    int primeSamples = 0;
    int hostRate = 0;

    std::unique_ptr<webrtc::PushSincResampler> toModel, toHost;
    SampleFifo hostIn, modelIn, modelOut, hostOut, dryDelay;
    std::vector<float> mono, hostChunkBuf, modelChunkBuf, hopIn, hopOut, hopDry;

    std::atomic<float> maxAttenuationDb { 100.0f };
    float appliedAttenuationDb = -1.0f;
};
