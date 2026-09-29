#include "PluginChain.h"
#include "../AppSettings.h"
#include <algorithm>
#include <ctime>

PluginChain::PluginChain()
{
    if (auto saved = getSettings().getKnownPluginList())
        knownPlugins.recreateFromXml (*saved);

    if (auto saved = getSettings().getActivePluginList())
        activePlugins.recreateFromXml (*saved);

    // 載入完才開始監聽，避免一啟動就把剛讀進來的清單又寫回去
    knownPlugins.addChangeListener (this);
    activePlugins.addChangeListener (this);
}

PluginChain::~PluginChain()
{
    knownPlugins.removeChangeListener (this);
    activePlugins.removeChangeListener (this);
}

void PluginChain::changeListenerCallback (juce::ChangeBroadcaster* changed)
{
    if (changed == &knownPlugins)
    {
        if (auto xml = knownPlugins.createXml())
            getSettings().setKnownPluginList (*xml);
    }
    else if (changed == &activePlugins)
    {
        if (auto xml = activePlugins.createXml())
            getSettings().setActivePluginList (*xml);
    }
}

void PluginChain::ensureOrderKeys() const
{
    const auto types = activePlugins.getTypes();

    for (int i = 0; i < types.size(); ++i)
        if (getSettings().getPluginOrder (types.getReference (i)) <= 0)
            getSettings().setPluginOrder (types.getReference (i), i + 1);
}

std::vector<juce::PluginDescription> PluginChain::getSortedPlugins() const
{
    ensureOrderKeys();

    const auto types = activePlugins.getTypes();
    std::vector<juce::PluginDescription> list (types.begin(), types.end());

    std::stable_sort (list.begin(), list.end(),
                      [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
                      {
                          return getSettings().getPluginOrder (a) < getSettings().getPluginOrder (b);
                      });

    return list;
}

bool PluginChain::isBypassed (const juce::PluginDescription& plugin) const
{
    return getSettings().isPluginBypassed (plugin);
}

void PluginChain::add (const juce::PluginDescription& plugin)
{
    // 新加入的排在最後
    getSettings().setPluginOrder (plugin, (int) std::time (nullptr));
    activePlugins.addType (plugin);
}

void PluginChain::remove (int sortedIndex)
{
    const auto sorted = getSortedPlugins();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) sorted.size()))
        return;

    const auto& plugin = sorted[(size_t) sortedIndex];
    getSettings().removePluginEntries (plugin);
    activePlugins.removeType (plugin);
}

void PluginChain::toggleBypass (int sortedIndex)
{
    const auto sorted = getSortedPlugins();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) sorted.size()))
        return;

    const auto& plugin = sorted[(size_t) sortedIndex];
    getSettings().setPluginBypassed (plugin, ! getSettings().isPluginBypassed (plugin));
}

void PluginChain::moveUp (int sortedIndex)
{
    const auto sorted = getSortedPlugins();

    if (sortedIndex <= 0 || sortedIndex >= (int) sorted.size())
        return;

    swapOrder (sorted[(size_t) sortedIndex], sorted[(size_t) (sortedIndex - 1)]);
}

void PluginChain::moveDown (int sortedIndex)
{
    const auto sorted = getSortedPlugins();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) sorted.size() - 1))
        return;

    swapOrder (sorted[(size_t) sortedIndex], sorted[(size_t) (sortedIndex + 1)]);
}

void PluginChain::swapOrder (const juce::PluginDescription& a, const juce::PluginDescription& b)
{
    const int orderA = getSettings().getPluginOrder (a);
    const int orderB = getSettings().getPluginOrder (b);
    getSettings().setPluginOrder (a, orderB);
    getSettings().setPluginOrder (b, orderA);
}

void PluginChain::removePluginsLackingInputOutput()
{
    const auto types = knownPlugins.getTypes();

    for (const auto& plugin : types)
        if (plugin.numInputChannels < 2 || plugin.numOutputChannels < 2)
            knownPlugins.removeType (plugin);
}
