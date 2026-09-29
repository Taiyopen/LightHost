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
};
