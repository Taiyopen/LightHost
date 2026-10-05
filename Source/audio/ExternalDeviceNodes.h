#pragma once

#include <JuceHeader.h>
#include "ExternalDevice.h"

/** 處理圖裡代表附加輸入裝置的節點：沒有輸入，輸出裝置的所有聲道（已轉成主裝置取樣率、追上時脈） */
class ExternalInputProcessor : public juce::AudioProcessor
{
public:
    explicit ExternalInputProcessor (ExternalDevice& deviceIn)
        : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::discreteChannels (juce::jmax (1, deviceIn.getNumChannels())), true)),
          device (deviceIn)
    {
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        // 目標緩衝 = 裝置週期 × 餘裕＋主裝置一個區塊＋3 ms（換成裝置的樣本數）
        const double rate = device.getSampleRate();
        const int target = (int) (rate * (device.getDevicePeriodSeconds() * device.getSafetyPeriods()
                                          + samplesPerBlock / sampleRate + 0.003));
        device.getBridge().configureReader (rate, sampleRate, target);
        device.setTargetSeconds (target / rate);
        setLatencySamples ((int) (target * sampleRate / rate));
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        device.getBridge().read (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
    }

    const juce::String getName() const override { return "External Input: " + device.getName(); }
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
    ExternalDevice& device;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExternalInputProcessor)
};

/** 處理圖裡代表附加輸出裝置的節點：輸入是要送到該裝置的聲道，沒有輸出 */
class ExternalOutputProcessor : public juce::AudioProcessor
{
public:
    explicit ExternalOutputProcessor (ExternalDevice& deviceIn)
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::discreteChannels (juce::jmax (1, deviceIn.getNumChannels())), true)),
          device (deviceIn)
    {
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        // 讀取端在裝置自己的執行緒；告訴它主裝置的格式，它會自己重新設定
        device.setMasterFormat (sampleRate, samplesPerBlock);
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        device.getBridge().write (buffer.getArrayOfReadPointers(), buffer.getNumSamples());
    }

    const juce::String getName() const override { return "External Output: " + device.getName(); }
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
    ExternalDevice& device;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExternalOutputProcessor)
};
