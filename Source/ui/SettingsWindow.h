#pragma once

#include <JuceHeader.h>

class IconMenu;

/** 常駐設定視窗：Audio / AEC / Plugins 分頁 */
class SettingsWindow : public juce::DocumentWindow
{
public:
    explicit SettingsWindow (IconMenu& owner);
    ~SettingsWindow() override;

    void closeButtonPressed() override;

private:
    class SettingsPanel;

    IconMenu& owner;
    SettingsPanel* panel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};
