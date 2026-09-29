#include "CustomNoiseReducer.h"

void CustomNoiseReducer::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate;
}

void CustomNoiseReducer::reset()
{
}

void CustomNoiseReducer::process (juce::AudioBuffer<float>& buffer)
{
    // TODO: 在此加入你的降噪演算法
    juce::ignoreUnused (buffer, sampleRate);
}
