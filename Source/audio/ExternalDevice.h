#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "ClockBridge.h"

/** 系統上可加入的 WASAPI 端點（共用模式） */
struct WasapiEndpointInfo
{
    juce::String id;
    juce::String name;
    bool isInput = false;
    int sampleRate = 0;     // Windows 音效設定裡的格式；讀不到時為 0
    int numChannels = 0;
    double defaultPeriodMs = 0.0;      // 一般共用模式的週期
    double minLowLatencyPeriodMs = 0.0; // 低延遲共用模式最短週期；驅動不支援時與 default 相同或為 0
};

juce::Array<WasapiEndpointInfo> enumerateWasapiEndpoints (bool inputs);

/** 開啟 Windows 的「聲音」設定（改共用模式的取樣率要在那裡改） */
void openWindowsSoundSettings();

/**
 * 主裝置（ASIO）以外的附加裝置，以 WASAPI 共用模式開啟。建構時（訊息執行緒）讀出裝置格式並開好，
 * 之後由自己的執行緒收發聲音，透過 ClockBridge 和主裝置交換：
 *   輸入裝置：本執行緒寫入（裝置取樣率），處理圖的 ExternalInputProcessor 讀出（主裝置取樣率）
 *   輸出裝置：處理圖的 ExternalOutputProcessor 寫入（主裝置取樣率），本執行緒讀出（裝置取樣率）
 * 開不起來或執行中出錯（例如被拔掉）時 getState() 變成 error，由 AudioEngine 稍後重建。
 */
class ExternalDevice : private juce::Thread
{
public:
    enum class State { running, error };

    /** lowLatency：先試 Windows 低延遲共用模式；safetyPeriods：緩衝餘裕要留幾個裝置週期 */
    ExternalDevice (int uid, const juce::String& endpointId, const juce::String& name, bool isInput,
                    bool lowLatency, double safetyPeriods);
    ~ExternalDevice() override;

    int getUid() const                       { return uid; }
    const juce::String& getEndpointId() const { return endpointId; }
    const juce::String& getName() const      { return name; }
    bool isInput() const                     { return input; }

    State getState() const                   { return state.load(); }
    juce::String getError() const;
    int getSampleRate() const                { return sampleRate; }
    int getNumChannels() const               { return numChannels; }
    double getDevicePeriodSeconds() const    { return devicePeriodSeconds; }
    bool isLowLatencyActive() const          { return lowLatencyActive; }

    /** 緩衝餘裕（幾個裝置週期）；輸出裝置的執行緒會自己重新設定，輸入裝置要重建處理圖才生效 */
    void setSafetyPeriods (double periods)   { safetyPeriods.store (periods); }
    double getSafetyPeriods() const          { return safetyPeriods.load(); }

    /** 這個裝置多出的延遲：時脈橋的目標緩衝＋一個裝置週期（還沒開始時為 0） */
    double getAddedLatencySeconds() const
    {
        const double t = targetSeconds.load();
        return t > 0.0 ? t + devicePeriodSeconds : 0.0;
    }
    /** 由讀取端（輸入裝置是處理圖節點）回報設定好的目標緩衝秒數 */
    void setTargetSeconds (double seconds)   { targetSeconds.store (seconds); }

    ClockBridge& getBridge()                 { return bridge; }

    /** 輸出裝置用：主裝置的取樣率與區塊大小（處理圖 prepare 時設定），本執行緒據此調整讀取 */
    void setMasterFormat (double rate, int blockSize);

private:
    struct Impl;

    void run() override;
    void stream();
    void fail (const juce::String& message);

    const int uid;
    const juce::String endpointId, name;
    const bool input;
    const bool lowLatency;
    bool lowLatencyActive = false;

    std::unique_ptr<Impl> impl;
    ClockBridge bridge;
    int sampleRate = 0, numChannels = 0;
    double devicePeriodSeconds = 0.01;

    std::atomic<State> state { State::error };
    juce::CriticalSection errorLock;
    juce::String error;

    std::atomic<double> safetyPeriods { 1.5 };
    std::atomic<double> targetSeconds { 0.0 };
    std::atomic<double> masterRate { 0.0 };
    std::atomic<int> masterBlock { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExternalDevice)
};
