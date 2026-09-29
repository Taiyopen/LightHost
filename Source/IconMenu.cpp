#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "AppSettings.h"
#include "AppTheme.h"
#include "PluginWindow.h"
#include "ui/AecMonitorWindow.h"
#include "ui/SettingsWindow.h"
#if JUCE_WINDOWS
 #include <Windows.h>
#endif

namespace
{
    // 左鍵選單；已知外掛的項目由 KnownPluginList::addToMenu 產生，編號遠大於這些
    namespace LeftMenu
    {
        enum
        {
            settings    = 1,
            editPlugins = 2,
            aecToggle   = 6000000,
            nrToggle    = 6100000,
            aecMonitor  = 6150000
        };
    }

    // 右鍵選單
    namespace RightMenu
    {
        enum
        {
            quit             = 1,
            deletePluginStates = 2,
            invertIconColour = 3
        };
    }
}

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
    explicit PluginListWindow (IconMenu& owner_)
        : DocumentWindow ("Available Plugins", getDialogBackgroundColour(),
                          DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          owner (owner_)
    {
        auto& settingsFile = getSettings().getFile();
        const File deadMansPedalFile (settingsFile.getFile().getSiblingFile ("RecentlyCrashedPluginsList"));

        setContentOwned (new PluginListComponent (owner.engine.getFormatManager(),
                                                  owner.plugins.getKnownPlugins(),
                                                  deadMansPedalFile,
                                                  &settingsFile), true);

        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (300, 400, 800, 1500);
        setSize (500, 450);
        setTopLeftPosition (60, 60);
        centreWithSize (getWidth(), getHeight());

        restoreWindowStateFromString (getSettings().getWindowState (AppSettings::Window::pluginList));
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    ~PluginListWindow() override
    {
        getSettings().setWindowState (AppSettings::Window::pluginList, getWindowStateAsString());
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        owner.plugins.removePluginsLackingInputOutput();
       #if JUCE_MAC
        Process::setDockIconVisible (false);
       #endif
        owner.pluginListWindow = nullptr;
    }

private:
    IconMenu& owner;
};

IconMenu::IconMenu()
{
    setIcon();
    setIconTooltip (JUCEApplication::getInstance()->getApplicationName());
}

IconMenu::~IconMenu() = default;

void IconMenu::openAecMonitorWindow()
{
    if (aecMonitorWindow != nullptr)
    {
        aecMonitorWindow->toFront (true);
        return;
    }

   #if JUCE_MAC
    Process::setDockIconVisible (true);
   #endif

    aecMonitorWindow = std::make_unique<AecMonitorWindow> (*this);
}

void IconMenu::closeAecMonitorWindow()
{
    aecMonitorWindow = nullptr;
}

void IconMenu::openSettingsWindow()
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront (true);
        return;
    }

   #if JUCE_MAC
    Process::setDockIconVisible (true);
   #endif

    settingsWindow = std::make_unique<SettingsWindow> (*this);
}

void IconMenu::closeSettingsWindow()
{
    settingsWindow = nullptr;
}

void IconMenu::setIcon()
{
    auto loadIcon = [] (const void* data, size_t size) -> Image
    {
        return ImageFileFormat::loadFrom (data, size);
    };

   #if JUCE_MAC
    if (exec ("defaults read -g AppleInterfaceStyle").compare ("Dark") == 1)
    {
        const Image icon = loadIcon (BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
        setIconImage (icon, icon);
    }
    else
    {
        const Image icon = loadIcon (BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
        setIconImage (icon, icon);
    }
   #else
    const String color = getSettings().getIconColour();
    Image icon;

    if (color.equalsIgnoreCase ("white"))
        icon = loadIcon (BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
    else if (color.equalsIgnoreCase ("black"))
        icon = loadIcon (BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);

    setIconImage (icon, icon);
   #endif
}

#if JUCE_MAC
std::string IconMenu::exec (const char* cmd)
{
    std::shared_ptr<FILE> pipe (popen (cmd, "r"), pclose);
    if (! pipe)
        return "ERROR";

    char buffer[128];
    std::string result;

    while (! feof (pipe.get()))
    {
        if (fgets (buffer, 128, pipe.get()) != nullptr)
            result += buffer;
    }

    return result;
}
#endif

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader (JUCEApplication::getInstance()->getApplicationName());

    if (menuIconLeftClicked)
    {
        const auto& settings = getSettings();

        menu.addItem (LeftMenu::settings, "Settings...");
        menu.addItem (LeftMenu::editPlugins, "Edit Plugins");
        menu.addSeparator();
        menu.addSectionHeader ("Audio Processing");
        menu.addItem (LeftMenu::aecToggle, "Echo Cancellation (AEC)", true, settings.isAecEnabled());
        menu.addItem (LeftMenu::nrToggle, "Noise Reduction", true, settings.isNrEnabled());

        if (settings.isAecEnabled())
            menu.addItem (LeftMenu::aecMonitor, "AEC Monitor...");

        menu.addSeparator();
        menu.addSectionHeader ("Available Plugins");
        plugins.getKnownPlugins().addToMenu (menu, plugins.getSortMethod());
    }
    else
    {
        menu.addItem (RightMenu::quit, "Quit");
        menu.addSeparator();
        menu.addItem (RightMenu::deletePluginStates, "Delete Plugin States");
       #if ! JUCE_MAC
        menu.addItem (RightMenu::invertIconColour, "Invert Icon Color");
       #endif
    }

   #if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this),
                        ModalCallbackFunction::forComponent (menuInvocationCallback, this));
   #else
    if (x == 0 || y == 0)
    {
        POINT iconLocation {};
        GetCursorPos (&iconLocation);
        x = iconLocation.x;
        y = iconLocation.y;
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (x, y, 1, 1)),
                        ModalCallbackFunction::forComponent (menuInvocationCallback, this));
   #endif
}

void IconMenu::mouseDown (const MouseEvent& e)
{
   #if JUCE_MAC
    Process::setDockIconVisible (true);
   #endif
    Process::makeForegroundProcess();
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    startTimer (50);
}

void IconMenu::menuInvocationCallback (int id, IconMenu* im)
{
   #if JUCE_MAC
    if (id == 0 && ! PluginWindow::containsActiveWindows())
        Process::setDockIconVisible (false);
   #endif

    if (id == 0)
        return;

    if (im->menuIconLeftClicked)
        im->handleLeftClickMenu (id);
    else
        im->handleRightClickMenu (id);
}

void IconMenu::handleRightClickMenu (int id)
{
    switch (id)
    {
        case RightMenu::quit:
            engine.savePluginStates();
            JUCEApplication::getInstance()->systemRequestedQuit();
            break;

        case RightMenu::deletePluginStates:
            engine.clearPluginStates();
            engine.rebuildGraph();
            break;

        case RightMenu::invertIconColour:
            getSettings().setIconColour (getSettings().getIconColour().equalsIgnoreCase ("black") ? "white" : "black");
            setIcon();
            break;

        default:
            break;
    }
}

void IconMenu::handleLeftClickMenu (int id)
{
    auto& settings = getSettings();

    switch (id)
    {
        case LeftMenu::settings:
            openSettingsWindow();
            return;

        case LeftMenu::editPlugins:
        {
            juce::Component::SafePointer<IconMenu> safe (this);
            juce::MessageManager::callAsync ([safe]
            {
                if (safe != nullptr)
                    safe->openPluginListWindow();
            });
            return;
        }

        case LeftMenu::aecToggle:
            settings.setAecEnabled (! settings.isAecEnabled());
            engine.rebuildGraph();
            return;

        case LeftMenu::nrToggle:
            settings.setNrEnabled (! settings.isNrEnabled());
            engine.rebuildGraph();
            return;

        case LeftMenu::aecMonitor:
            openAecMonitorWindow();
            return;

        default:
            break;
    }

    const int knownIndex = plugins.getKnownPlugins().getIndexChosenByMenu (id);

    if (knownIndex >= 0)
    {
        engine.addPlugin (*plugins.getKnownPlugins().getType (knownIndex));

        // 加完外掛後重新打開選單，方便連續加
        startTimer (50);
    }
}

void IconMenu::openPluginEditor (int sortedIndex)
{
    auto node = engine.getPluginNode (sortedIndex);

    if (node == nullptr)
    {
        juce::String message = "Could not open the plugin editor.";
        const auto err = engine.getPluginLoadError (sortedIndex);

        if (err.isNotEmpty())
            message += "\n\n" + err;
        else
            message += "\n\nThe plugin is not loaded in the audio graph. Open Edit Plugins and rescan your VST3 plugins.";

        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Light Host",
                                                message);
        return;
    }

    engine.suspendProcessing (true);
    auto* processor = node->getProcessor();
    processor->suspendProcessing (true);
    PluginWindow* window = PluginWindow::getWindowFor (node, PluginWindow::Normal);
    processor->suspendProcessing (false);
    engine.suspendProcessing (false);

    if (window != nullptr)
    {
        PluginWindow::showAndFocus (window);
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Light Host",
                                                "This plugin does not provide an editor interface.");
    }
}

void IconMenu::openPluginListWindow()
{
    if (pluginListWindow == nullptr)
        pluginListWindow = std::make_unique<PluginListWindow> (*this);
    else
        pluginListWindow->setVisible (true);

    pluginListWindow->centreWithSize (pluginListWindow->getWidth(), pluginListWindow->getHeight());
    pluginListWindow->toFront (true);
    pluginListWindow->setAlwaysOnTop (true);
    pluginListWindow->setAlwaysOnTop (false);
}
