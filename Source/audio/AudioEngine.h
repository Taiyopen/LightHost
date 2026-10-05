#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>
#include "LoopbackCapture.h"
#include "Routing.h"
#include "ExternalDevice.h"
#include "../dsp/AecStats.h"

class AecProcessor;
class DryWetMixer;
class NoiseReducerProcessor;
class PluginChain;

/**
 * 音訊裝置、處理圖（輸入 → AEC → 降噪 → 外掛鏈 → 輸出）與 AEC 參考訊號擷取。
 * 任何設定或外掛清單改變後呼叫 rebuildGraph()；外掛鏈的增刪移動走下面的方法，
 * 會先把各外掛目前的狀態存起來再重建。
 */
class AudioEngine : private juce::Timer
{
public:
    explicit AudioEngine (PluginChain& plugins);
    ~AudioEngine();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    /** 依目前設定與外掛清單重建整張處理圖 */
    void rebuildGraph();
    void saveAudioDeviceStateAndRebuild();

    // 改外掛鏈：先存狀態 → 改清單 → 重建
    void addPlugin (const juce::PluginDescription& plugin);
    void removePlugin (int sortedIndex);
    /** 略過與乾濕比瞬間生效，不重建處理圖 */
    void togglePluginBypass (int sortedIndex);
    float getPluginMix (int sortedIndex) const;
    void setPluginMix (int sortedIndex, float percent);
    void movePluginUp (int sortedIndex);
    void movePluginDown (int sortedIndex);

    void savePluginStates();
    void clearPluginStates();

    /** 存設定並直接套用到執行中的 AEC，不重建處理圖 */
    void setAecStrength (float strengthPercent);

    // 降噪：開關與強度瞬間生效；換演算法要重建處理圖（會重新載入模型）
    void setNoiseReductionEnabled (bool enabled);
    void setNoiseReductionMaxAttenuation (float db);
    void setNoiseReducerAlgorithm (const juce::String& id);
    /** 上次建立處理圖時模型載入失敗的原因；成功時為空 */
    juce::String getNoiseReducerError() const { return noiseReducerError; }

    /** 外掛載入失敗時回傳 nullptr（被略過的外掛仍有節點，可以開編輯視窗） */
    juce::AudioProcessorGraph::Node::Ptr getPluginNode (int sortedIndex);
    /** 載入失敗的原因；沒有失敗時回傳空字串 */
    juce::String getPluginLoadError (int sortedIndex) const;

    /** 建立外掛編輯視窗期間暫停整張處理圖 */
    void suspendProcessing (bool shouldSuspend) { graph.suspendProcessing (shouldSuspend); }

    AecMonitorSnapshot getAecMonitorSnapshot() const;

    /** 麥克風到輸出的總延遲與各段明細（毫秒） */
    struct LatencyReport
    {
        bool hasDevice = false;
        double sampleRate = 0.0;
        int bufferSize = 0;
        double inputMs = 0.0;    // 音效卡回報的輸入延遲
        double aecMs = 0.0;
        double noiseMs = 0.0;    // 降噪關閉時為 0（原音直接通過）
        double pluginsMs = 0.0;  // 處理鏈上外掛回報的延遲總和（略過的也算：乾聲為了對齊仍會延遲）
        double outputMs = 0.0;   // 音效卡回報的輸出延遲
        std::vector<std::pair<juce::String, double>> externalOutputs;   // 附加輸出：名稱、多出的延遲（時脈橋＋裝置週期）

        double totalMs() const { return inputMs + aecMs + noiseMs + pluginsMs + outputMs; }
        /** 例：「總計 38 ms（輸入 5 + 回音消除 19 + 降噪 30 + 外掛 0 + 輸出 5）」 */
        juce::String describe() const;
        /** 附加輸出各自的總延遲（處理鏈＋該裝置）；沒有附加輸出時為空字串 */
        juce::String describeExternalOutputs (bool chinese) const;
    };
    LatencyReport getLatencyReport() const;

    /** 目前裝置上有勾選的聲道組（index 為裝置上的絕對組號，見 Routing） */
    struct ChannelPair
    {
        int index;
        juce::String name;      // 左右聲道名稱開頭相同時只取共同部分
        juce::String fullName;  // 「左 + 右」完整名稱
    };
    std::vector<ChannelPair> getActiveInputPairs() const  { return getActivePairs (true); }
    std::vector<ChannelPair> getActiveOutputPairs() const { return getActivePairs (false); }

    /** 存過的路由；沒存過時依目前裝置給預設（第一組輸入 → 處理 → 第一組輸出） */
    Routing getEffectiveRouting() const;
    /** 存檔並重建處理圖 */
    void setRouting (const Routing& routing);

    // 附加裝置（WASAPI 共用模式）。聲道組編號 = uid × externalPairBase + 組號，主裝置的組號都小於它
    static constexpr int externalPairBase = 1000;
    const std::vector<std::unique_ptr<ExternalDevice>>& getExternalDevices() const { return externalDevices; }
    void addExternalDevice (const juce::String& endpointId, const juce::String& name, bool isInput);
    void removeExternalDevice (int uid);
    /** 緩衝餘裕（幾個裝置週期）；改了會重建處理圖 */
    void setExternalSafetyPeriods (double periods);
    /** 低延遲共用模式；改了會重新開啟所有附加裝置 */
    void setExternalLowLatency (bool enabled);

private:
    template <typename Change>
    void changeChain (Change&& change);
    void timerCallback() override;
    void saveExternalDevices();
    std::unique_ptr<ExternalDevice> createExternalDevice (int uid, const juce::String& endpointId,
                                                          const juce::String& name, bool isInput);

    AecProcessor* getAecProcessor() const;
    NoiseReducerProcessor* getNoiseReducer() const;
    DryWetMixer* getPluginMixer (int sortedIndex) const;
    void updateLoopbackCapture (AecProcessor* aecProcessor);

    struct Pin
    {
        juce::AudioProcessorGraph::NodeID node;
        int left, right;
    };
    struct PairChannels
    {
        int left = -1, right = -1;
        bool isValid() const { return left >= 0; }
    };

    void connect (const Pin& from, const Pin& to);
    /** 裝置聲道組在處理圖輸入／輸出節點上的聲道編號；該組沒勾選時 isValid() 為 false */
    static PairChannels pairChannels (const juce::BigInteger& activeChannels, int pair);
    std::vector<ChannelPair> getActivePairs (bool inputs) const;
    static juce::String sharedPairName (const juce::String& left, const juce::String& right);

    PluginChain& plugins;

    // 要比 graph 晚毀：處理圖的節點握著它們的參照
    std::vector<std::unique_ptr<ExternalDevice>> externalDevices;

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<LoopbackCapture> loopbackCapture;

    // 對應 rebuildGraph 當下 getSortedPlugins() 的順序；0 代表沒載入
    std::vector<juce::uint32> pluginGraphNodeIds;
    // 鍵為 AppSettings::getPluginId
    std::map<juce::String, juce::String> pluginLoadErrors;
    juce::String noiseReducerError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
