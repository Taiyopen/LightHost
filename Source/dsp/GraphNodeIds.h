#pragma once

#include <JuceHeader.h>

namespace GraphNodeIds
{
    inline juce::AudioProcessorGraph::NodeID inputId()  { return juce::AudioProcessorGraph::NodeID { 1000000 }; }
    inline juce::AudioProcessorGraph::NodeID outputId() { return juce::AudioProcessorGraph::NodeID { 1000001 }; }
    inline juce::AudioProcessorGraph::NodeID aecId()    { return juce::AudioProcessorGraph::NodeID { 999997 }; }
    inline juce::AudioProcessorGraph::NodeID nrId()     { return juce::AudioProcessorGraph::NodeID { 999998 }; }
    inline juce::AudioProcessorGraph::NodeID refTapId() { return juce::AudioProcessorGraph::NodeID { 999996 }; }
    /** 附加裝置（WASAPI）的節點，依裝置的 uid */
    inline juce::AudioProcessorGraph::NodeID externalInputId (int uid)  { return juce::AudioProcessorGraph::NodeID { (juce::uint32) (600000 + uid) }; }
    inline juce::AudioProcessorGraph::NodeID externalOutputId (int uid) { return juce::AudioProcessorGraph::NodeID { (juce::uint32) (700000 + uid) }; }
    /** 外掛節點編號從 1 開始；它後面的乾濕比混音用 500000 + 同一個編號 */
    inline juce::AudioProcessorGraph::NodeID pluginMixerId (juce::uint32 pluginNodeIndex) { return juce::AudioProcessorGraph::NodeID { 500000 + pluginNodeIndex }; }

    inline constexpr int channelLeft  = 0;
    inline constexpr int channelRight = 1;
}
