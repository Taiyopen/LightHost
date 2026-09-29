#pragma once

#include "INoiseReducer.h"

/**
 * 在此實作你的降噪演算法。
 * 完成後在 audio/AudioEngine.cpp 的 rebuildGraph() 中呼叫：
 *   nrProcessor->setAlgorithm (std::make_unique<CustomNoiseReducer>());
 */
class CustomNoiseReducer : public INoiseReducer
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer) override;

private:
    double sampleRate = 44100.0;
};
