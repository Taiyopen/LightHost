#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>
#include "LoopbackCapture.h"
#include "../dsp/AecStats.h"

class AecProcessor;
class PluginChain;

/**
 * 音訊裝置、處理圖（輸入 → AEC → 降噪 → 外掛鏈 → 輸出）與 AEC 參考訊號擷取。
 * 任何設定或外掛清單改變後呼叫 rebuildGraph()；外掛鏈的增刪移動走下面的方法，
 * 會先把各外掛目前的狀態存起來再重建。
 */
class AudioEngine
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
    void togglePluginBypass (int sortedIndex);
    void movePluginUp (int sortedIndex);
    void movePluginDown (int sortedIndex);

    void savePluginStates();
    void clearPluginStates();

    /** 存設定並直接套用到執行中的 AEC，不重建處理圖 */
    void setAecStrength (float strengthPercent);

    /** 外掛載入失敗時回傳 nullptr（被略過的外掛仍有節點，可以開編輯視窗） */
    juce::AudioProcessorGraph::Node::Ptr getPluginNode (int sortedIndex);
    /** 載入失敗的原因；沒有失敗時回傳空字串 */
    juce::String getPluginLoadError (int sortedIndex) const;

    /** 建立外掛編輯視窗期間暫停整張處理圖 */
    void suspendProcessing (bool shouldSuspend) { graph.suspendProcessing (shouldSuspend); }

    AecMonitorSnapshot getAecMonitorSnapshot() const;

private:
    template <typename Change>
    void changeChain (Change&& change);

    AecProcessor* getAecProcessor() const;
    void updateLoopbackCapture (AecProcessor* aecProcessor);
    void connectChainToOutput (juce::AudioProcessorGraph::NodeID lastId, AecProcessor* aecPtr);

    PluginChain& plugins;

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<LoopbackCapture> loopbackCapture;

    // 對應 rebuildGraph 當下 getSortedPlugins() 的順序；0 代表沒載入
    std::vector<juce::uint32> pluginGraphNodeIds;
    // 鍵為 AppSettings::getPluginId
    std::map<juce::String, juce::String> pluginLoadErrors;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
