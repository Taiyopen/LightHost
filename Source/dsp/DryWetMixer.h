#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
 * 接在每個外掛後面的乾濕比混音：輸入 0/1 = 外掛輸出（濕）、2/3 = 外掛之前的訊號（乾），輸出 0/1。
 * 外掛有延遲時，處理圖會自動把乾的那一路延遲對齊。外掛被略過時只輸出乾的那一路
 * （已被延遲對齊），避免略過中的外掛沒延遲、乾聲有延遲造成相位抵消。
 */
class DryWetMixer : public juce::AudioProcessor
{
public:
    DryWetMixer()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput  ("Input",  juce::AudioChannelSet::discreteChannels (4), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    /** 0–100 %，100 = 全濕（只有外掛輸出） */
    void setMixPercent (float percent)       { mix.store (juce::jlimit (0.0f, 100.0f, percent) / 100.0f); }
    void setPluginBypassed (bool isBypassed) { pluginBypassed.store (isBypassed); }

    void prepareToPlay (double sampleRate, int) override
    {
        smoothedMix.reset (sampleRate, 0.03);
        smoothedMix.setCurrentAndTargetValue (targetMix());
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (buffer.getNumChannels() < 4)
            return;

        smoothedMix.setTargetValue (targetMix());

        auto* outL = buffer.getWritePointer (0);
        auto* outR = buffer.getWritePointer (1);
        const auto* dryL = buffer.getReadPointer (2);
        const auto* dryR = buffer.getReadPointer (3);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float wet = smoothedMix.getNextValue();
            outL[i] = dryL[i] + wet * (outL[i] - dryL[i]);
            outR[i] = dryR[i] + wet * (outR[i] - dryR[i]);
        }
    }

    const juce::String getName() const override { return "Dry/Wet Mix"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 0; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    float targetMix() const { return pluginBypassed.load() ? 0.0f : mix.load(); }

    std::atomic<float> mix { 1.0f };
    std::atomic<bool> pluginBypassed { false };
    juce::SmoothedValue<float> smoothedMix { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DryWetMixer)
};
