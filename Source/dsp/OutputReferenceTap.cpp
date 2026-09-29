#include "OutputReferenceTap.h"

OutputReferenceTap::OutputReferenceTap (AecProcessor* aecProcessor)
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      aec (aecProcessor)
{
}

bool OutputReferenceTap::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void OutputReferenceTap::prepareToPlay (double /*sampleRate*/, int samplesPerBlock)
{
    mono.assign (static_cast<size_t> (samplesPerBlock), 0.0f);
}

void OutputReferenceTap::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (aec == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    if (! feedsReference.load())
        return;

    // 主機給的區塊比 prepareToPlay 宣告的大時才會配置（少見）
    if (static_cast<int> (mono.size()) < numSamples)
        mono.resize (static_cast<size_t> (numSamples));

    const float currentGain = gain.load();

    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getReadPointer (ch)[i];

        mono[static_cast<size_t> (i)] = static_cast<float> ((sum / static_cast<double> (numChannels)) * currentGain);
    }

    aec->pushReference (mono.data(), numSamples);
}
