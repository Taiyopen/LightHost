#include "SimpleNoiseReducer.h"

void SimpleNoiseReducer::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate;
    gateGain.reset (sampleRate, 0.02);
    gateGain.setCurrentAndTargetValue (1.0f);
}

void SimpleNoiseReducer::reset()
{
    gateGain.setCurrentAndTargetValue (1.0f);
}

float SimpleNoiseReducer::computeChannelRms (const float* data, int numSamples) const
{
    if (numSamples <= 0)
        return 0.0f;

    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double> (data[i]) * static_cast<double> (data[i]);

    return static_cast<float> (std::sqrt (sum / static_cast<double> (numSamples)));
}

void SimpleNoiseReducer::process (juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    float maxRms = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        maxRms = juce::jmax (maxRms, computeChannelRms (buffer.getReadPointer (ch), numSamples));

    const float threshold = juce::Decibels::decibelsToGain (gateThresholdDb);
    const float targetGain = maxRms >= threshold ? 1.0f : (1.0f - reductionAmount);
    gateGain.setTargetValue (targetGain);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gateGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}
