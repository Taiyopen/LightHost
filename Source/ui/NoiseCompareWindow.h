#pragma once

#include <JuceHeader.h>
#include <functional>

class AudioEngine;

/** 降噪演算法比較表：延遲（依目前裝置設定）、這台電腦上的 CPU 用量、頻寬與特色 */
class NoiseCompareWindow : public juce::DocumentWindow
{
public:
    NoiseCompareWindow (AudioEngine& engine, const juce::String& currentAlgorithmId);
    ~NoiseCompareWindow() override;

    void closeButtonPressed() override;

    /** 按下關閉時呼叫；擁有者在這裡釋放視窗 */
    std::function<void()> onClose;

private:
    class Panel;
    Panel* panel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseCompareWindow)
};
