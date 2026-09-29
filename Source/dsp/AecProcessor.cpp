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

AecProcessor::~AecProcessor()
{
    destroyAec3();
}

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

void AecProcessor::destroyAec3()
{
    std::lock_guard<std::mutex> lock (captureMutex);
    echoController.reset();
    hpFilter.reset();
    renderBuffer.reset();
    captureBuffer.reset();
    refResampler.reset();
    capInResampler.reset();
    capOutResampler.reset();
}

void AecProcessor::createAec3 (int processingRate)
{
    destroyAec3();

    webrtc::EchoCanceller3Config config = makeAecConfig (strengthPercent);
    config.filter.export_linear_aec_output = false;

    webrtc::EchoCanceller3Factory factory (config);
    echoController = factory.Create (processingRate, numChannels, numChannels);
    hpFilter = std::make_unique<webrtc::HighPassFilter> (processingRate, static_cast<size_t> (numChannels));

    renderBuffer = std::make_unique<webrtc::AudioBuffer> (
        static_cast<size_t> (processingRate),
        static_cast<size_t> (numChannels),
        static_cast<size_t> (processingRate),
        static_cast<size_t> (numChannels),
        static_cast<size_t> (processingRate),
        static_cast<size_t> (numChannels));

    captureBuffer = std::make_unique<webrtc::AudioBuffer> (
        static_cast<size_t> (processingRate),
        static_cast<size_t> (numChannels),
        static_cast<size_t> (processingRate),
        static_cast<size_t> (numChannels),
        static_cast<size_t> (processingRate),
        static_cast<size_t> (numChannels));

    const int hostRate = static_cast<int> (hostSampleRate);
    resampling = hostRate != processingRate;
    frameSize = processingRate / 100;
    hostFrameSize = resampling ? hostFrameSizeFor (hostRate, processingRate, frameSize) : frameSize;

    capHostPending.clear();
    capOutPending.clear();
    refHostFrameScratch.assign (static_cast<size_t> (hostFrameSize), 0.0f);
    capFrameScratch.assign (static_cast<size_t> (frameSize), 0.0f);
    channelPtrScratch.assign (1, nullptr);

    if (resampling)
    {
        refResampleScratch.assign (static_cast<size_t> (frameSize), 0.0f);
        capResampleScratch.assign (static_cast<size_t> (frameSize), 0.0f);
        capOutResampleScratch.assign (static_cast<size_t> (hostFrameSize), 0.0f);

        refResampler = std::make_unique<webrtc::PushSincResampler> (
            static_cast<size_t> (hostFrameSize),
            static_cast<size_t> (frameSize));
        capInResampler = std::make_unique<webrtc::PushSincResampler> (
            static_cast<size_t> (hostFrameSize),
            static_cast<size_t> (frameSize));
        capOutResampler = std::make_unique<webrtc::PushSincResampler> (
            static_cast<size_t> (frameSize),
            static_cast<size_t> (hostFrameSize));
    }
}

void AecProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    hostSampleRate = newSampleRate;
    blockSize = samplesPerBlock;
    referenceDelaySamples = juce::jmax (512, samplesPerBlock * 2);

    referenceWritePos.store (0);
    referenceReadPos.store (0);
    std::fill (referenceRing.begin(), referenceRing.end(), 0.0f);

    processingSampleRate = pickProcessingSampleRate (static_cast<int> (newSampleRate));
    createAec3 (processingSampleRate);

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
    strengthPercent = juce::jlimit (25.0f, 150.0f, strengthPercentIn);

    if (processingSampleRate > 0 && hostSampleRate > 0.0)
        createAec3 (processingSampleRate);
}

float AecProcessor::getStrength() const
{
    return strengthPercent;
}

void AecProcessor::releaseResources()
{
    destroyAec3();
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

void AecProcessor::feedRenderFrame (const float* frame, int processingFrameSize)
{
    if (echoController == nullptr || renderBuffer == nullptr || frame == nullptr)
        return;

    channelPtrScratch[0] = const_cast<float*> (frame);
    const webrtc::StreamConfig streamConfig (processingSampleRate, static_cast<size_t> (numChannels));
    renderBuffer->CopyFrom (channelPtrScratch.data(), streamConfig);
    renderBuffer->SplitIntoFrequencyBands();
    echoController->AnalyzeRender (renderBuffer.get());
    renderBuffer->MergeFrequencyBands();
}

void AecProcessor::processCaptureFrame (const float* micFrame, float* outputFrame, int processingFrameSize)
{
    if (echoController == nullptr || captureBuffer == nullptr || micFrame == nullptr || outputFrame == nullptr)
    {
        if (outputFrame != nullptr && micFrame != nullptr)
            std::memcpy (outputFrame, micFrame, static_cast<size_t> (processingFrameSize) * sizeof (float));
        return;
    }

    channelPtrScratch[0] = const_cast<float*> (micFrame);
    const webrtc::StreamConfig streamConfig (processingSampleRate, static_cast<size_t> (numChannels));

    captureBuffer->CopyFrom (channelPtrScratch.data(), streamConfig);
    echoController->AnalyzeCapture (captureBuffer.get());
    captureBuffer->SplitIntoFrequencyBands();
    hpFilter->Process (captureBuffer.get(), true);

    const int delayMs = juce::roundToInt (1000.0 * static_cast<double> (lastReferenceLeadForAec)
                                          / hostSampleRate);
    echoController->SetAudioBufferDelay (delayMs);
    echoController->ProcessCapture (captureBuffer.get(), false);
    captureBuffer->MergeFrequencyBands();
    captureBuffer->CopyTo (streamConfig, channelPtrScratch.data());
    std::memcpy (outputFrame, channelPtrScratch[0], static_cast<size_t> (processingFrameSize) * sizeof (float));
}

void AecProcessor::processAlignedFramePair (const float* refHostFrame,
                                            const float* micHostFrame,
                                            int frameHostSize)
{
    updateLevelDb (referenceLevelDb, computeBlockRms (refHostFrame, frameHostSize));

    std::lock_guard<std::mutex> lock (captureMutex);

    if (resampling)
    {
        refResampler->Resample (refHostFrame,
                                static_cast<size_t> (frameHostSize),
                                refResampleScratch.data(),
                                static_cast<size_t> (frameSize));
        capInResampler->Resample (micHostFrame,
                                  static_cast<size_t> (frameHostSize),
                                  capResampleScratch.data(),
                                  static_cast<size_t> (frameSize));

        feedRenderFrame (refResampleScratch.data(), frameSize);
        processCaptureFrame (capResampleScratch.data(), capFrameScratch.data(), frameSize);

        capOutResampler->Resample (capFrameScratch.data(),
                                   static_cast<size_t> (frameSize),
                                   capOutResampleScratch.data(),
                                   static_cast<size_t> (frameHostSize));
        capOutPending.insert (capOutPending.end(),
                              capOutResampleScratch.begin(),
                              capOutResampleScratch.begin() + frameHostSize);

        const float refRms = computeBlockRms (refHostFrame, frameHostSize);
        const float micInRms = computeBlockRms (micHostFrame, frameHostSize);
        const float micOutRms = computeBlockRms (capOutResampleScratch.data(), frameHostSize);
        updateLevelDb (micLevelDb, micInRms);
        updateLevelDb (outputLevelDb, micOutRms);

        if (echoController != nullptr)
            updateSmoothedDb (erleDb, static_cast<float> (echoController->GetMetrics().echo_return_loss_enhancement));

        if (refRms > 1.0e-4f && micInRms > 1.0e-7f)
        {
            const float attenuationDb = 20.0f * std::log10 (micInRms / juce::jmax (micOutRms, 1.0e-10f));
            updateAttenuationDb (echoRemovedDb, attenuationDb);
        }
    }
    else
    {
        feedRenderFrame (refHostFrame, frameSize);
        processCaptureFrame (micHostFrame, capFrameScratch.data(), frameSize);
        capOutPending.insert (capOutPending.end(),
                              capFrameScratch.begin(),
                              capFrameScratch.begin() + frameSize);

        const float refRms = computeBlockRms (refHostFrame, frameSize);
        const float micInRms = computeBlockRms (micHostFrame, frameSize);
        const float micOutRms = computeBlockRms (capFrameScratch.data(), frameSize);
        updateLevelDb (micLevelDb, micInRms);
        updateLevelDb (outputLevelDb, micOutRms);

        if (echoController != nullptr)
            updateSmoothedDb (erleDb, static_cast<float> (echoController->GetMetrics().echo_return_loss_enhancement));

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
    stats.aecStrengthPercent = strengthPercent;
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
    if (numSamples <= 0 || echoController == nullptr)
        return;

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    std::vector<float> micMono (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        micMono[static_cast<size_t> (i)] = buffer.getNumChannels() > 1 ? 0.5f * (left[i] + right[i]) : left[i];

    capHostPending.insert (capHostPending.end(), micMono.begin(), micMono.end());
    updateLevelDb (micRawLevelDb, computeBlockRms (micMono.data(), numSamples));

    while (static_cast<int> (capHostPending.size()) >= hostFrameSize)
    {
        readReferenceFrame (refHostFrameScratch.data(), hostFrameSize);

        processAlignedFramePair (refHostFrameScratch.data(),
                                 capHostPending.data(),
                                 hostFrameSize);

        capHostPending.erase (capHostPending.begin(),
                              capHostPending.begin() + hostFrameSize);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = micMono[static_cast<size_t> (i)];
        if (! capOutPending.empty())
        {
            sample = capOutPending.front();
            capOutPending.erase (capOutPending.begin());
        }

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

    samplesProcessed.fetch_add (numSamples, std::memory_order_relaxed);
}
