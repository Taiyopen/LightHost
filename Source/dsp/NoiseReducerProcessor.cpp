#include "NoiseReducerProcessor.h"

NoiseReducerProcessor::NoiseReducerProcessor (std::unique_ptr<INoiseReducer> algorithmIn)
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    setAlgorithm (std::move (algorithmIn));
}

void NoiseReducerProcessor::setMaxAttenuationDb (float db)
{
    if (algorithm != nullptr)
        algorithm->setMaxAttenuationDb (db);
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
    {
        algorithm->prepare (sampleRate, samplesPerBlock);
        setLatencySamples (juce::roundToInt (algorithm->getLatencySeconds() * sampleRate));
    }
}

void NoiseReducerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (algorithm == nullptr)
        return;

    if (! enabled.load())
    {
        needsReset = true;
        return;
    }

    // 重新開啟時清掉關閉前留下的緩衝與模型狀態
    if (needsReset)
    {
        algorithm->reset();
        needsReset = false;
    }

    algorithm->process (buffer);
}
