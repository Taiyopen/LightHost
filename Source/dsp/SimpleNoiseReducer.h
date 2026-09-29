#pragma once

#include "INoiseReducer.h"

/** 簡易降噪（高頻衰減 + 噪音閘）— 可替換成自訂演算法 */
class SimpleNoiseReducer : public INoiseReducer
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer) override;

    void setGateThresholdDb (float db) { gateThresholdDb = db; }
    void setReductionAmount (float amount) { reductionAmount = juce::jlimit (0.0f, 1.0f, amount); }

private:
    double sampleRate = 44100.0;
    float gateThresholdDb = -40.0f;
    float reductionAmount = 0.6f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gateGain;

    float computeChannelRms (const float* data, int numSamples) const;
};
