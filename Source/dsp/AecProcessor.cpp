#include "AecProcessor.h"

#include "api/echo_canceller3_config.h"
#include "api/echo_canceller3_factory.h"
#include "audio_processing/audio_buffer.h"
#include "audio_processing/high_pass_filter.h"
#include "audio_processing/include/audio_processing.h"
#include "audio_processing/resampler/push_sinc_resampler.h"

namespace
{
    float computeBlockRms (const float* data, int numSamples)
    {
        if (data == nullptr || numSamples <= 0)
            return 0.0f;

        double sumSq = 0.0;
        for (int i = 0; i < numSamples; ++i)
            sumSq += static_cast<double> (data[i]) * static_cast<double> (data[i]);

        return static_cast<float> (std::sqrt (sumSq / static_cast<double> (numSamples)));
    }

    int hostFrameSizeFor (int hostRate, int processingRate, int processingFrameSize)
    {
        return juce::roundToInt (static_cast<double> (processingFrameSize)
                                 * static_cast<double> (hostRate)
                                 / static_cast<double> (processingRate));
    }
}

struct AecProcessor::Engine
{
    std::unique_ptr<webrtc::EchoControl> echoController;
    std::unique_ptr<webrtc::HighPassFilter> hpFilter;
    std::unique_ptr<webrtc::AudioBuffer> renderBuffer;
    std::unique_ptr<webrtc::AudioBuffer> captureBuffer;
    std::unique_ptr<webrtc::PushSincResampler> refResampler;
    std::unique_ptr<webrtc::PushSincResampler> capInResampler;
    std::unique_ptr<webrtc::PushSincResampler> capOutResampler;

    // 容量在建立時預留好，音訊執行緒上正常情況不會再配置記憶體
    std::vector<float> micMono;
    std::vector<float> capHostPending;
    std::vector<float> capOutPending;
    std::vector<float> refHostFrameScratch;
    std::vector<float> refResampleScratch;
    std::vector<float> capResampleScratch;
    std::vector<float> capFrameScratch;
    std::vector<float> capOutResampleScratch;
    std::vector<float*> channelPtrScratch;
};

webrtc::EchoCanceller3Config AecProcessor::makeAecConfig (float strength)
{
    webrtc::EchoCanceller3Config config;
    const float s = juce::jlimit (25.0f, 150.0f, strength) / 100.0f;

    config.ep_strength.default_gain = juce::jlimit (0.3f, 2.0f, s);

    const float inv = 1.0f / s;
    auto& lf = config.suppressor.normal_tuning.mask_lf;
    auto& hf = config.suppressor.normal_tuning.mask_hf;

    lf.enr_suppress = juce::jlimit (0.08f, 0.7f, 0.4f * inv);
    lf.enr_transparent = juce::jlimit (0.12f, 0.6f, 0.3f * inv);
    hf.enr_suppress = juce::jlimit (0.03f, 0.35f, 0.1f * inv);
    hf.enr_transparent = juce::jlimit (0.04f, 0.4f, 0.07f * inv);
    config.suppressor.high_bands_suppression.max_gain_during_echo = juce::jlimit (0.01f, 1.0f, inv);

    webrtc::EchoCanceller3Config::Validate (&config);
    return config;
}

AecProcessor::AecProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    referenceRing.resize (static_cast<size_t> (referenceRingSize), 0.0f);
}

AecProcessor::~AecProcessor() = default;

bool AecProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

int AecProcessor::pickProcessingSampleRate (int hostRate)
{
    switch (hostRate)
    {
        case 8000:
        case 16000:
        case 32000:
        case 48000:
            return hostRate;
        case 11025:
        case 22050:
            return 16000;
        case 44100:
        case 88200:
        case 96000:
            return 48000;
        default:
            break;
    }

    if (hostRate < 12000)
        return 16000;

    if (hostRate < 36000)
        return 32000;

    return 48000;
}

std::unique_ptr<AecProcessor::Engine> AecProcessor::createEngine() const
{
    auto e = std::make_unique<Engine>();

    webrtc::EchoCanceller3Config config = makeAecConfig (strengthPercent.load());
    config.filter.export_linear_aec_output = false;

    webrtc::EchoCanceller3Factory factory (config);
    e->echoController = factory.Create (processingSampleRate, numChannels, numChannels);
    e->hpFilter = std::make_unique<webrtc::HighPassFilter> (processingSampleRate, static_cast<size_t> (numChannels));

    e->renderBuffer = std::make_unique<webrtc::AudioBuffer> (
        static_cast<size_t> (processingSampleRate),
        static_cast<size_t> (numChannels),
        static_cast<size_t> (processingSampleRate),
        static_cast<size_t> (numChannels),
        static_cast<size_t> (processingSampleRate),
        static_cast<size_t> (numChannels));

    e->captureBuffer = std::make_unique<webrtc::AudioBuffer> (
        static_cast<size_t> (processingSampleRate),
        static_cast<size_t> (numChannels),
        static_cast<size_t> (processingSampleRate),
        static_cast<size_t> (numChannels),
        static_cast<size_t> (processingSampleRate),
        static_cast<size_t> (numChannels));

    e->micMono.assign (static_cast<size_t> (blockSize), 0.0f);
    e->capHostPending.reserve (static_cast<size_t> (hostFrameSize + blockSize * 2));
    e->capOutPending.reserve (static_cast<size_t> (hostFrameSize * 2 + blockSize * 2));
    e->refHostFrameScratch.assign (static_cast<size_t> (hostFrameSize), 0.0f);
    e->capFrameScratch.assign (static_cast<size_t> (frameSize), 0.0f);
    e->channelPtrScratch.assign (1, nullptr);

    if (resampling)
    {
        e->refResampleScratch.assign (static_cast<size_t> (frameSize), 0.0f);
        e->capResampleScratch.assign (static_cast<size_t> (frameSize), 0.0f);
        e->capOutResampleScratch.assign (static_cast<size_t> (hostFrameSize), 0.0f);

        e->refResampler = std::make_unique<webrtc::PushSincResampler> (
            static_cast<size_t> (hostFrameSize),
            static_cast<size_t> (frameSize));
        e->capInResampler = std::make_unique<webrtc::PushSincResampler> (
            static_cast<size_t> (hostFrameSize),
            static_cast<size_t> (frameSize));
        e->capOutResampler = std::make_unique<webrtc::PushSincResampler> (
            static_cast<size_t> (frameSize),
            static_cast<size_t> (hostFrameSize));
    }

    return e;
}

void AecProcessor::installEngine (std::unique_ptr<Engine> newEngine)
{
    {
        const std::lock_guard<std::mutex> lock (engineMutex);
        std::swap (engine, newEngine);
    }

    // newEngine 現在是舊引擎，在鎖外釋放，不拖住音訊執行緒
}

void AecProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    prepared.store (false);
    installEngine (nullptr);

    hostSampleRate = newSampleRate;
    blockSize = samplesPerBlock;
    referenceDelaySamples = juce::jmax (512, samplesPerBlock * 2);

    referenceWritePos.store (0);
    referenceReadPos.store (0);
    std::fill (referenceRing.begin(), referenceRing.end(), 0.0f);

    processingSampleRate = pickProcessingSampleRate (static_cast<int> (newSampleRate));
    const int hostRate = static_cast<int> (hostSampleRate);
    resampling = hostRate != processingSampleRate;
    frameSize = processingSampleRate / 100;
    hostFrameSize = resampling ? hostFrameSizeFor (hostRate, processingSampleRate, frameSize) : frameSize;

    installEngine (createEngine());
    prepared.store (true);

    referenceLevelDb.store (-100.0f);
    micRawLevelDb.store (-100.0f);
    micLevelDb.store (-100.0f);
    outputLevelDb.store (-100.0f);
    echoRemovedDb.store (0.0f);
    erleDb.store (0.0f);
    referenceSamplesReceived.store (0);
    samplesProcessed.store (0);
    referenceUnderruns.store (0);
}

void AecProcessor::setStrength (float strengthPercentIn)
{
    strengthPercent.store (juce::jlimit (25.0f, 150.0f, strengthPercentIn));

    if (prepared.load())
        installEngine (createEngine());
}

float AecProcessor::getStrength() const
{
    return strengthPercent.load();
}

void AecProcessor::releaseResources()
{
    prepared.store (false);
    installEngine (nullptr);
}

void AecProcessor::updateLevelDb (std::atomic<float>& target, float blockRms)
{
    const float newDb = juce::Decibels::gainToDecibels (blockRms, -100.0f);
    const float previous = target.load (std::memory_order_relaxed);
    target.store (previous * 0.85f + newDb * 0.15f, std::memory_order_relaxed);
}

void AecProcessor::updateAttenuationDb (std::atomic<float>& target, float attenuationDb)
{
    const float clamped = juce::jmax (0.0f, attenuationDb);
    updateSmoothedDb (target, clamped);
}

void AecProcessor::updateSmoothedDb (std::atomic<float>& target, float db)
{
    const float previous = target.load (std::memory_order_relaxed);
    target.store (previous * 0.85f + db * 0.15f, std::memory_order_relaxed);
}

int AecProcessor::getReferenceLeadSamples() const
{
    const int writeIndex = referenceWritePos.load (std::memory_order_acquire);
    const int readIndex  = referenceReadPos.load (std::memory_order_acquire);
    return (writeIndex - readIndex + referenceRingSize) % referenceRingSize;
}

int AecProcessor::getMaxAllowedReferenceLeadSamples() const
{
    const int slack = juce::jmax (hostFrameSize * 2, blockSize);
    return juce::jmin (referenceRingSize / 2, referenceDelaySamples + slack);
}

void AecProcessor::resyncReferenceReadPointer()
{
    const int writeIndex = referenceWritePos.load (std::memory_order_acquire);
    int readIndex = referenceReadPos.load (std::memory_order_relaxed);
    const int lead = (writeIndex - readIndex + referenceRingSize) % referenceRingSize;

    const int targetLead = referenceDelaySamples;
    const int maxAllowedLead = getMaxAllowedReferenceLeadSamples();

    if (lead < targetLead || lead > maxAllowedLead)
    {
        readIndex = (writeIndex - targetLead + referenceRingSize) % referenceRingSize;
        referenceReadPos.store (readIndex, std::memory_order_release);
    }
}

bool AecProcessor::readReferenceFrame (float* dest, int numSamples)
{
    if (dest == nullptr || numSamples <= 0)
        return false;

    resyncReferenceReadPointer();

    const int writeIndex = referenceWritePos.load (std::memory_order_acquire);
    int readIndex = referenceReadPos.load (std::memory_order_relaxed);
    const int lead = (writeIndex - readIndex + referenceRingSize) % referenceRingSize;

    if (lead <= referenceDelaySamples)
    {
        referenceUnderruns.fetch_add (1, std::memory_order_relaxed);
        std::fill (dest, dest + numSamples, 0.0f);
        return false;
    }

    lastReferenceLeadForAec = lead;

    for (int i = 0; i < numSamples; ++i)
    {
        dest[i] = referenceRing[static_cast<size_t> (readIndex)];
        readIndex = (readIndex + 1) % referenceRingSize;
    }

    referenceReadPos.store (readIndex, std::memory_order_release);
    return true;
}

void AecProcessor::pushReference (const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;

    referenceSamplesReceived.fetch_add (numSamples, std::memory_order_relaxed);

    int writeIndex = referenceWritePos.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        referenceRing[static_cast<size_t> (writeIndex)] = samples[i];
        writeIndex = (writeIndex + 1) % referenceRingSize;
    }

    referenceWritePos.store (writeIndex, std::memory_order_release);

    const int readIndex = referenceReadPos.load (std::memory_order_acquire);
    const int lead = (writeIndex - readIndex + referenceRingSize) % referenceRingSize;

    if (lead > getMaxAllowedReferenceLeadSamples())
    {
        referenceReadPos.store ((writeIndex - referenceDelaySamples + referenceRingSize) % referenceRingSize,
                                std::memory_order_release);
    }
}

void AecProcessor::feedRenderFrame (Engine& e, const float* frame)
{
    if (e.echoController == nullptr || e.renderBuffer == nullptr || frame == nullptr)
        return;

    e.channelPtrScratch[0] = const_cast<float*> (frame);
    const webrtc::StreamConfig streamConfig (processingSampleRate, static_cast<size_t> (numChannels));
    e.renderBuffer->CopyFrom (e.channelPtrScratch.data(), streamConfig);
    e.renderBuffer->SplitIntoFrequencyBands();
    e.echoController->AnalyzeRender (e.renderBuffer.get());
    e.renderBuffer->MergeFrequencyBands();
}

void AecProcessor::processCaptureFrame (Engine& e, const float* micFrame, float* outputFrame)
{
    if (e.echoController == nullptr || e.captureBuffer == nullptr || micFrame == nullptr || outputFrame == nullptr)
    {
        if (outputFrame != nullptr && micFrame != nullptr)
            std::memcpy (outputFrame, micFrame, static_cast<size_t> (frameSize) * sizeof (float));
        return;
    }

    e.channelPtrScratch[0] = const_cast<float*> (micFrame);
    const webrtc::StreamConfig streamConfig (processingSampleRate, static_cast<size_t> (numChannels));

    e.captureBuffer->CopyFrom (e.channelPtrScratch.data(), streamConfig);
    e.echoController->AnalyzeCapture (e.captureBuffer.get());
    e.captureBuffer->SplitIntoFrequencyBands();
    e.hpFilter->Process (e.captureBuffer.get(), true);

    const int delayMs = juce::roundToInt (1000.0 * static_cast<double> (lastReferenceLeadForAec)
                                          / hostSampleRate);
    e.echoController->SetAudioBufferDelay (delayMs);
    e.echoController->ProcessCapture (e.captureBuffer.get(), false);
    e.captureBuffer->MergeFrequencyBands();
    e.captureBuffer->CopyTo (streamConfig, e.channelPtrScratch.data());
    std::memcpy (outputFrame, e.channelPtrScratch[0], static_cast<size_t> (frameSize) * sizeof (float));
}

void AecProcessor::processAlignedFramePair (Engine& e, const float* refHostFrame, const float* micHostFrame)
{
    updateLevelDb (referenceLevelDb, computeBlockRms (refHostFrame, hostFrameSize));

    if (resampling)
    {
        e.refResampler->Resample (refHostFrame,
                                  static_cast<size_t> (hostFrameSize),
                                  e.refResampleScratch.data(),
                                  static_cast<size_t> (frameSize));
        e.capInResampler->Resample (micHostFrame,
                                    static_cast<size_t> (hostFrameSize),
                                    e.capResampleScratch.data(),
                                    static_cast<size_t> (frameSize));

        feedRenderFrame (e, e.refResampleScratch.data());
        processCaptureFrame (e, e.capResampleScratch.data(), e.capFrameScratch.data());

        e.capOutResampler->Resample (e.capFrameScratch.data(),
                                     static_cast<size_t> (frameSize),
                                     e.capOutResampleScratch.data(),
                                     static_cast<size_t> (hostFrameSize));
        e.capOutPending.insert (e.capOutPending.end(),
                                e.capOutResampleScratch.begin(),
                                e.capOutResampleScratch.begin() + hostFrameSize);

        const float refRms = computeBlockRms (refHostFrame, hostFrameSize);
        const float micInRms = computeBlockRms (micHostFrame, hostFrameSize);
        const float micOutRms = computeBlockRms (e.capOutResampleScratch.data(), hostFrameSize);
        updateLevelDb (micLevelDb, micInRms);
        updateLevelDb (outputLevelDb, micOutRms);

        if (e.echoController != nullptr)
            updateSmoothedDb (erleDb, static_cast<float> (e.echoController->GetMetrics().echo_return_loss_enhancement));

        if (refRms > 1.0e-4f && micInRms > 1.0e-7f)
        {
            const float attenuationDb = 20.0f * std::log10 (micInRms / juce::jmax (micOutRms, 1.0e-10f));
            updateAttenuationDb (echoRemovedDb, attenuationDb);
        }
    }
    else
    {
        feedRenderFrame (e, refHostFrame);
        processCaptureFrame (e, micHostFrame, e.capFrameScratch.data());
        e.capOutPending.insert (e.capOutPending.end(),
                                e.capFrameScratch.begin(),
                                e.capFrameScratch.begin() + frameSize);

        const float refRms = computeBlockRms (refHostFrame, frameSize);
        const float micInRms = computeBlockRms (micHostFrame, frameSize);
        const float micOutRms = computeBlockRms (e.capFrameScratch.data(), frameSize);
        updateLevelDb (micLevelDb, micInRms);
        updateLevelDb (outputLevelDb, micOutRms);

        if (e.echoController != nullptr)
            updateSmoothedDb (erleDb, static_cast<float> (e.echoController->GetMetrics().echo_return_loss_enhancement));

        if (refRms > 1.0e-4f && micInRms > 1.0e-7f)
        {
            const float attenuationDb = 20.0f * std::log10 (micInRms / juce::jmax (micOutRms, 1.0e-10f));
            updateAttenuationDb (echoRemovedDb, attenuationDb);
        }
    }
}

AecProcessorStats AecProcessor::getStats() const
{
    AecProcessorStats stats;
    stats.aecStrengthPercent = strengthPercent.load();
    const int lead = getReferenceLeadSamples();
    stats.referenceDelayMs = static_cast<float> (1000.0 * static_cast<double> (lead) / hostSampleRate);
    stats.referenceTargetDelayMs = static_cast<float> (1000.0 * static_cast<double> (referenceDelaySamples)
                                                       / hostSampleRate);
    stats.referenceLevelDb = referenceLevelDb.load (std::memory_order_relaxed);
    stats.micRawLevelDb = micRawLevelDb.load (std::memory_order_relaxed);
    stats.micLevelDb = micLevelDb.load (std::memory_order_relaxed);
    stats.outputLevelDb = outputLevelDb.load (std::memory_order_relaxed);
    stats.echoRemovedDb = echoRemovedDb.load (std::memory_order_relaxed);
    stats.erleDb = erleDb.load (std::memory_order_relaxed);
    stats.referenceSamplesReceived = referenceSamplesReceived.load (std::memory_order_relaxed);
    stats.samplesProcessed = samplesProcessed.load (std::memory_order_relaxed);
    stats.referenceUnderruns = referenceUnderruns.load (std::memory_order_relaxed);
    return stats;
}

void AecProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // UI 執行緒正在換引擎時不等鎖，這一段直接讓原音通過
    std::unique_lock<std::mutex> lock (engineMutex, std::try_to_lock);
    if (! lock.owns_lock() || engine == nullptr || engine->echoController == nullptr)
        return;

    Engine& e = *engine;

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    // 主機給的區塊比 prepareToPlay 宣告的大時才會配置（少見）
    if (static_cast<int> (e.micMono.size()) < numSamples)
        e.micMono.resize (static_cast<size_t> (numSamples));

    float* micMono = e.micMono.data();
    for (int i = 0; i < numSamples; ++i)
        micMono[i] = buffer.getNumChannels() > 1 ? 0.5f * (left[i] + right[i]) : left[i];

    e.capHostPending.insert (e.capHostPending.end(), micMono, micMono + numSamples);
    updateLevelDb (micRawLevelDb, computeBlockRms (micMono, numSamples));

    while (static_cast<int> (e.capHostPending.size()) >= hostFrameSize)
    {
        readReferenceFrame (e.refHostFrameScratch.data(), hostFrameSize);
        processAlignedFramePair (e, e.refHostFrameScratch.data(), e.capHostPending.data());

        e.capHostPending.erase (e.capHostPending.begin(),
                                e.capHostPending.begin() + hostFrameSize);
    }

    // 先用已處理好的樣本，不夠的部分補原音；最後一次移掉用過的，不再逐樣本從頭刪除
    const int available = juce::jmin (numSamples, static_cast<int> (e.capOutPending.size()));

    for (int i = 0; i < numSamples; ++i)
    {
        const float sample = i < available ? e.capOutPending[static_cast<size_t> (i)] : micMono[i];

        if (buffer.getNumChannels() > 1)
        {
            left[i] = sample;
            right[i] = sample;
        }
        else
        {
            left[i] = sample;
        }
    }

    e.capOutPending.erase (e.capOutPending.begin(), e.capOutPending.begin() + available);

    samplesProcessed.fetch_add (numSamples, std::memory_order_relaxed);
}
