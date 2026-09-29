#pragma once

#include <JuceHeader.h>
#include "AecProcessor.h"

/** 將即將送往喇叭的訊號餵給 AEC 當參考（ASIO / WASAPI 皆適用） */
class OutputReferenceTap : public juce::AudioProcessor
{
public:
    explicit OutputReferenceTap (AecProcessor* aecProcessor);
    ~OutputReferenceTap() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    const juce::String getName() const override { return "Output Reference Tap"; }
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

    void setReferenceGain (float linearGain) { gain.store (linearGain); }

    /** false 時僅轉送音訊，不餵 AEC 參考（System Loopback 模式使用） */
    void setFeedsReference (bool shouldFeed) { feedsReference.store (shouldFeed); }

private:
    AecProcessor* aec = nullptr;
    std::atomic<float> gain { 1.0f };
    std::atomic<bool> feedsReference { true };
    std::vector<float> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputReferenceTap)
};
