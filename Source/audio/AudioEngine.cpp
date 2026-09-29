#include "AudioEngine.h"
#include "LoopbackDevices.h"
#include "../AppSettings.h"
#include "../PluginWindow.h"
#include "../plugins/PluginChain.h"
#include "../dsp/AecProcessor.h"
#include "../dsp/GraphNodeIds.h"
#include "../dsp/NoiseReducerProcessor.h"
#include "../dsp/OutputReferenceTap.h"

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

AudioEngine::AudioEngine (PluginChain& pluginsIn)
    : plugins (pluginsIn)
{
    formatManager.addDefaultFormats();

    loopbackCapture = std::make_unique<LoopbackCapture>();

    if (auto savedAudioState = getSettings().getAudioDeviceState())
        deviceManager.initialise (256, 256, savedAudioState.get(), true);
    else
        deviceManager.initialise (256, 256, nullptr, true);

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&player);

    rebuildGraph();
}

AudioEngine::~AudioEngine()
{
    loopbackCapture->stop();
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
    savePluginStates();
}

template <typename Change>
void AudioEngine::changeChain (Change&& change)
{
    // 先照「改之前」的順序存狀態，pluginGraphNodeIds 才對得上
    savePluginStates();
    change();
    rebuildGraph();
}

void AudioEngine::addPlugin (const juce::PluginDescription& plugin)  { changeChain ([&] { plugins.add (plugin); }); }
void AudioEngine::removePlugin (int sortedIndex)                     { changeChain ([&] { plugins.remove (sortedIndex); }); }
void AudioEngine::togglePluginBypass (int sortedIndex)               { changeChain ([&] { plugins.toggleBypass (sortedIndex); }); }
void AudioEngine::movePluginUp (int sortedIndex)                     { changeChain ([&] { plugins.moveUp (sortedIndex); }); }
void AudioEngine::movePluginDown (int sortedIndex)                   { changeChain ([&] { plugins.moveDown (sortedIndex); }); }

void AudioEngine::saveAudioDeviceStateAndRebuild()
{
    if (auto audioState = deviceManager.createStateXml())
        getSettings().setAudioDeviceState (*audioState);

    rebuildGraph();
}

void AudioEngine::setAecStrength (float strengthPercent)
{
    getSettings().setAecStrengthPercent (strengthPercent);

    if (auto* aec = getAecProcessor())
        aec->setStrength (strengthPercent);
}

AecProcessor* AudioEngine::getAecProcessor() const
{
    if (auto node = graph.getNodeForId (GraphNodeIds::aecId()))
        return dynamic_cast<AecProcessor*> (node->getProcessor());

    return nullptr;
}

void AudioEngine::updateLoopbackCapture (AecProcessor* aecProcessor)
{
    const auto& settings = getSettings();

    loopbackCapture->stop();
    loopbackCapture->setReferenceDeviceId (settings.getReferenceDeviceId());
    loopbackCapture->setReferenceGainDb (settings.getReferenceGainDb());

    // ASIO 模式下參考訊號改由 OutputReferenceTap 從 graph 輸出取得
    if (aecProcessor != nullptr && settings.isAecEnabled() && settings.useSystemLoopbackReference())
    {
        loopbackCapture->setReferenceConsumer (aecProcessor);
        loopbackCapture->start();
    }
    else
    {
        loopbackCapture->setReferenceConsumer (nullptr);
    }
}

void AudioEngine::rebuildGraph()
{
    using namespace GraphNodeIds;
    using juce::AudioProcessorGraph;
    constexpr auto noRebuild = AudioProcessorGraph::UpdateKind::none;
    const auto& settings = getSettings();

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

    graph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                       AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode),
                   inputId(),
                   noRebuild);

    graph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                       AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode),
                   outputId(),
                   noRebuild);

    AecProcessor* aecPtr = nullptr;
    AudioProcessorGraph::NodeID chainOut = inputId();

    if (settings.isAecEnabled())
    {
        const auto aecNodeId = aecId();
        auto aecNode = graph.addNode (std::make_unique<AecProcessor>(), aecNodeId, noRebuild);
        aecPtr = dynamic_cast<AecProcessor*> (aecNode->getProcessor());

        if (aecPtr != nullptr)
            aecPtr->setStrength (settings.getAecStrengthPercent());
        graph.addConnection ({ { chainOut, channelLeft  }, { aecNodeId, channelLeft  } }, noRebuild);
        graph.addConnection ({ { chainOut, channelRight }, { aecNodeId, channelRight } }, noRebuild);
        chainOut = aecNodeId;
    }

    if (settings.isNrEnabled())
    {
        const auto nrNodeId = nrId();
        graph.addNode (std::make_unique<NoiseReducerProcessor>(), nrNodeId, noRebuild);
        graph.addConnection ({ { chainOut, channelLeft  }, { nrNodeId, channelLeft  } }, noRebuild);
        graph.addConnection ({ { chainOut, channelRight }, { nrNodeId, channelRight } }, noRebuild);
        chainOut = nrNodeId;
    }

    const std::vector<juce::PluginDescription> sortedPlugins = plugins.getSortedPlugins();
    AudioProcessorGraph::NodeID lastId = chainOut;
    bool hasInputConnected = false;
    pluginGraphNodeIds.clear();
    pluginGraphNodeIds.resize (sortedPlugins.size(), 0);
    pluginLoadErrors.clear();
    juce::uint32 nextPluginNodeId = 1;

    for (int i = 0; i < (int) sortedPlugins.size(); ++i)
    {
        const juce::PluginDescription& plugin = sortedPlugins[(size_t) i];
        juce::String errorMessage;

        auto instance = formatManager.createPluginInstance (plugin,
                                                            sampleRate,
                                                            blockSize,
                                                            errorMessage);

        if (instance == nullptr)
        {
            if (errorMessage.isEmpty())
                errorMessage = "Unknown error (check that the plugin is VST3 and still installed).";

            pluginLoadErrors[AppSettings::getPluginId (plugin)] = plugin.name + ": " + errorMessage;
            continue;
        }

        configurePluginForGraph (*instance);

        const juce::uint32 nodeIndex = nextPluginNodeId++;
        pluginGraphNodeIds[(size_t) i] = nodeIndex;

        juce::MemoryBlock savedPluginBinary;
        savedPluginBinary.fromBase64Encoding (settings.getPluginState (plugin));

        const auto addedNode = graph.addNode (std::move (instance),
                                              AudioProcessorGraph::NodeID { nodeIndex },
                                              noRebuild);

        if (addedNode != nullptr && savedPluginBinary.getSize() > 0)
            addedNode->getProcessor()->setStateInformation (savedPluginBinary.getData(),
                                                            (int) savedPluginBinary.getSize());

        const bool bypass = plugins.isBypassed (plugin);
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

    graph.rebuild();
    updateLoopbackCapture (aecPtr);
    deviceManager.addAudioCallback (&player);
}

void AudioEngine::connectChainToOutput (juce::AudioProcessorGraph::NodeID lastId, AecProcessor* aecPtr)
{
    using namespace GraphNodeIds;
    constexpr auto noRebuild = juce::AudioProcessorGraph::UpdateKind::none;
    const auto& settings = getSettings();

    if (settings.isAecEnabled() && aecPtr != nullptr)
    {
        const auto tapNodeId = refTapId();
        auto tapNode = graph.addNode (std::make_unique<OutputReferenceTap> (aecPtr), tapNodeId, noRebuild);

        if (auto* tap = dynamic_cast<OutputReferenceTap*> (tapNode->getProcessor()))
        {
            tap->setReferenceGain (juce::Decibels::decibelsToGain (settings.getReferenceGainDb()));
            tap->setFeedsReference (! settings.useSystemLoopbackReference());
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

void AudioEngine::savePluginStates()
{
    const auto list = plugins.getSortedPlugins();

    for (int i = 0; i < (int) list.size() && i < (int) pluginGraphNodeIds.size(); ++i)
    {
        const juce::uint32 nodeId = pluginGraphNodeIds[(size_t) i];

        if (nodeId == 0)
            continue;

        if (auto* node = graph.getNodeForId (juce::AudioProcessorGraph::NodeID { nodeId }))
        {
            juce::MemoryBlock savedStateBinary;
            node->getProcessor()->getStateInformation (savedStateBinary);
            getSettings().setPluginState (list[(size_t) i], savedStateBinary.toBase64Encoding());
        }
    }
}

void AudioEngine::clearPluginStates()
{
    for (const auto& plugin : plugins.getSortedPlugins())
        getSettings().removePluginState (plugin);
}

juce::AudioProcessorGraph::Node::Ptr AudioEngine::getPluginNode (int sortedIndex)
{
    const auto sorted = plugins.getSortedPlugins();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) sorted.size()))
        return nullptr;

    if (juce::isPositiveAndBelow (sortedIndex, (int) pluginGraphNodeIds.size()))
    {
        const juce::uint32 nodeId = pluginGraphNodeIds[(size_t) sortedIndex];

        if (nodeId != 0)
            if (auto node = graph.getNodeForId (juce::AudioProcessorGraph::NodeID { nodeId }))
                return node;
    }

    const juce::PluginDescription& desc = sorted[(size_t) sortedIndex];

    for (auto& node : graph.getNodes())
        if (auto* instance = dynamic_cast<juce::AudioPluginInstance*> (node->getProcessor()))
            if (instance->getPluginDescription().isDuplicateOf (desc))
                return node;

    return nullptr;
}

juce::String AudioEngine::getPluginLoadError (int sortedIndex) const
{
    const auto sorted = plugins.getSortedPlugins();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) sorted.size()))
        return {};

    const auto it = pluginLoadErrors.find (AppSettings::getPluginId (sorted[(size_t) sortedIndex]));
    return it != pluginLoadErrors.end() ? it->second : juce::String();
}

AecMonitorSnapshot AudioEngine::getAecMonitorSnapshot() const
{
    const auto& settings = getSettings();

    AecMonitorSnapshot snap;
    snap.aecEnabled = settings.isAecEnabled();
    snap.referenceGainDb = settings.getReferenceGainDb();
    snap.aecStrengthPercent = settings.getAecStrengthPercent();

   #if JUCE_WINDOWS
    const bool useSystemLoopback = settings.useSystemLoopbackReference();
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
        const juce::String deviceId = settings.getReferenceDeviceId();

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
