#pragma once

#include <JuceHeader.h>
#include <functional>

/**
 * 在背景執行緒逐一量測每種降噪：延遲（依目前的取樣率與區塊大小計算）與 CPU 用量
 * （用類似人聲＋背景噪音的訊號實際跑一段時間）。結果在訊息執行緒逐筆回報。
 */
class NoiseReducerBenchmark : private juce::Thread
{
public:
    struct Result
    {
        juce::String id;
        bool loaded = false;
        juce::String error;
        double latencyMs = 0.0;          // 計算值
        double measuredLatencyMs = -1.0; // 實測值（比對輸入輸出的時間差）；< 0 代表沒量
        double cpuPercent = -1.0;        // 單一 CPU 核心的百分比；< 0 代表沒量
    };

    NoiseReducerBenchmark();
    ~NoiseReducerBenchmark() override;

    /** measureCpu 為 true 時實際跑一段測試訊號量 CPU 與延遲；false 時只載入模型算延遲（很快） */
    void start (double sampleRate, int blockSize, bool measureCpu);
    bool isRunning() const { return isThreadRunning(); }

    std::function<void (const Result&)> onResult;   // 每量完一種
    std::function<void()> onFinished;

private:
    void run() override;
    void post (std::function<void()> fn);

    double sampleRate = 48000.0;
    int blockSize = 256;
    bool measureCpu = false;

    // 在建構時就建立，背景執行緒只複製它
    juce::WeakReference<NoiseReducerBenchmark> weakThis;

    JUCE_DECLARE_WEAK_REFERENCEABLE (NoiseReducerBenchmark)
};
