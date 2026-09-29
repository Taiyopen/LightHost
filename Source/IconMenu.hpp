#pragma once

#include <JuceHeader.h>
#include <map>
#include "audio/LoopbackCapture.h"
#include "dsp/AecStats.h"
#include "dsp/GraphNodeIds.h"

ApplicationProperties& getAppProperties();

class AecProcessor;
class AecMonitorWindow;
class SettingsWindow;

class IconMenu : public SystemTrayIconComponent,
                 private Timer,
                 public ChangeListener
{
public:
    IconMenu();
    ~IconMenu() override;

    void mouseDown (const MouseEvent&) override;
    static void menuInvocationCallback (int id, IconMenu*);
    void changeListenerCallback (ChangeBroadcaster* changed) override;
    static String getKey (String type, PluginDescription plugin);

    const int INDEX_EDIT, INDEX_BYPASS, INDEX_DELETE, INDEX_MOVE_UP, INDEX_MOVE_DOWN;
    const int INDEX_AEC_TOGGLE, INDEX_NR_TOGGLE, INDEX_AEC_MONITOR;
    const int INDEX_SPEAKER_REF_BASE, INDEX_SPEAKER_GAIN_BASE, INDEX_AEC_STRENGTH_BASE;

    AecMonitorSnapshot getAecMonitorSnapshot() const;
    void closeAecMonitorWindow();
    void closeSettingsWindow();

    AudioDeviceManager& getDeviceManager() { return deviceManager; }
    void saveAudioDeviceStateAndReload();
    void reloadActivePlugins();

    bool isAecEnabled() const;
    bool isNrEnabled() const;
    void setAecEnabled (bool enabled);
    void setNrEnabled (bool enabled);
    bool shouldUseSystemLoopbackReference() const;
    void setUseSystemLoopbackReference (bool use);
    juce::String getReferenceDeviceId() const;
    void setReferenceDeviceId (const juce::String& deviceId);
    float getReferenceGainDb() const;
    void setReferenceGainDb (float gainDb);
    float getAecStrengthPercent() const;
    void setAecStrengthPercent (float strengthPercent);
    AecProcessor* getAecProcessor() const;

    void openSettingsWindow();
    void openAecMonitorWindow();

    std::vector<PluginDescription> getTimeSortedPluginList() const;
    bool isPluginBypassed (const PluginDescription& plugin) const;
    void togglePluginBypass (int sortedIndex);
    void deletePluginAtSortedIndex (int sortedIndex);
    void movePluginUp (int sortedIndex);
    void movePluginDown (int sortedIndex);
    void addActivePlugin (const PluginDescription& plugin);
    void openPluginEditorForSortedIndex (int sortedIndex);

    KnownPluginList& getKnownPluginList() { return knownPluginList; }
    KnownPluginList::SortMethod getPluginSortMethod() const { return pluginSortMethod; }

private:
   #if JUCE_MAC
    std::string exec (const char* cmd);
   #endif
    void timerCallback() override;
    void reloadPlugins();
    void loadActivePlugins();
    void savePluginStates();
    void deletePluginStates();
    void removePluginsLackingInputOutput();
    std::vector<PluginDescription> getTimeSortedList();
    void setIcon();

    void updateLoopbackCapture (AecProcessor* aecProcessor);
    void ensurePluginOrderKeys();
    void reprepareAudioGraph();
    void connectChainToOutput (juce::AudioProcessorGraph::NodeID lastId, AecProcessor* aecPtr);
    void restartAecReferenceCapture();
    void openPluginEditorForNode (juce::AudioProcessorGraph::Node::Ptr node);
    juce::AudioProcessorGraph::Node::Ptr findGraphNodeForIndex (int sortedIndex);
    juce::String getPluginLoadError (const juce::PluginDescription& desc) const;

    AudioDeviceManager deviceManager;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    KnownPluginList activePluginList;
    KnownPluginList::SortMethod pluginSortMethod;
    PopupMenu menu;
    std::unique_ptr<PluginDirectoryScanner> scanner;
    bool menuIconLeftClicked = false;
    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
    AudioProcessorGraph::Node* inputNode = nullptr;
    AudioProcessorGraph::Node* outputNode = nullptr;
    std::unique_ptr<LoopbackCapture> loopbackCapture;
    std::vector<uint32> pluginGraphNodeIds;
    std::map<juce::String, juce::String> pluginLoadErrors;

   #if JUCE_WINDOWS
    int x = 0, y = 0;
   #endif

    class PluginListWindow;
    std::unique_ptr<PluginListWindow> pluginListWindow;
    std::unique_ptr<AecMonitorWindow> aecMonitorWindow;
    std::unique_ptr<SettingsWindow> settingsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconMenu)
};
