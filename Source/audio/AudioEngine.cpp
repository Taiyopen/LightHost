#include "AudioEngine.h"
#include <algorithm>
#include <optional>
#include "LoopbackDevices.h"
#include "ExternalDeviceNodes.h"
#include "../AppSettings.h"
#include "../PluginWindow.h"
#include "../plugins/PluginChain.h"
#include "../dsp/AecProcessor.h"
#include "../dsp/GraphNodeIds.h"
#include "../dsp/DryWetMixer.h"
#include "../dsp/NoiseReducerProcessor.h"
#include "../dsp/SimpleNoiseReducer.h"
#include "../dsp/noise/NoiseReducerFactory.h"
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

    // 主裝置只能是 ASIO：先註冊 ASIO，裝置管理員就不會再加入其他類型
   #if JUCE_ASIO
    deviceManager.addAudioDeviceType (std::unique_ptr<juce::AudioIODeviceType> (juce::AudioIODeviceType::createAudioIODeviceType_ASIO()));
   #endif

    for (const auto& c : getSettings().getExternalDevices())
        externalDevices.push_back (createExternalDevice (c.uid, c.endpointId, c.name, c.isInput));

    if (auto savedAudioState = getSettings().getAudioDeviceState())
        deviceManager.initialise (256, 256, savedAudioState.get(), true);
    else
        deviceManager.initialise (256, 256, nullptr, true);

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&player);

    rebuildGraph();
    startTimer (3000);
}

std::unique_ptr<ExternalDevice> AudioEngine::createExternalDevice (int uid, const juce::String& endpointId,
                                                                  const juce::String& name, bool isInput)
{
    const auto& settings = getSettings();
    return std::make_unique<ExternalDevice> (uid, endpointId, name, isInput,
                                             settings.isExternalLowLatency(), settings.getExternalSafetyPeriods());
}

void AudioEngine::setExternalSafetyPeriods (double periods)
{
    getSettings().setExternalSafetyPeriods (periods);

    for (auto& ext : externalDevices)
        ext->setSafetyPeriods (periods);

    rebuildGraph();   // 輸入裝置的目標緩衝在處理圖 prepare 時設定
}

void AudioEngine::setExternalLowLatency (bool enabled)
{
    getSettings().setExternalLowLatency (enabled);

    // 要重新開啟裝置才會換模式；舊的等處理圖不再用它才釋放
    std::vector<std::unique_ptr<ExternalDevice>> old;

    for (auto& ext : externalDevices)
    {
        auto fresh = createExternalDevice (ext->getUid(), ext->getEndpointId(), ext->getName(), ext->isInput());
        old.push_back (std::move (ext));
        ext = std::move (fresh);
    }

    rebuildGraph();
}

void AudioEngine::saveExternalDevices()
{
    std::vector<ExternalDeviceConfig> configs;

    for (auto& ext : externalDevices)
        configs.push_back ({ ext->getUid(), ext->getEndpointId(), ext->getName(), ext->isInput() });

    getSettings().setExternalDevices (configs);
}

void AudioEngine::addExternalDevice (const juce::String& endpointId, const juce::String& name, bool isInput)
{
    int uid = 1;

    for (auto& ext : externalDevices)
        uid = juce::jmax (uid, ext->getUid() + 1);

    // uid 以後不回收：路由裡的聲道組編號靠它識別裝置
    for (const auto& c : getSettings().getExternalDevices())
        uid = juce::jmax (uid, c.uid + 1);

    externalDevices.push_back (createExternalDevice (uid, endpointId, name, isInput));
    saveExternalDevices();
    rebuildGraph();
}

void AudioEngine::removeExternalDevice (int uid)
{
    for (auto it = externalDevices.begin(); it != externalDevices.end(); ++it)
    {
        if ((*it)->getUid() != uid)
            continue;

        // 先把它從處理圖拿掉（節點握著它的參照），再關掉裝置
        auto removed = std::move (*it);
        externalDevices.erase (it);
        saveExternalDevices();
        rebuildGraph();
        return;
    }
}

void AudioEngine::timerCallback()
{
    // 出錯的附加裝置（例如被拔掉）每 3 秒重開一次；開起來了才重建處理圖
    std::vector<std::unique_ptr<ExternalDevice>> replaced;

    for (auto& ext : externalDevices)
    {
        if (ext->getState() != ExternalDevice::State::error)
            continue;

        auto fresh = createExternalDevice (ext->getUid(), ext->getEndpointId(), ext->getName(), ext->isInput());

        if (fresh->getState() == ExternalDevice::State::running)
        {
            replaced.push_back (std::move (ext));
            ext = std::move (fresh);
        }
    }

    if (! replaced.empty())
        rebuildGraph();   // 舊裝置要等處理圖不再用它（重建完）才釋放
}

AudioEngine::~AudioEngine()
{
    stopTimer();
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
void AudioEngine::movePluginUp (int sortedIndex)                     { changeChain ([&] { plugins.moveUp (sortedIndex); }); }
void AudioEngine::movePluginDown (int sortedIndex)                   { changeChain ([&] { plugins.moveDown (sortedIndex); }); }

void AudioEngine::togglePluginBypass (int sortedIndex)
{
    // 不重建處理圖：直接切節點的 bypass 旗標，瞬間生效、不會斷音
    plugins.toggleBypass (sortedIndex);
    const auto sorted = plugins.getSortedPlugins();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) sorted.size())
        || ! juce::isPositiveAndBelow (sortedIndex, (int) pluginGraphNodeIds.size()))
        return;

    const bool bypass = plugins.isBypassed (sorted[(size_t) sortedIndex]);
    const juce::uint32 nodeIndex = pluginGraphNodeIds[(size_t) sortedIndex];

    if (nodeIndex == 0)
        return;

    if (auto* node = graph.getNodeForId (juce::AudioProcessorGraph::NodeID { nodeIndex }))
        node->setBypassed (bypass);

    if (auto* mixer = getPluginMixer (sortedIndex))
        mixer->setPluginBypassed (bypass);
}

DryWetMixer* AudioEngine::getPluginMixer (int sortedIndex) const
{
    if (! juce::isPositiveAndBelow (sortedIndex, (int) pluginGraphNodeIds.size()))
        return nullptr;

    const juce::uint32 nodeIndex = pluginGraphNodeIds[(size_t) sortedIndex];

    if (nodeIndex == 0)
        return nullptr;

    if (auto* node = graph.getNodeForId (GraphNodeIds::pluginMixerId (nodeIndex)))
        return dynamic_cast<DryWetMixer*> (node->getProcessor());

    return nullptr;
}

float AudioEngine::getPluginMix (int sortedIndex) const
{
    const auto sorted = plugins.getSortedPlugins();
    return juce::isPositiveAndBelow (sortedIndex, (int) sorted.size())
               ? getSettings().getPluginMix (sorted[(size_t) sortedIndex])
               : 100.0f;
}

void AudioEngine::setPluginMix (int sortedIndex, float percent)
{
    const auto sorted = plugins.getSortedPlugins();

    if (! juce::isPositiveAndBelow (sortedIndex, (int) sorted.size()))
        return;

    getSettings().setPluginMix (sorted[(size_t) sortedIndex], percent);

    if (auto* mixer = getPluginMixer (sortedIndex))
        mixer->setMixPercent (percent);
}

void AudioEngine::saveAudioDeviceStateAndRebuild()
{
    if (auto audioState = deviceManager.createStateXml())
        getSettings().setAudioDeviceState (*audioState);

    rebuildGraph();
}

NoiseReducerProcessor* AudioEngine::getNoiseReducer() const
{
    if (auto node = graph.getNodeForId (GraphNodeIds::nrId()))
        return dynamic_cast<NoiseReducerProcessor*> (node->getProcessor());

    return nullptr;
}

void AudioEngine::setNoiseReductionEnabled (bool enabled)
{
    getSettings().setNrEnabled (enabled);

    if (auto* nr = getNoiseReducer())
        nr->setEnabled (enabled);
}

void AudioEngine::setNoiseReductionMaxAttenuation (float db)
{
    getSettings().setNrMaxAttenuationDb (db);

    if (auto* nr = getNoiseReducer())
        nr->setMaxAttenuationDb (db);
}

void AudioEngine::setNoiseReducerAlgorithm (const juce::String& id)
{
    getSettings().setNrAlgorithm (id);
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

    const Routing routing = getEffectiveRouting();
    juce::BigInteger activeIn, activeOut;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        activeIn = device->getActiveInputChannels();
        activeOut = device->getActiveOutputChannels();
    }

    // 附加裝置（WASAPI）各一個節點；開不起來的裝置不放，相關路由自然略過
    for (auto& ext : externalDevices)
    {
        if (ext->getState() != ExternalDevice::State::running)
            continue;

        if (ext->isInput())
            graph.addNode (std::make_unique<ExternalInputProcessor> (*ext), externalInputId (ext->getUid()), noRebuild);
        else
            graph.addNode (std::make_unique<ExternalOutputProcessor> (*ext), externalOutputId (ext->getUid()), noRebuild);
    }

    // 聲道組編號 → 處理圖上的節點與聲道。小於 externalPairBase 是主裝置；否則是 uid × externalPairBase + 組號
    auto resolvePair = [&] (int key, bool isInput) -> std::optional<Pin>
    {
        if (key < externalPairBase)
        {
            const auto ch = pairChannels (isInput ? activeIn : activeOut, key);

            if (! ch.isValid())
                return std::nullopt;

            return Pin { isInput ? inputId() : outputId(), ch.left, ch.right };
        }

        const int uid = key / externalPairBase;
        const int left = (key % externalPairBase) * 2;

        for (auto& ext : externalDevices)
        {
            const auto nodeId = isInput ? externalInputId (uid) : externalOutputId (uid);

            if (ext->getUid() == uid && ext->isInput() == isInput && graph.getNodeForId (nodeId) != nullptr
                && left < ext->getNumChannels())
                return Pin { nodeId, left, left + 1 < ext->getNumChannels() ? left + 1 : left };
        }

        return std::nullopt;
    };

    // 處理鏈目前的尾端。一開始是所有勾選的輸入組（接到同一個節點時處理圖會自動相加）；
    // 接上第一個節點後就只剩那個節點。沒有任何輸入時為空，後面的節點只會收到靜音
    std::vector<Pin> chainEnds;

    for (int pair : routing.chainInputPairs)
        if (const auto in = resolvePair (pair, true))
            chainEnds.push_back (*in);

    auto appendToChain = [&] (AudioProcessorGraph::NodeID nodeId)
    {
        const Pin nodePin { nodeId, channelLeft, channelRight };

        for (const auto& end : chainEnds)
            connect (end, nodePin);

        chainEnds = { nodePin };
    };

    AecProcessor* aecPtr = nullptr;

    if (settings.isAecEnabled())
    {
        auto aecNode = graph.addNode (std::make_unique<AecProcessor>(), aecId(), noRebuild);
        aecPtr = dynamic_cast<AecProcessor*> (aecNode->getProcessor());

        if (aecPtr != nullptr)
            aecPtr->setStrength (settings.getAecStrengthPercent());

        appendToChain (aecId());
    }

    {
        // 一律放進處理鏈，開關用 setEnabled 瞬間切換；模型載入失敗時退回簡易降噪
        noiseReducerError.clear();
        auto algorithm = createNoiseReducer (settings.getNrAlgorithm(), noiseReducerError);

        if (algorithm == nullptr)
            algorithm = std::make_unique<SimpleNoiseReducer>();

        auto nrNode = graph.addNode (std::make_unique<NoiseReducerProcessor> (std::move (algorithm)), nrId(), noRebuild);

        if (auto* nr = dynamic_cast<NoiseReducerProcessor*> (nrNode->getProcessor()))
        {
            nr->setEnabled (settings.isNrEnabled());
            nr->setMaxAttenuationDb (settings.getNrMaxAttenuationDb());
        }

        appendToChain (nrId());
    }

    const std::vector<juce::PluginDescription> sortedPlugins = plugins.getSortedPlugins();
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

        // 外掛後面接乾濕比混音：外掛之前的訊號同時送進混音當乾聲。
        // 略過用節點的 bypass 旗標，切換時不用重建處理圖
        const Pin pluginPin { AudioProcessorGraph::NodeID { nodeIndex }, channelLeft, channelRight };
        const auto mixerId = pluginMixerId (nodeIndex);
        auto mixerNode = graph.addNode (std::make_unique<DryWetMixer>(), mixerId, noRebuild);
        const bool bypass = plugins.isBypassed (plugin);

        if (addedNode != nullptr)
            addedNode->setBypassed (bypass);

        if (auto* mixer = dynamic_cast<DryWetMixer*> (mixerNode->getProcessor()))
        {
            mixer->setMixPercent (settings.getPluginMix (plugin));
            mixer->setPluginBypassed (bypass);
        }

        for (const auto& end : chainEnds)
        {
            connect (end, pluginPin);
            connect (end, Pin { mixerId, 2, 3 });
        }

        connect (pluginPin, Pin { mixerId, 0, 1 });
        chainEnds = { Pin { mixerId, channelLeft, channelRight } };
    }

    if (settings.isAecEnabled() && aecPtr != nullptr)
    {
        auto tapNode = graph.addNode (std::make_unique<OutputReferenceTap> (aecPtr), refTapId(), noRebuild);

        if (auto* tap = dynamic_cast<OutputReferenceTap*> (tapNode->getProcessor()))
        {
            tap->setReferenceGain (juce::Decibels::decibelsToGain (settings.getReferenceGainDb()));
            tap->setFeedsReference (! settings.useSystemLoopbackReference());
        }

        appendToChain (refTapId());
    }

    for (const auto& [source, outputPair] : routing.routes)
    {
        const auto outPin = resolvePair (outputPair, false);

        if (! outPin)
            continue;

        if (source == Routing::processedChain)
        {
            for (const auto& end : chainEnds)
                connect (end, *outPin);
        }
        else if (const auto inPin = resolvePair (source, true))
        {
            connect (*inPin, *outPin);
        }
    }

    graph.removeIllegalConnections (noRebuild);

    graph.rebuild();
    updateLoopbackCapture (aecPtr);
    deviceManager.addAudioCallback (&player);
}

void AudioEngine::connect (const Pin& from, const Pin& to)
{
    constexpr auto noRebuild = juce::AudioProcessorGraph::UpdateKind::none;

    graph.addConnection ({ { from.node, from.left }, { to.node, to.left } }, noRebuild);

    // 輸出是單聲道時只接左聲道，避免左右相加變兩倍大聲
    if (to.right != to.left)
        graph.addConnection ({ { from.node, from.right }, { to.node, to.right } }, noRebuild);
}

AudioEngine::PairChannels AudioEngine::pairChannels (const juce::BigInteger& active, int pair)
{
    // 處理圖的輸入／輸出節點只有「有勾選」的聲道，依裝置順序排；換算成節點上的聲道編號
    auto graphChannel = [&active] (int deviceChannel)
    {
        if (deviceChannel < 0 || ! active[deviceChannel])
            return -1;

        int index = 0;

        for (int i = 0; i < deviceChannel; ++i)
            if (active[i])
                ++index;

        return index;
    };

    PairChannels channels { graphChannel (pair * 2), graphChannel (pair * 2 + 1) };

    // 只勾了其中一聲道時當單聲道用
    if (channels.left < 0)
        channels.left = channels.right;
    if (channels.right < 0)
        channels.right = channels.left;

    return channels;
}

juce::String AudioEngine::sharedPairName (const juce::String& left, const juce::String& right)
{
    // 例：「Main Output 1/2 1」+「Main Output 1/2 2」→「Main Output 1/2」
    int length = 0;

    while (length < left.length() && length < right.length() && left[length] == right[length])
        ++length;

    // 只在空格或符號處切，避免「Channel 10」+「Channel 11」變成「Channel 1」
    while (length > 0 && juce::CharacterFunctions::isLetterOrDigit (left[length - 1]))
        --length;

    return left.substring (0, length).trimCharactersAtStart (" |-_").trimCharactersAtEnd (" |-_");
}

std::vector<AudioEngine::ChannelPair> AudioEngine::getActivePairs (bool inputs) const
{
    std::vector<ChannelPair> pairs;
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return pairs;

    const auto active = inputs ? device->getActiveInputChannels() : device->getActiveOutputChannels();
    const auto names = inputs ? device->getInputChannelNames() : device->getOutputChannelNames();

    for (int pair = 0; pair * 2 <= active.getHighestBit(); ++pair)
    {
        juce::StringArray pairNames;

        for (int channel : { pair * 2, pair * 2 + 1 })
            if (active[channel])
                pairNames.add (names[channel].isNotEmpty() ? names[channel] : juce::String (channel + 1));

        if (pairNames.isEmpty())
            continue;

        juce::String name = pairNames.size() == 2 ? sharedPairName (pairNames[0], pairNames[1]) : juce::String();

        if (name.isEmpty())
            name = pairNames.joinIntoString (" + ");

        pairs.push_back ({ pair, name, pairNames.joinIntoString (" + ") });
    }

    // 附加裝置：所有聲道都算有效；名稱前面加裝置名
    for (auto& ext : externalDevices)
    {
        if (ext->isInput() != inputs || ext->getState() != ExternalDevice::State::running)
            continue;

        const int n = ext->getNumChannels();

        for (int pair = 0; pair * 2 < n; ++pair)
        {
            const auto channels = pair * 2 + 1 < n ? juce::String (pair * 2 + 1) + "+" + juce::String (pair * 2 + 2)
                                                   : juce::String (pair * 2 + 1);
            const auto name = n > 2 ? ext->getName() + " " + channels : ext->getName();
            pairs.push_back ({ ext->getUid() * externalPairBase + pair, name, name });
        }
    }

    // 簡化後同名的（例如「Input 1/2」與「Input 3/4」都變成「Input」）改回完整名稱
    juce::StringArray shortNames;

    for (const auto& pair : pairs)
        shortNames.add (pair.name);

    for (auto& pair : pairs)
    {
        const auto sameName = std::count (shortNames.begin(), shortNames.end(), pair.name);

        if (sameName > 1)
            pair.name = pair.fullName;
    }

    return pairs;
}

Routing AudioEngine::getEffectiveRouting() const
{
    if (auto saved = getSettings().getRouting())
        return *saved;

    const auto inputs = getActiveInputPairs();
    const auto outputs = getActiveOutputPairs();
    return Routing::makeDefault (inputs.empty() ? -1 : inputs.front().index,
                                 outputs.empty() ? -1 : outputs.front().index);
}

void AudioEngine::setRouting (const Routing& routing)
{
    getSettings().setRouting (routing);
    rebuildGraph();
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

AudioEngine::LatencyReport AudioEngine::getLatencyReport() const
{
    LatencyReport report;
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return report;

    report.hasDevice = true;
    report.sampleRate = device->getCurrentSampleRate();
    report.bufferSize = device->getCurrentBufferSizeSamples();

    const double msPerSample = report.sampleRate > 0.0 ? 1000.0 / report.sampleRate : 0.0;
    report.inputMs = device->getInputLatencyInSamples() * msPerSample;
    report.outputMs = device->getOutputLatencyInSamples() * msPerSample;

    if (auto* aec = getAecProcessor())
        report.aecMs = aec->getLatencySamples() * msPerSample;

    if (auto* nr = getNoiseReducer())
        if (nr->isEnabled())
            report.noiseMs = nr->getLatencySamples() * msPerSample;

    for (auto nodeIndex : pluginGraphNodeIds)
        if (nodeIndex != 0)
            if (auto* node = graph.getNodeForId (juce::AudioProcessorGraph::NodeID { nodeIndex }))
                report.pluginsMs += node->getProcessor()->getLatencySamples() * msPerSample;

    // 附加輸出：處理鏈之後另外多出的延遲（取代音效卡輸出那一段）
    for (auto& ext : externalDevices)
        if (! ext->isInput() && ext->getState() == ExternalDevice::State::running)
            report.externalOutputs.push_back ({ ext->getName(), 1000.0 * ext->getAddedLatencySeconds() });

    return report;
}

juce::String AudioEngine::LatencyReport::describe() const
{
    if (! hasDevice)
        return juce::String::fromUTF8 ("沒有開啟音訊裝置");

    auto ms = [] (double v) { return juce::String (juce::roundToInt (v)); };

    return juce::String::fromUTF8 ("總計 ") + ms (totalMs()) + " ms"
         + juce::String::fromUTF8 ("（輸入 ") + ms (inputMs)
         + juce::String::fromUTF8 (" + 回音消除 ") + ms (aecMs)
         + juce::String::fromUTF8 (" + 降噪 ") + ms (noiseMs)
         + juce::String::fromUTF8 (" + 外掛 ") + ms (pluginsMs)
         + juce::String::fromUTF8 (" + 輸出 ") + ms (outputMs) + juce::String::fromUTF8 ("）") + describeExternalOutputs (true);
}

juce::String AudioEngine::LatencyReport::describeExternalOutputs (bool chinese) const
{
    if (externalOutputs.empty())
        return {};

    // 送到附加輸出時，走的是「處理鏈 → 時脈橋 → 該裝置」，不經過主裝置的輸出
    const double chainMs = inputMs + aecMs + noiseMs + pluginsMs;
    juce::StringArray items;

    for (const auto& [name, addedMs] : externalOutputs)
        items.add (name + (chinese ? juce::String::fromUTF8 (" 總計 ") : juce::String (" total "))
                   + juce::String (juce::roundToInt (chainMs + addedMs)) + " ms");

    return (chinese ? juce::String::fromUTF8 ("；附加輸出：") : juce::String ("; extra outputs: ")) + items.joinIntoString (", ");
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
        snap.referenceDriftPpm = stats.referenceDriftPpm;
    }

    return snap;
}
