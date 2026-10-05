#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

/**
 * 兩個各用各時脈的音訊裝置之間的橋：寫入端（一條執行緒）以來源取樣率寫入，
 * 讀取端（另一條執行緒）以自己的取樣率讀出。讀取端負責：
 *   - 轉換取樣率（四點三次內插，讀取位置可落在樣本之間）
 *   - 依緩衝量持續微調讀取速度，追上兩邊的時脈差（比例＋積分，最多 ±2000 ppm）
 *   - 資料斷了就輸出靜音、讀取位置不動；資料夠了直接對準目標緩衝量再開始
 * 寫入與讀取都不配置記憶體、不上鎖。
 */
class ClockBridge
{
public:
    /** 在兩邊都還沒開始前呼叫（會配置記憶體） */
    void prepare (int numChannels, int capacityFrames);
    int getNumChannels() const { return numChannels; }

    // ---- 寫入端 ----
    /** channels[ch][i]；滿了就丟掉放不下的部分（讀取端停住時才會發生） */
    void write (const float* const* channels, int numFrames);
    /** 交錯格式 L R L R … */
    void writeInterleaved (const float* interleaved, int numFrames);
    void writeSilence (int numFrames);

    // ---- 讀取端 ----
    /** 來源、讀取端的取樣率與目標緩衝量（來源樣本數）；改變時會重新對準 */
    void configureReader (double sourceRate, double readerRate, int targetFrames);
    /** 讀出 numFrames 個讀取端取樣率的樣本；資料不夠時輸出靜音並回傳 false */
    bool read (float* const* dest, int numDestChannels, int numFrames);
    /** 交錯格式；dest 長度 numFrames × getNumChannels() */
    bool readInterleaved (float* dest, int numFrames);

    // ---- 狀態（任何執行緒）----
    double getDriftPpm() const          { return driftPpm.load (std::memory_order_relaxed); }
    juce::int64 getResyncCount() const  { return resyncs.load (std::memory_order_relaxed); }
    /** 目前緩衝了多少來源樣本 */
    int getBufferedFrames() const;

private:
    float sampleAt (int channel, juce::int64 frame) const;
    bool prepareRead (int numFrames, double& step);

    int numChannels = 0;
    int capacity = 0;
    std::vector<float> ring;   // 交錯：frame * numChannels + ch

    std::atomic<juce::int64> writeFrame { 0 };   // 已寫入的總樣本數
    std::atomic<juce::int64> readFrame { 0 };    // 讀取端已不再需要的位置（寫入端用來判斷剩餘空間）

    // 讀取端專用
    double sourceRate = 48000.0, readerRate = 48000.0;
    double ratio = 1.0;          // 來源取樣率 / 讀取端取樣率
    int targetFrames = 0;
    double position = 0.0;       // 讀取位置（來源樣本，可落在樣本之間）
    double smoothedLead = 0.0;
    double integral = 0.0;
    bool starved = true;
    std::vector<float> interleavedScratch;

    std::atomic<double> driftPpm { 0.0 };
    std::atomic<juce::int64> resyncs { 0 };
};
