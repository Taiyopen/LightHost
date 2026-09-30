#pragma once

#include <JuceHeader.h>

/** 可插拔降噪演算法介面 — 在此實作你自己的演算法 */
class INoiseReducer
{
public:
    virtual ~INoiseReducer() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;

    /** 背景噪音最多壓低幾 dB（>= 100 代表不限制）。可在任何執行緒呼叫；不支援的演算法忽略 */
    virtual void setMaxAttenuationDb (float) {}

    /** prepare 之後，輸出比輸入晚多少秒（切片等待＋模型本身） */
    virtual double getLatencySeconds() const { return 0.0; }
};
