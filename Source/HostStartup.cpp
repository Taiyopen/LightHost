#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "AppSettings.h"
#include "AppTheme.h"

#if ! (JUCE_PLUGINHOST_VST || JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "Enable at least one plugin host format (VST3 recommended)."
#endif

class PluginHostApp : public JUCEApplication
{
public:
    PluginHostApp() = default;

    void initialise (const String&) override
    {
        PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";

        checkArguments (&options);

        appProperties = std::make_unique<ApplicationProperties>();
        appProperties->setStorageParameters (options);
        settings = std::make_unique<AppSettings> (*appProperties);

        applySystemColourScheme (lookAndFeel);
        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        mainWindow = std::make_unique<IconMenu>();

       #if JUCE_MAC
        Process::setDockIconVisible (false);
       #endif
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        settings = nullptr;
        appProperties = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    const String getApplicationName() override       { return "Light Host"; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }

    bool moreThanOneInstanceAllowed() override
    {
        return getParameter ("-multi-instance").size() == 2;
    }

    AppSettings& getSettings() { return *settings; }
    ApplicationCommandManager commandManager;

private:
    std::unique_ptr<ApplicationProperties> appProperties;
    std::unique_ptr<AppSettings> settings;
    LookAndFeel_V4 lookAndFeel;
    std::unique_ptr<IconMenu> mainWindow;

    StringArray getParameter (const String& lookFor)
    {
        const StringArray parameters = getCommandLineParameterArray();
        StringArray found;

        for (auto& param : parameters)
        {
            if (param.contains (lookFor))
            {
                found.add (lookFor);
                const int delimiter = param.indexOfChar ('=') + 1;
                found.add (param.substring (delimiter));
                return found;
            }
        }

        return found;
    }

    void checkArguments (PropertiesFile::Options* options)
    {
        const StringArray multiInstance = getParameter ("-multi-instance");
        if (multiInstance.size() == 2)
            options->filenameSuffix = multiInstance[1] + "." + options->filenameSuffix;
    }
};

static PluginHostApp& getApp()
{
    return *dynamic_cast<PluginHostApp*> (JUCEApplication::getInstance());
}

ApplicationCommandManager& getCommandManager()
{
    return getApp().commandManager;
}

AppSettings& getSettings()
{
    return getApp().getSettings();
}

START_JUCE_APPLICATION (PluginHostApp)
