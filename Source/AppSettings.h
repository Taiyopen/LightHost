#pragma once

#include <JuceHeader.h>
#include <optional>
#include "audio/Routing.h"

/**
 * 設定檔唯一的讀寫入口。所有設定鍵名與預設值都只寫在 AppSettings.cpp，
 * 其他地方不要直接碰 PropertiesFile。
 */
class AppSettings
{
public:
    explicit AppSettings (juce::ApplicationProperties& properties);

    /** 給需要直接拿 PropertiesFile 的 JUCE 元件（例如 PluginListComponent）使用 */
    juce::PropertiesFile& getFile() const;

    // 音訊處理
    bool isAecEnabled() const;
    void setAecEnabled (bool enabled);
    bool isNrEnabled() const;
    void setNrEnabled (bool enabled);
    /** 降噪演算法 id，見 getNoiseReducerChoices() */
    juce::String getNrAlgorithm() const;
    void setNrAlgorithm (const juce::String& id);
    /** 背景噪音最多壓低幾 dB；>= 100 代表不限制 */
    float getNrMaxAttenuationDb() const;
    void setNrMaxAttenuationDb (float db);
    bool useSystemLoopbackReference() const;
    void setUseSystemLoopbackReference (bool use);
    juce::String getReferenceDeviceId() const;
    void setReferenceDeviceId (const juce::String& deviceId);
    float getReferenceGainDb() const;
    void setReferenceGainDb (float gainDb);
    float getAecStrengthPercent() const;
    void setAecStrengthPercent (float strengthPercent);

    /** 沒設定過時回傳空值，由 AudioEngine 依目前裝置決定預設 */
    std::optional<Routing> getRouting() const;
    void setRouting (const Routing& routing);

    // 自動檢查更新
    bool isAutoUpdateCheckEnabled() const;
    void setAutoUpdateCheckEnabled (bool enabled);

    // 系統列圖示顏色："white" 或 "black"
    juce::String getIconColour() const;
    void setIconColour (const juce::String& colour);

    // 音訊裝置與外掛清單（XML）
    std::unique_ptr<juce::XmlElement> getAudioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement& state);
    std::unique_ptr<juce::XmlElement> getKnownPluginList() const;
    void setKnownPluginList (const juce::XmlElement& list);
    std::unique_ptr<juce::XmlElement> getActivePluginList() const;
    void setActivePluginList (const juce::XmlElement& list);

    // 視窗位置
    enum class Window { pluginList, settings, aecMonitor };
    juce::String getWindowState (Window window) const;
    void setWindowState (Window window, const juce::String& state);

    // 每個外掛的排序、略過與狀態；排序值 <= 0 代表還沒設定
    int getPluginOrder (const juce::PluginDescription& plugin) const;
    void setPluginOrder (const juce::PluginDescription& plugin, int order);
    bool isPluginBypassed (const juce::PluginDescription& plugin) const;
    void setPluginBypassed (const juce::PluginDescription& plugin, bool bypassed);
    /** 乾濕比 0–100 %，預設 100（只有外掛輸出） */
    float getPluginMix (const juce::PluginDescription& plugin) const;
    void setPluginMix (const juce::PluginDescription& plugin, float percent);
    juce::String getPluginState (const juce::PluginDescription& plugin) const;
    void setPluginState (const juce::PluginDescription& plugin, const juce::String& base64State);
    void removePluginState (const juce::PluginDescription& plugin);
    /** 移除外掛時一併清掉它的排序、略過、乾濕比與狀態 */
    void removePluginEntries (const juce::PluginDescription& plugin);

    /** 同一個外掛（名稱＋版本＋格式）的識別字串，也用來當錯誤訊息表的鍵 */
    static juce::String getPluginId (const juce::PluginDescription& plugin);

private:
    void save();

    juce::ApplicationProperties& properties;
};

/** 由 HostStartup 建立，App 存活期間都有效 */
AppSettings& getSettings();
