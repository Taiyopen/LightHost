#pragma once

#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "plugins/PluginChain.h"

class AecMonitorWindow;
class SettingsWindow;

/** 系統列圖示與選單；擁有外掛清單、音訊引擎與各個視窗 */
class IconMenu : public SystemTrayIconComponent,
                 private Timer
{
public:
    IconMenu();
    ~IconMenu() override;

    void mouseDown (const MouseEvent&) override;

    PluginChain& getPlugins() { return plugins; }
    AudioEngine& getEngine() { return engine; }

    void openSettingsWindow();
    void closeSettingsWindow();
    void openAecMonitorWindow();
    void closeAecMonitorWindow();
    void openPluginEditor (int sortedIndex);

private:
    static void menuInvocationCallback (int id, IconMenu*);
    void handleLeftClickMenu (int id);
    void handleRightClickMenu (int id);

   #if JUCE_MAC
    std::string exec (const char* cmd);
   #endif
    void timerCallback() override;
    void openPluginListWindow();
    void setIcon();

    // 宣告順序即建構順序：外掛清單要比引擎先建、後毀
    PluginChain plugins;
    AudioEngine engine { plugins };

    PopupMenu menu;
    bool menuIconLeftClicked = false;

   #if JUCE_WINDOWS
    int x = 0, y = 0;
   #endif

    class PluginListWindow;
    std::unique_ptr<PluginListWindow> pluginListWindow;
    std::unique_ptr<AecMonitorWindow> aecMonitorWindow;
    std::unique_ptr<SettingsWindow> settingsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconMenu)
};
