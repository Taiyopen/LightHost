#pragma once

#include "INoiseReducer.h"
#include "SimpleNoiseReducer.h"
#include <memory>

/** JUCE AudioProcessor 包裝 — 在 graph 中使用可插拔降噪 */
class NoiseReducerProcessor : public juce::AudioProcessor
{
public:
    NoiseReducerProcessor();
    ~NoiseReducerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    const juce::String getName() const override { return "Noise Reduction"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 0; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    /** 替換成自訂降噪演算法 */
    void setAlgorithm (std::unique_ptr<INoiseReducer> newAlgorithm);

    INoiseReducer* getAlgorithm() const { return algorithm.get(); }

private:
    std::unique_ptr<INoiseReducer> algorithm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseReducerProcessor)
};
