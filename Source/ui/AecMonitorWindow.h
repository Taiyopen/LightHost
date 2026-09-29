#pragma once

#include <JuceHeader.h>
#include "../dsp/AecStats.h"

class IconMenu;

class AecMonitorWindow : public juce::DocumentWindow,
                         private juce::Timer
{
public:
    AecMonitorWindow (IconMenu& owner);
    ~AecMonitorWindow() override;

    void closeButtonPressed() override;

private:
    class MonitorPanel;

    void timerCallback() override;

    IconMenu& owner;
    MonitorPanel* panel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AecMonitorWindow)
};
