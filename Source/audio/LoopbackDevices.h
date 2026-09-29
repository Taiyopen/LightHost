#pragma once

#include <JuceHeader.h>

struct LoopbackDeviceInfo
{
    juce::String id;
    juce::String name;
};

/** 列出可用於 WASAPI loopback 的喇叭（輸出）裝置 */
juce::Array<LoopbackDeviceInfo> enumerateLoopbackOutputDevices();

juce::String getDefaultLoopbackDeviceName();
