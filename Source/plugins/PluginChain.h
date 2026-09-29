#pragma once

#include <JuceHeader.h>
#include <vector>

/**
 * 外掛清單：掃描到的外掛（known）與掛在處理鏈上的外掛（active），
 * 以及每個外掛的排序與略過狀態。只管資料，不碰音訊圖；改完要由 AudioEngine 重建。
 */
class PluginChain : private juce::ChangeListener
{
public:
    PluginChain();
    ~PluginChain() override;

    juce::KnownPluginList& getKnownPlugins() { return knownPlugins; }
    juce::KnownPluginList::SortMethod getSortMethod() const { return sortMethod; }

    /** 依使用者排的順序排好的處理鏈 */
    std::vector<juce::PluginDescription> getSortedPlugins() const;
    bool isBypassed (const juce::PluginDescription& plugin) const;

    // sortedIndex 都是 getSortedPlugins() 的索引；超出範圍時什麼都不做
    void add (const juce::PluginDescription& plugin);
    void remove (int sortedIndex);
    void toggleBypass (int sortedIndex);
    void moveUp (int sortedIndex);
    void moveDown (int sortedIndex);

    /** 掃描後移除不是立體聲進出的外掛 */
    void removePluginsLackingInputOutput();

private:
    void changeListenerCallback (juce::ChangeBroadcaster* changed) override;
    void ensureOrderKeys() const;
    void swapOrder (const juce::PluginDescription& a, const juce::PluginDescription& b);

    juce::KnownPluginList knownPlugins;
    juce::KnownPluginList activePlugins;
    juce::KnownPluginList::SortMethod sortMethod = juce::KnownPluginList::sortByManufacturer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginChain)
};
