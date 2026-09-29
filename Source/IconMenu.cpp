#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "PluginWindow.h"
#include "AppTheme.h"
#include "ui/AecMonitorWindow.h"
#include "ui/SettingsWindow.h"
#include "dsp/GraphNodeIds.h"
#include "dsp/AecProcessor.h"
#include "dsp/NoiseReducerProcessor.h"
#include "dsp/OutputReferenceTap.h"
#include "audio/LoopbackDevices.h"
#include <ctime>
#include <limits.h>
#include <map>
#if JUCE_WINDOWS
 #include <Windows.h>
#endif

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
    PluginListWindow (IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
        : DocumentWindow ("Available Plugins", getDialogBackgroundColour(),
                          DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          owner (owner_)
    {
        const File deadMansPedalFile (getAppProperties().getUserSettings()
                                          ->getFile().getSiblingFile ("RecentlyCrashedPluginsList"));

        setContentOwned (new PluginListComponent (pluginFormatManager,
                                                  owner.knownPluginList,
                                                  deadMansPedalFile,
                                                  getAppProperties().getUserSettings()), true);

        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (300, 400, 800, 1500);
        setSize (500, 450);
        setTopLeftPosition (60, 60);
        centreWithSize (getWidth(), getHeight());

        restoreWindowStateFromString (getAppProperties().getUserSettings()->getValue ("listWindowPos"));
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    ~PluginListWindow() override
    {
        getAppProperties().getUserSettings()->setValue ("listWindowPos", getWindowStateAsString());
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        owner.removePluginsLackingInputOutput();
       #if JUCE_MAC
        Process::setDockIconVisible (false);
       #endif
        owner.pluginListWindow = nullptr;
    }

private:
    IconMenu& owner;
};

namespace
{
    void configurePluginForGraph (juce::AudioPluginInstance& plugin)
    {
        plugin.enableAllBuses();

        const juce::AudioProcessor::BusesLayout stereo
        {
            juce::AudioChannelSet::stereo(),
            juce::AudioChannelSet::stereo()
        };

        if (plugin.checkBusesLayoutSupported (stereo))
        {
            plugin.setBusesLayout (stereo);
            return;
        }

        const juce::AudioProcessor::BusesLayout monoStereo
        {
            juce::AudioChannelSet::mono(),
            juce::AudioChannelSet::stereo()
        };

        if (plugin.checkBusesLayoutSupported (monoStereo))
            plugin.setBusesLayout (monoStereo);
    }
}

IconMenu::IconMenu()
    : INDEX_EDIT (1000000),
      INDEX_BYPASS (2000000),
      INDEX_DELETE (3000000),
      INDEX_MOVE_UP (4000000),
      INDEX_MOVE_DOWN (5000000),
      INDEX_AEC_TOGGLE (6000000),
      INDEX_NR_TOGGLE (6100000),
      INDEX_AEC_MONITOR (6150000),
      INDEX_SPEAKER_REF_BASE (6200000),
      INDEX_SPEAKER_GAIN_BASE (6300000),
      INDEX_AEC_STRENGTH_BASE (6400000)
{
    formatManager.addDefaultFormats();

    loopbackCapture = std::make_unique<LoopbackCapture>();

    if (auto savedAudioState = getAppProperties().getUserSettings()->getXmlValue ("audioDeviceState"))
        deviceManager.initialise (256, 256, savedAudioState.get(), true);
    else
        deviceManager.initialise (256, 256, nullptr, true);

    if (auto savedPluginList = getAppProperties().getUserSettings()->getXmlValue ("pluginList"))
        knownPluginList.recreateFromXml (*savedPluginList);

    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener (this);

    if (auto savedPluginListActive = getAppProperties().getUserSettings()->getXmlValue ("pluginListActive"))
        activePluginList.recreateFromXml (*savedPluginListActive);

    if (! getAppProperties().getUserSettings()->containsKey ("aecEnabled"))
        getAppProperties().getUserSettings()->setValue ("aecEnabled", true);

    if (! getAppProperties().getUserSettings()->containsKey ("nrEnabled"))
        getAppProperties().getUserSettings()->setValue ("nrEnabled", true);

    if (! getAppProperties().getUserSettings()->containsKey ("aecReferenceGainDb"))
        getAppProperties().getUserSettings()->setValue ("aecReferenceGainDb", 0.0);

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&player);

    loadActivePlugins();
    activePluginList.addChangeListener (this);
    setIcon();
    setIconTooltip (JUCEApplication::getInstance()->getApplicationName());
}

IconMenu::~IconMenu()
{
    loopbackCapture->stop();
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
    savePluginStates();
}

bool IconMenu::isAecEnabled() const
{
    return getAppProperties().getUserSettings()->getBoolValue ("aecEnabled", true);
}

bool IconMenu::isNrEnabled() const
{
    return getAppProperties().getUserSettings()->getBoolValue ("nrEnabled", true);
}

void IconMenu::setAecEnabled (bool enabled)
{
    getAppProperties().getUserSettings()->setValue ("aecEnabled", enabled);
    getAppProperties().saveIfNeeded();
}

void IconMenu::setNrEnabled (bool enabled)
{
    getAppProperties().getUserSettings()->setValue ("nrEnabled", enabled);
    getAppProperties().saveIfNeeded();
}

juce::String IconMenu::getReferenceDeviceId() const
{
    return getAppProperties().getUserSettings()->getValue ("aecReferenceDeviceId");
}

void IconMenu::setReferenceDeviceId (const juce::String& deviceId)
{
    getAppProperties().getUserSettings()->setValue ("aecReferenceDeviceId", deviceId);
    getAppProperties().saveIfNeeded();
}

float IconMenu::getReferenceGainDb() const
{
    return (float) getAppProperties().getUserSettings()->getDoubleValue ("aecReferenceGainDb", 0.0);
}

void IconMenu::setReferenceGainDb (float gainDb)
{
    getAppProperties().getUserSettings()->setValue ("aecReferenceGainDb", gainDb);
    getAppProperties().saveIfNeeded();
}

float IconMenu::getAecStrengthPercent() const
{
    return (float) getAppProperties().getUserSettings()->getDoubleValue ("aecStrengthPercent", 100.0);
}

void IconMenu::setAecStrengthPercent (float strengthPercent)
{
    getAppProperties().getUserSettings()->setValue ("aecStrengthPercent", strengthPercent);
    getAppProperties().saveIfNeeded();
}

AecProcessor* IconMenu::getAecProcessor() const
{
    if (auto node = graph.getNodeForId (GraphNodeIds::aecId()))
        return dynamic_cast<AecProcessor*> (node->getProcessor());

    return nullptr;
}

void IconMenu::restartAecReferenceCapture()
{
    updateLoopbackCapture (getAecProcessor());
}

AecMonitorSnapshot IconMenu::getAecMonitorSnapshot() const
{
    AecMonitorSnapshot snap;
    snap.aecEnabled = isAecEnabled();
    snap.referenceGainDb = getReferenceGainDb();
    snap.aecStrengthPercent = getAecStrengthPercent();

   #if JUCE_WINDOWS
    const bool useSystemLoopback = shouldUseSystemLoopbackReference();
   #else
    const bool useSystemLoopback = false;
   #endif

    if (useSystemLoopback)
        snap.loopbackStatus = (loopbackCapture != nullptr && loopbackCapture->isRunning()) ? "Running" : "Not running";
    else
        snap.loopbackStatus = "Not used (App Output)";

   #if JUCE_WINDOWS
    if (useSystemLoopback)
    {
        const juce::String deviceId = getReferenceDeviceId();

        if (deviceId.isEmpty())
            snap.referenceSource = "System Loopback (Default: " + getDefaultLoopbackDeviceName() + ")";
        else
        {
            snap.referenceSource = "System Loopback";

            for (const auto& device : enumerateLoopbackOutputDevices())
            {
                if (device.id == deviceId)
                {
                    snap.referenceSource += " (" + device.name + ")";
                    break;
                }
            }
        }
    }
    else
    {
        snap.referenceSource = "App Output";
    }
   #else
    snap.referenceSource = "App Output";
   #endif

    if (auto* aec = getAecProcessor())
    {
        const auto stats = aec->getStats();
        snap.referenceDelayMs = stats.referenceDelayMs;
        snap.referenceTargetDelayMs = stats.referenceTargetDelayMs;
        snap.referenceLevelDb = stats.referenceLevelDb;
        snap.micRawLevelDb = stats.micRawLevelDb;
        snap.micLevelDb = stats.micLevelDb;
        snap.outputLevelDb = stats.outputLevelDb;
        snap.echoRemovedDb = stats.echoRemovedDb;
        snap.erleDb = stats.erleDb;
        snap.referenceSamplesReceived = stats.referenceSamplesReceived;
        snap.samplesProcessed = stats.samplesProcessed;
        snap.referenceUnderruns = stats.referenceUnderruns;
    }

    return snap;
}

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

void IconMenu::saveAudioDeviceStateAndReload()
{
    if (auto audioState = deviceManager.createStateXml())
    {
        getAppProperties().getUserSettings()->setValue ("audioDeviceState", audioState.get());
        getAppProperties().getUserSettings()->saveIfNeeded();
    }

    loadActivePlugins();
}

void IconMenu::reloadActivePlugins()
{
    loadActivePlugins();
}

void IconMenu::setUseSystemLoopbackReference (bool use)
{
    getAppProperties().getUserSettings()->setValue ("aecUseSystemLoopback", use);
    getAppProperties().saveIfNeeded();
}

std::vector<PluginDescription> IconMenu::getTimeSortedPluginList() const
{
    return const_cast<IconMenu*> (this)->getTimeSortedList();
}

bool IconMenu::isPluginBypassed (const PluginDescription& plugin) const
{
    return getAppProperties().getUserSettings()->getBoolValue (getKey ("bypass", plugin));
}

void IconMenu::togglePluginBypass (int sortedIndex)
{
    const std::vector<PluginDescription> timeSorted = getTimeSortedList();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) timeSorted.size()))
        return;

    const String key = getKey ("bypass", timeSorted[(size_t) sortedIndex]);
    const bool bypassed = getAppProperties().getUserSettings()->getBoolValue (key);
    getAppProperties().getUserSettings()->setValue (key, ! bypassed);
    getAppProperties().saveIfNeeded();
    savePluginStates();
    loadActivePlugins();
}

void IconMenu::deletePluginAtSortedIndex (int sortedIndex)
{
    deletePluginStates();

    const std::vector<PluginDescription> timeSorted = getTimeSortedList();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) timeSorted.size()))
        return;

    const String key = getKey ("order", timeSorted[(size_t) sortedIndex]);
    int unsortedIndex = 0;

    for (int i = 0; i < activePluginList.getNumTypes(); ++i)
    {
        const PluginDescription current = *activePluginList.getType (i);

        if (key.equalsIgnoreCase (getKey ("order", current)))
        {
            unsortedIndex = i;
            break;
        }
    }

    getAppProperties().getUserSettings()->removeValue (key);
    getAppProperties().getUserSettings()->removeValue (getKey ("bypass", timeSorted[(size_t) sortedIndex]));
    getAppProperties().saveIfNeeded();
    activePluginList.removeType (*activePluginList.getType (unsortedIndex));
    savePluginStates();
    loadActivePlugins();
}

void IconMenu::movePluginUp (int sortedIndex)
{
    if (sortedIndex <= 0)
        return;

    const std::vector<PluginDescription> timeSorted = getTimeSortedList();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) timeSorted.size()))
        return;

    savePluginStates();

    const String currentKey = getKey ("order", timeSorted[(size_t) sortedIndex]);
    const String previousKey = getKey ("order", timeSorted[(size_t) (sortedIndex - 1)]);
    const int currentOrder = getAppProperties().getUserSettings()->getValue (currentKey).getIntValue();
    const int previousOrder = getAppProperties().getUserSettings()->getValue (previousKey).getIntValue();

    getAppProperties().getUserSettings()->setValue (currentKey, previousOrder);
    getAppProperties().getUserSettings()->setValue (previousKey, currentOrder);
    getAppProperties().saveIfNeeded();
    loadActivePlugins();
}

void IconMenu::movePluginDown (int sortedIndex)
{
    const std::vector<PluginDescription> timeSorted = getTimeSortedList();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) timeSorted.size() - 1))
        return;

    savePluginStates();

    const String currentKey = getKey ("order", timeSorted[(size_t) sortedIndex]);
    const String nextKey = getKey ("order", timeSorted[(size_t) (sortedIndex + 1)]);
    const int currentOrder = getAppProperties().getUserSettings()->getValue (currentKey).getIntValue();
    const int nextOrder = getAppProperties().getUserSettings()->getValue (nextKey).getIntValue();

    getAppProperties().getUserSettings()->setValue (currentKey, nextOrder);
    getAppProperties().getUserSettings()->setValue (nextKey, currentOrder);
    getAppProperties().saveIfNeeded();
    loadActivePlugins();
}

void IconMenu::addActivePlugin (const PluginDescription& plugin)
{
    const String key = getKey ("order", plugin);
    getAppProperties().getUserSettings()->setValue (key, (int) std::time (nullptr));
    getAppProperties().saveIfNeeded();
    activePluginList.addType (plugin);
    savePluginStates();
    loadActivePlugins();
}

void IconMenu::updateLoopbackCapture (AecProcessor* aecProcessor)
{
    loopbackCapture->stop();
    loopbackCapture->setReferenceDeviceId (getReferenceDeviceId());
    loopbackCapture->setReferenceGainDb (getReferenceGainDb());

    // ASIO 模式下參考訊號改由 OutputReferenceTap 從 graph 輸出取得
    if (aecProcessor != nullptr && isAecEnabled() && shouldUseSystemLoopbackReference())
    {
        loopbackCapture->setReferenceConsumer (aecProcessor);
        loopbackCapture->start();
    }
    else
    {
        loopbackCapture->setReferenceConsumer (nullptr);
    }
}

bool IconMenu::shouldUseSystemLoopbackReference() const
{
    return getAppProperties().getUserSettings()->getBoolValue ("aecUseSystemLoopback", false);
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
    String defaultColor;
   #if JUCE_WINDOWS
    defaultColor = "white";
   #elif JUCE_LINUX
    defaultColor = "black";
   #endif
    if (! getAppProperties().getUserSettings()->containsKey ("icon"))
        getAppProperties().getUserSettings()->setValue ("icon", defaultColor);

    const String color = getAppProperties().getUserSettings()->getValue ("icon");
    Image icon;

    if (color.equalsIgnoreCase ("white"))
        icon = loadIcon (BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
    else if (color.equalsIgnoreCase ("black"))
        icon = loadIcon (BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);

    setIconImage (icon, icon);
   #endif
}

void IconMenu::loadActivePlugins()
{
    using namespace GraphNodeIds;
    constexpr auto noRebuild = juce::AudioProcessorGraph::UpdateKind::none;

    deviceManager.removeAudioCallback (&player);
    PluginWindow::closeAllCurrentlyOpenWindows();
    loopbackCapture->setReferenceConsumer (nullptr);
    loopbackCapture->stop();
    graph.clear (noRebuild);

    double sampleRate = 44100.0;
    int blockSize = 512;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize = device->getCurrentBufferSizeSamples();
    }

    inputNode = graph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                                   AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode),
                               inputId(),
                               noRebuild)
                    .get();

    outputNode = graph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                                    AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode),
                                outputId(),
                                noRebuild)
                     .get();

    AecProcessor* aecPtr = nullptr;
    AudioProcessorGraph::NodeID chainOut = inputId();

    if (isAecEnabled())
    {
        const auto aecNodeId = aecId();
        auto aecNode = graph.addNode (std::make_unique<AecProcessor>(), aecNodeId, noRebuild);
        aecPtr = dynamic_cast<AecProcessor*> (aecNode->getProcessor());

        if (aecPtr != nullptr)
            aecPtr->setStrength (getAecStrengthPercent());
        graph.addConnection ({ { chainOut, channelLeft  }, { aecNodeId, channelLeft  } }, noRebuild);
        graph.addConnection ({ { chainOut, channelRight }, { aecNodeId, channelRight } }, noRebuild);
        chainOut = aecNodeId;
    }

    if (isNrEnabled())
    {
        const auto nrNodeId = nrId();
        graph.addNode (std::make_unique<NoiseReducerProcessor>(), nrNodeId, noRebuild);
        graph.addConnection ({ { chainOut, channelLeft  }, { nrNodeId, channelLeft  } }, noRebuild);
        graph.addConnection ({ { chainOut, channelRight }, { nrNodeId, channelRight } }, noRebuild);
        chainOut = nrNodeId;
    }

    const std::vector<PluginDescription> sortedPlugins = getTimeSortedList();
    AudioProcessorGraph::NodeID lastId = chainOut;
    bool hasInputConnected = false;
    pluginGraphNodeIds.clear();
    pluginGraphNodeIds.resize (sortedPlugins.size(), 0);
    pluginLoadErrors.clear();
    uint32 nextPluginNodeId = 1;

    for (int i = 0; i < (int) sortedPlugins.size(); ++i)
    {
        const PluginDescription& plugin = sortedPlugins[(size_t) i];
        String errorMessage;

        auto instance = formatManager.createPluginInstance (plugin,
                                                            sampleRate,
                                                            blockSize,
                                                            errorMessage);

        if (instance == nullptr)
        {
            if (errorMessage.isEmpty())
                errorMessage = "Unknown error (check that the plugin is VST3 and still installed).";

            pluginLoadErrors[getKey ("state", plugin)] = plugin.name + ": " + errorMessage;
            continue;
        }

        configurePluginForGraph (*instance);

        const uint32 nodeIndex = nextPluginNodeId++;
        pluginGraphNodeIds[(size_t) i] = nodeIndex;

        const String pluginUid = getKey ("state", plugin);
        const String savedPluginState = getAppProperties().getUserSettings()->getValue (pluginUid);
        MemoryBlock savedPluginBinary;
        savedPluginBinary.fromBase64Encoding (savedPluginState);

        const auto addedNode = graph.addNode (std::move (instance),
                                              AudioProcessorGraph::NodeID { nodeIndex },
                                              noRebuild);

        if (addedNode != nullptr && savedPluginBinary.getSize() > 0)
            addedNode->getProcessor()->setStateInformation (savedPluginBinary.getData(),
                                                            (int) savedPluginBinary.getSize());

        const String key = getKey ("bypass", plugin);
        const bool bypass = getAppProperties().getUserSettings()->getBoolValue (key, false);
        const auto pluginNodeId = AudioProcessorGraph::NodeID { nodeIndex };

        if ((! hasInputConnected) && (! bypass))
        {
            graph.addConnection ({ { chainOut, channelLeft  }, { pluginNodeId, channelLeft  } }, noRebuild);
            graph.addConnection ({ { chainOut, channelRight }, { pluginNodeId, channelRight } }, noRebuild);
            hasInputConnected = true;
        }
        else if (! bypass)
        {
            graph.addConnection ({ { lastId, channelLeft  }, { pluginNodeId, channelLeft  } }, noRebuild);
            graph.addConnection ({ { lastId, channelRight }, { pluginNodeId, channelRight } }, noRebuild);
        }

        if (! bypass)
            lastId = pluginNodeId;
    }

    connectChainToOutput (lastId, aecPtr);
    graph.removeIllegalConnections (noRebuild);

    reprepareAudioGraph();
    updateLoopbackCapture (aecPtr);
    deviceManager.addAudioCallback (&player);
}

void IconMenu::connectChainToOutput (juce::AudioProcessorGraph::NodeID lastId, AecProcessor* aecPtr)
{
    using namespace GraphNodeIds;
    constexpr auto noRebuild = juce::AudioProcessorGraph::UpdateKind::none;

    if (isAecEnabled() && aecPtr != nullptr)
    {
        const auto tapNodeId = refTapId();
        auto tapNode = graph.addNode (std::make_unique<OutputReferenceTap> (aecPtr), tapNodeId, noRebuild);

        if (auto* tap = dynamic_cast<OutputReferenceTap*> (tapNode->getProcessor()))
        {
            tap->setReferenceGain (juce::Decibels::decibelsToGain (getReferenceGainDb()));
            tap->setFeedsReference (! shouldUseSystemLoopbackReference());
        }

        graph.addConnection ({ { lastId, channelLeft  }, { tapNodeId, channelLeft  } }, noRebuild);
        graph.addConnection ({ { lastId, channelRight }, { tapNodeId, channelRight } }, noRebuild);
        graph.addConnection ({ { tapNodeId, channelLeft  }, { outputId(), channelLeft  } }, noRebuild);
        graph.addConnection ({ { tapNodeId, channelRight }, { outputId(), channelRight } }, noRebuild);
    }
    else
    {
        graph.addConnection ({ { lastId, channelLeft  }, { outputId(), channelLeft  } }, noRebuild);
        graph.addConnection ({ { lastId, channelRight }, { outputId(), channelRight } }, noRebuild);
    }
}

void IconMenu::reprepareAudioGraph()
{
    graph.rebuild();
}

void IconMenu::changeListenerCallback (ChangeBroadcaster* changed)
{
    if (changed == &knownPluginList)
    {
        if (auto savedPluginList = knownPluginList.createXml())
        {
            getAppProperties().getUserSettings()->setValue ("pluginList", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
    else if (changed == &activePluginList)
    {
        if (auto savedPluginList = activePluginList.createXml())
        {
            getAppProperties().getUserSettings()->setValue ("pluginListActive", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
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
        menu.addItem (1, "Settings...");
        menu.addItem (2, "Edit Plugins");
        menu.addSeparator();
        menu.addSectionHeader ("Audio Processing");
        menu.addItem (INDEX_AEC_TOGGLE, "Echo Cancellation (AEC)", true, isAecEnabled());
        menu.addItem (INDEX_NR_TOGGLE, "Noise Reduction", true, isNrEnabled());

        if (isAecEnabled())
            menu.addItem (INDEX_AEC_MONITOR, "AEC Monitor...");

        menu.addSeparator();
        menu.addSectionHeader ("Available Plugins");
        knownPluginList.addToMenu (menu, pluginSortMethod);
    }
    else
    {
        menu.addItem (1, "Quit");
        menu.addSeparator();
        menu.addItem (2, "Delete Plugin States");
       #if ! JUCE_MAC
        menu.addItem (3, "Invert Icon Color");
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
    if ((! im->menuIconLeftClicked))
    {
        if (id == 1)
        {
            im->savePluginStates();
            JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }

        if (id == 2)
        {
            im->deletePluginStates();
            im->loadActivePlugins();
            return;
        }

        if (id == 3)
        {
            const String color = getAppProperties().getUserSettings()->getValue ("icon");
            getAppProperties().getUserSettings()->setValue ("icon", color.equalsIgnoreCase ("black") ? "white" : "black");
            im->setIcon();
            return;
        }
    }

   #if JUCE_MAC
    if (id == 0 && ! PluginWindow::containsActiveWindows())
        Process::setDockIconVisible (false);
   #endif

    if (id == 1)
        im->openSettingsWindow();

    if (id == 2)
    {
        juce::Component::SafePointer<IconMenu> safe (im);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->reloadPlugins();
        });
        return;
    }

    if (id == im->INDEX_AEC_TOGGLE)
    {
        im->setAecEnabled (! im->isAecEnabled());
        im->loadActivePlugins();
        return;
    }

    if (id == im->INDEX_NR_TOGGLE)
    {
        im->setNrEnabled (! im->isNrEnabled());
        im->loadActivePlugins();
        return;
    }

    if (id == im->INDEX_AEC_MONITOR)
    {
        im->openAecMonitorWindow();
        return;
    }

    if (id >= im->INDEX_SPEAKER_REF_BASE && id < im->INDEX_SPEAKER_REF_BASE + 1000)
    {
       #if JUCE_WINDOWS
        const int deviceIndex = id - im->INDEX_SPEAKER_REF_BASE;

        if (deviceIndex == 0)
        {
            getAppProperties().getUserSettings()->setValue ("aecUseSystemLoopback", false);
            getAppProperties().saveIfNeeded();
            im->loadActivePlugins();
        }
        else if (deviceIndex == 999)
        {
            getAppProperties().getUserSettings()->setValue ("aecUseSystemLoopback", true);
            getAppProperties().saveIfNeeded();
            im->loadActivePlugins();
        }
        else if (deviceIndex == 1)
        {
            im->setReferenceDeviceId ({});
            getAppProperties().getUserSettings()->setValue ("aecUseSystemLoopback", true);
            getAppProperties().saveIfNeeded();
            im->loadActivePlugins();
        }
        else
        {
            const juce::Array<LoopbackDeviceInfo> outputDevices = enumerateLoopbackOutputDevices();
            const int listIndex = deviceIndex - 2;

            if (juce::isPositiveAndBelow (listIndex, outputDevices.size()))
            {
                im->setReferenceDeviceId (outputDevices.getReference (listIndex).id);
                getAppProperties().getUserSettings()->setValue ("aecUseSystemLoopback", true);
                getAppProperties().saveIfNeeded();
                im->loadActivePlugins();
            }
        }
       #endif
        return;
    }

    if (id >= im->INDEX_AEC_STRENGTH_BASE && id < im->INDEX_AEC_STRENGTH_BASE + 100)
    {
        static constexpr float strengthOptions[] = { 25.0f, 50.0f, 75.0f, 100.0f, 125.0f, 150.0f };
        const int strengthIndex = id - im->INDEX_AEC_STRENGTH_BASE;

        if (juce::isPositiveAndBelow (strengthIndex, (int) std::size (strengthOptions)))
        {
            im->setAecStrengthPercent (strengthOptions[strengthIndex]);

            if (auto* aec = im->getAecProcessor())
                aec->setStrength (strengthOptions[strengthIndex]);
        }

        return;
    }

    if (id >= im->INDEX_SPEAKER_GAIN_BASE && id < im->INDEX_SPEAKER_GAIN_BASE + 100)
    {
        static constexpr float gainOptions[] = { -40.0f, -30.0f, -20.0f, -10.0f, 0.0f, 10.0f, 20.0f, 30.0f, 40.0f };
        const int gainIndex = id - im->INDEX_SPEAKER_GAIN_BASE;

        if (juce::isPositiveAndBelow (gainIndex, (int) std::size (gainOptions)))
        {
            im->setReferenceGainDb (gainOptions[gainIndex]);
            im->loadActivePlugins();
        }

        return;
    }

    if (id > 2)
    {
        if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        {
            im->deletePluginStates();

            const int index = id - im->INDEX_DELETE;
            const std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            const String key = getKey ("order", timeSorted[(size_t) index]);
            int unsortedIndex = 0;

            for (int i = 0; i < im->activePluginList.getNumTypes(); ++i)
            {
                const PluginDescription current = *im->activePluginList.getType (i);
                if (key.equalsIgnoreCase (getKey ("order", current)))
                {
                    unsortedIndex = i;
                    break;
                }
            }

            getAppProperties().getUserSettings()->removeValue (key);
            getAppProperties().getUserSettings()->removeValue (getKey ("bypass", timeSorted[(size_t) index]));
            getAppProperties().saveIfNeeded();
            im->activePluginList.removeType (*im->activePluginList.getType (unsortedIndex));
            im->savePluginStates();
            im->loadActivePlugins();
        }
        else if (im->knownPluginList.getIndexChosenByMenu (id) > -1)
        {
            const PluginDescription plugin = *im->knownPluginList.getType (im->knownPluginList.getIndexChosenByMenu (id));
            const String key = getKey ("order", plugin);
            getAppProperties().getUserSettings()->setValue (key, (int) std::time (nullptr));
            getAppProperties().saveIfNeeded();
            im->activePluginList.addType (plugin);
            im->savePluginStates();
            im->loadActivePlugins();
        }
        else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
        {
            const int index = id - im->INDEX_BYPASS;
            const std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            const String key = getKey ("bypass", timeSorted[(size_t) index]);
            const bool bypassed = getAppProperties().getUserSettings()->getBoolValue (key);
            getAppProperties().getUserSettings()->setValue (key, ! bypassed);
            getAppProperties().saveIfNeeded();
            im->savePluginStates();
            im->loadActivePlugins();
        }
        else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        {
            const int index = id - im->INDEX_EDIT;
            juce::Component::SafePointer<IconMenu> safe (im);
            juce::MessageManager::callAsync ([safe, index]
            {
                if (safe != nullptr)
                    safe->openPluginEditorForSortedIndex (index);
            });
            return;
        }
        else if (id >= im->INDEX_MOVE_UP && id < im->INDEX_MOVE_UP + 1000000)
        {
            im->movePluginUp (id - im->INDEX_MOVE_UP);
        }
        else if (id >= im->INDEX_MOVE_DOWN && id < im->INDEX_MOVE_DOWN + 1000000)
        {
            im->movePluginDown (id - im->INDEX_MOVE_DOWN);
        }

        im->startTimer (50);
    }
}

void IconMenu::ensurePluginOrderKeys()
{
    bool needsSave = false;

    for (int i = 0; i < activePluginList.getNumTypes(); ++i)
    {
        const PluginDescription plugin = *activePluginList.getType (i);
        const String key = getKey ("order", plugin);

        if (! getAppProperties().getUserSettings()->containsKey (key)
            || getAppProperties().getUserSettings()->getValue (key).getIntValue() <= 0)
        {
            getAppProperties().getUserSettings()->setValue (key, i + 1);
            needsSave = true;
        }
    }

    if (needsSave)
        getAppProperties().saveIfNeeded();
}

std::vector<PluginDescription> IconMenu::getTimeSortedList()
{
    ensurePluginOrderKeys();

    struct Entry
    {
        int order;
        PluginDescription plugin;
    };

    std::vector<Entry> entries;
    entries.reserve ((size_t) activePluginList.getNumTypes());

    for (int i = 0; i < activePluginList.getNumTypes(); ++i)
    {
        const PluginDescription plugin = *activePluginList.getType (i);
        const String key = getKey ("order", plugin);
        const int order = getAppProperties().getUserSettings()->getValue (key).getIntValue();
        entries.push_back ({ order, plugin });
    }

    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b) { return a.order < b.order; });

    std::vector<PluginDescription> list;
    list.reserve (entries.size());

    for (const auto& entry : entries)
        list.push_back (entry.plugin);

    return list;
}

String IconMenu::getKey (String type, PluginDescription plugin)
{
    return "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
}

void IconMenu::deletePluginStates()
{
    const std::vector<PluginDescription> list = getTimeSortedList();
    for (int i = 0; i < activePluginList.getNumTypes(); ++i)
    {
        const String pluginUid = getKey ("state", list[(size_t) i]);
        getAppProperties().getUserSettings()->removeValue (pluginUid);
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::savePluginStates()
{
    const std::vector<PluginDescription> list = getTimeSortedList();

    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (i >= (int) pluginGraphNodeIds.size())
            break;

        const uint32 nodeId = pluginGraphNodeIds[(size_t) i];

        if (nodeId == 0)
            continue;

        if (auto* node = graph.getNodeForId (juce::AudioProcessorGraph::NodeID { nodeId }))
        {
            AudioProcessor& processor = *node->getProcessor();
            const String pluginUid = getKey ("state", list[(size_t) i]);
            MemoryBlock savedStateBinary;
            processor.getStateInformation (savedStateBinary);
            getAppProperties().getUserSettings()->setValue (pluginUid, savedStateBinary.toBase64Encoding());
            getAppProperties().saveIfNeeded();
        }
    }
}

void IconMenu::openPluginEditorForSortedIndex (int sortedIndex)
{
    auto node = findGraphNodeForIndex (sortedIndex);

    if (node == nullptr)
    {
        const auto sorted = getTimeSortedList();
        juce::String message = "Could not open the plugin editor.";

        if (juce::isPositiveAndBelow (sortedIndex, (int) sorted.size()))
        {
            const auto err = getPluginLoadError (sorted[(size_t) sortedIndex]);

            if (err.isNotEmpty())
                message += "\n\n" + err;
            else
                message += "\n\nThe plugin is not loaded in the audio graph. Open Edit Plugins and rescan your VST3 plugins.";
        }

        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Light Host",
                                                message);
        return;
    }

    openPluginEditorForNode (std::move (node));
}

juce::String IconMenu::getPluginLoadError (const juce::PluginDescription& desc) const
{
    const auto it = pluginLoadErrors.find (getKey ("state", desc));
    return it != pluginLoadErrors.end() ? it->second : juce::String();
}

AudioProcessorGraph::Node::Ptr IconMenu::findGraphNodeForIndex (int sortedIndex)
{
    const std::vector<PluginDescription> sorted = getTimeSortedList();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) sorted.size()))
        return nullptr;

    const PluginDescription& desc = sorted[(size_t) sortedIndex];

    if (juce::isPositiveAndBelow (sortedIndex, (int) pluginGraphNodeIds.size()))
    {
        const uint32 nodeId = pluginGraphNodeIds[(size_t) sortedIndex];

        if (nodeId != 0)
        {
            for (auto& node : graph.getNodes())
                if (node->nodeID.uid == nodeId)
                    return node;
        }
    }

    for (auto& node : graph.getNodes())
    {
        if (auto* instance = dynamic_cast<AudioPluginInstance*> (node->getProcessor()))
            if (instance->getPluginDescription().isDuplicateOf (desc))
                return node;
    }

    return nullptr;
}

void IconMenu::openPluginEditorForNode (AudioProcessorGraph::Node::Ptr node)
{
    if (node == nullptr)
        return;

    graph.suspendProcessing (true);

    if (auto* processor = node->getProcessor())
        processor->suspendProcessing (true);

    PluginWindow* window = PluginWindow::getWindowFor (node, PluginWindow::Normal);

    if (auto* processor = node->getProcessor())
        processor->suspendProcessing (false);

    graph.suspendProcessing (false);

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

void IconMenu::reloadPlugins()
{
    if (pluginListWindow == nullptr)
        pluginListWindow = std::make_unique<PluginListWindow> (*this, formatManager);
    else
        pluginListWindow->setVisible (true);

    pluginListWindow->centreWithSize (pluginListWindow->getWidth(), pluginListWindow->getHeight());
    pluginListWindow->toFront (true);
    pluginListWindow->setAlwaysOnTop (true);
    pluginListWindow->setAlwaysOnTop (false);
}

void IconMenu::removePluginsLackingInputOutput()
{
    std::vector<int> removeIndex;
    for (int i = 0; i < knownPluginList.getNumTypes(); ++i)
    {
        const PluginDescription* plugin = knownPluginList.getType (i);
        if (plugin->numInputChannels < 2 || plugin->numOutputChannels < 2)
            removeIndex.push_back (i);
    }

    for (int i = 0; i < (int) removeIndex.size(); ++i)
        knownPluginList.removeType (*knownPluginList.getType (removeIndex[(size_t) i] - i));
}
