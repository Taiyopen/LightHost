#include "NoiseReducerProcessor.h"

NoiseReducerProcessor::NoiseReducerProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    setAlgorithm (std::make_unique<SimpleNoiseReducer>());
}

bool NoiseReducerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void NoiseReducerProcessor::setAlgorithm (std::unique_ptr<INoiseReducer> newAlgorithm)
{
    algorithm = std::move (newAlgorithm);

    if (algorithm != nullptr && getSampleRate() > 0.0)
        algorithm->prepare (getSampleRate(), getBlockSize());
}

void NoiseReducerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (algorithm != nullptr)
        algorithm->prepare (sampleRate, samplesPerBlock);
}

void NoiseReducerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (algorithm != nullptr)
        algorithm->process (buffer);
}
