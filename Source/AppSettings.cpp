#include "AppSettings.h"

namespace
{
    // 鍵名沿用舊版，已存在的設定檔不需要轉換
    namespace Keys
    {
        constexpr auto aecEnabled           = "aecEnabled";
        constexpr auto nrEnabled            = "nrEnabled";
        constexpr auto useSystemLoopback    = "aecUseSystemLoopback";
        constexpr auto referenceDeviceId    = "aecReferenceDeviceId";
        constexpr auto referenceGainDb      = "aecReferenceGainDb";
        constexpr auto aecStrengthPercent   = "aecStrengthPercent";
        constexpr auto icon                 = "icon";
        constexpr auto autoUpdateCheck      = "autoCheckUpdates";
        constexpr auto audioDeviceState     = "audioDeviceState";
        constexpr auto knownPluginList      = "pluginList";
        constexpr auto activePluginList     = "pluginListActive";
        constexpr auto pluginListWindowPos  = "listWindowPos";
        constexpr auto settingsWindowPos    = "settingsWindowPos";
        constexpr auto aecMonitorWindowPos  = "aecMonitorWindowPos";
    }

    juce::String pluginKey (const char* type, const juce::PluginDescription& plugin)
    {
        return juce::String ("plugin-") + type + "-" + AppSettings::getPluginId (plugin);
    }

    const char* windowKey (AppSettings::Window window)
    {
        switch (window)
        {
            case AppSettings::Window::pluginList: return Keys::pluginListWindowPos;
            case AppSettings::Window::settings:   return Keys::settingsWindowPos;
            case AppSettings::Window::aecMonitor: return Keys::aecMonitorWindowPos;
        }

        jassertfalse;
        return Keys::settingsWindowPos;
    }
}

AppSettings::AppSettings (juce::ApplicationProperties& propertiesIn)
    : properties (propertiesIn)
{
}

juce::PropertiesFile& AppSettings::getFile() const
{
    return *properties.getUserSettings();
}

void AppSettings::save()
{
    properties.saveIfNeeded();
}

bool AppSettings::isAecEnabled() const                  { return getFile().getBoolValue (Keys::aecEnabled, true); }
void AppSettings::setAecEnabled (bool enabled)          { getFile().setValue (Keys::aecEnabled, enabled); save(); }
bool AppSettings::isNrEnabled() const                   { return getFile().getBoolValue (Keys::nrEnabled, true); }
void AppSettings::setNrEnabled (bool enabled)           { getFile().setValue (Keys::nrEnabled, enabled); save(); }
bool AppSettings::useSystemLoopbackReference() const    { return getFile().getBoolValue (Keys::useSystemLoopback, false); }
void AppSettings::setUseSystemLoopbackReference (bool use) { getFile().setValue (Keys::useSystemLoopback, use); save(); }

juce::String AppSettings::getReferenceDeviceId() const
{
    return getFile().getValue (Keys::referenceDeviceId);
}

void AppSettings::setReferenceDeviceId (const juce::String& deviceId)
{
    getFile().setValue (Keys::referenceDeviceId, deviceId);
    save();
}

float AppSettings::getReferenceGainDb() const
{
    return (float) getFile().getDoubleValue (Keys::referenceGainDb, 0.0);
}

void AppSettings::setReferenceGainDb (float gainDb)
{
    getFile().setValue (Keys::referenceGainDb, gainDb);
    save();
}

float AppSettings::getAecStrengthPercent() const
{
    return (float) getFile().getDoubleValue (Keys::aecStrengthPercent, 100.0);
}

void AppSettings::setAecStrengthPercent (float strengthPercent)
{
    getFile().setValue (Keys::aecStrengthPercent, strengthPercent);
    save();
}

bool AppSettings::isAutoUpdateCheckEnabled() const
{
    return getFile().getBoolValue (Keys::autoUpdateCheck, true);
}

void AppSettings::setAutoUpdateCheckEnabled (bool enabled)
{
    getFile().setValue (Keys::autoUpdateCheck, enabled);
    save();
}

juce::String AppSettings::getIconColour() const
{
   #if JUCE_LINUX
    const juce::String defaultColour = "black";
   #else
    const juce::String defaultColour = "white";
   #endif

    return getFile().getValue (Keys::icon, defaultColour);
}

void AppSettings::setIconColour (const juce::String& colour)
{
    getFile().setValue (Keys::icon, colour);
    save();
}

std::unique_ptr<juce::XmlElement> AppSettings::getAudioDeviceState() const
{
    return getFile().getXmlValue (Keys::audioDeviceState);
}

void AppSettings::setAudioDeviceState (const juce::XmlElement& state)
{
    getFile().setValue (Keys::audioDeviceState, &state);
    save();
}

std::unique_ptr<juce::XmlElement> AppSettings::getKnownPluginList() const
{
    return getFile().getXmlValue (Keys::knownPluginList);
}

void AppSettings::setKnownPluginList (const juce::XmlElement& list)
{
    getFile().setValue (Keys::knownPluginList, &list);
    save();
}

std::unique_ptr<juce::XmlElement> AppSettings::getActivePluginList() const
{
    return getFile().getXmlValue (Keys::activePluginList);
}

void AppSettings::setActivePluginList (const juce::XmlElement& list)
{
    getFile().setValue (Keys::activePluginList, &list);
    save();
}

juce::String AppSettings::getWindowState (Window window) const
{
    return getFile().getValue (windowKey (window));
}

void AppSettings::setWindowState (Window window, const juce::String& state)
{
    getFile().setValue (windowKey (window), state);
    save();
}

juce::String AppSettings::getPluginId (const juce::PluginDescription& plugin)
{
    return plugin.name + plugin.version + plugin.pluginFormatName;
}

int AppSettings::getPluginOrder (const juce::PluginDescription& plugin) const
{
    return getFile().getValue (pluginKey ("order", plugin)).getIntValue();
}

void AppSettings::setPluginOrder (const juce::PluginDescription& plugin, int order)
{
    getFile().setValue (pluginKey ("order", plugin), order);
    save();
}

bool AppSettings::isPluginBypassed (const juce::PluginDescription& plugin) const
{
    return getFile().getBoolValue (pluginKey ("bypass", plugin), false);
}

void AppSettings::setPluginBypassed (const juce::PluginDescription& plugin, bool bypassed)
{
    getFile().setValue (pluginKey ("bypass", plugin), bypassed);
    save();
}

juce::String AppSettings::getPluginState (const juce::PluginDescription& plugin) const
{
    return getFile().getValue (pluginKey ("state", plugin));
}

void AppSettings::setPluginState (const juce::PluginDescription& plugin, const juce::String& base64State)
{
    getFile().setValue (pluginKey ("state", plugin), base64State);
    save();
}

void AppSettings::removePluginState (const juce::PluginDescription& plugin)
{
    getFile().removeValue (pluginKey ("state", plugin));
    save();
}

void AppSettings::removePluginEntries (const juce::PluginDescription& plugin)
{
    getFile().removeValue (pluginKey ("order", plugin));
    getFile().removeValue (pluginKey ("bypass", plugin));
    getFile().removeValue (pluginKey ("state", plugin));
    save();
}
