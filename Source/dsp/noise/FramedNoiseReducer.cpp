#include "FramedNoiseReducer.h"
#include "audio_processing/resampler/push_sinc_resampler.h"

void SampleFifo::setCapacity (int capacity)
{
    buffer.assign ((size_t) juce::jmax (1, capacity), 0.0f);
    clear();
}

void SampleFifo::clear()
{
    readPos = writePos = count = 0;
}

void SampleFifo::push (const float* data, int numSamples)
{
    const int capacity = (int) buffer.size();
    numSamples = juce::jmin (numSamples, capacity - count); // 滿了就丟掉多的，不在音訊執行緒上擴充

    for (int i = 0; i < numSamples; ++i)
    {
        buffer[(size_t) writePos] = data != nullptr ? data[i] : 0.0f;
        writePos = (writePos + 1) % capacity;
    }

    count += numSamples;
}

void SampleFifo::pushZeros (int numSamples)
{
    push (nullptr, numSamples);
}

void SampleFifo::pop (float* dest, int numSamples)
{
    const int capacity = (int) buffer.size();
    const int available = juce::jmin (numSamples, count);

    for (int i = 0; i < available; ++i)
    {
        dest[i] = buffer[(size_t) readPos];
        readPos = (readPos + 1) % capacity;
    }

    for (int i = available; i < numSamples; ++i)
        dest[i] = 0.0f;

    count -= available;
}

FramedNoiseReducer::FramedNoiseReducer() = default;
FramedNoiseReducer::~FramedNoiseReducer() = default;

void FramedNoiseReducer::prepare (double sampleRate, int maxBlockSize)
{
    format = getFormat();
    hostRate = juce::roundToInt (sampleRate);
    maxBlockSize = juce::jmax (maxBlockSize, 64);

    resampling = hostRate != format.sampleRate;
    // 轉換取樣率以 10 ms 為一段，主機取樣率要能被 100 整除（44.1k、48k、96k 都可以）
    ready = hostRate > 0 && format.hopSize > 0 && (! resampling || hostRate % 100 == 0);

    if (! ready)
        return;

    hostChunk = resampling ? hostRate / 100 : 0;
    modelChunk = resampling ? format.sampleRate / 100 : 0;

    if (resampling)
    {
        toModel = std::make_unique<webrtc::PushSincResampler> ((size_t) hostChunk, (size_t) modelChunk);
        toHost = std::make_unique<webrtc::PushSincResampler> ((size_t) modelChunk, (size_t) hostChunk);
    }
    else
    {
        toModel.reset();
        toHost.reset();
    }

    // 輸出先墊一段靜音，之後每次都拿得到足夠的樣本：
    // 不轉取樣率時最多差一個 hop；轉換時再加上輸入、輸出各一段 10 ms
    const int hopInHost = (int) std::ceil ((double) format.hopSize * hostRate / format.sampleRate);
    primeSamples = resampling ? 2 * hostChunk + hopInHost : format.hopSize;

    const int hostCapacity = 4 * (maxBlockSize + hopInHost + 2 * hostChunk) + primeSamples;
    const int modelCapacity = 4 * (format.hopSize + 2 * modelChunk)
                              + (int) std::ceil ((double) hostCapacity * format.sampleRate / hostRate);

    hostIn.setCapacity (hostCapacity);
    hostOut.setCapacity (hostCapacity);
    modelIn.setCapacity (modelCapacity);
    modelOut.setCapacity (modelCapacity);
    dryDelay.setCapacity (format.latencySamples + format.hopSize);

    mono.assign ((size_t) maxBlockSize, 0.0f);
    hostChunkBuf.assign ((size_t) juce::jmax (1, hostChunk), 0.0f);
    modelChunkBuf.assign ((size_t) juce::jmax (1, modelChunk), 0.0f);
    hopIn.assign ((size_t) format.hopSize, 0.0f);
    hopOut.assign ((size_t) format.hopSize, 0.0f);
    hopDry.assign ((size_t) format.hopSize, 0.0f);

    reset();
}

double FramedNoiseReducer::getLatencySeconds() const
{
    if (! ready)
        return 0.0;

    // 輸出先墊的靜音（主機取樣率）＋模型本身的延遲（模型取樣率）
    return (double) primeSamples / hostRate + (double) format.latencySamples / format.sampleRate;
}

void FramedNoiseReducer::reset()
{
    if (! ready)
        return;

    hostIn.clear();
    modelIn.clear();
    modelOut.clear();
    hostOut.clear();
    dryDelay.clear();

    hostOut.pushZeros (primeSamples);
    dryDelay.pushZeros (format.latencySamples);
    appliedAttenuationDb = -1.0f;
    resetModel();
}

void FramedNoiseReducer::process (juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (! ready || numChannels == 0 || numSamples == 0)
        return;

    // 主機給的區塊比 prepare 宣告的大時才會配置（少見）
    if ((int) mono.size() < numSamples)
        mono.resize ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getReadPointer (ch)[i];

        mono[(size_t) i] = sum / (float) numChannels;
    }

    const float limitDb = maxAttenuationDb.load();
    float dryGain = 0.0f;

    if (format.hasNativeAttenuationLimit)
    {
        if (limitDb != appliedAttenuationDb)
        {
            applyNativeAttenuationLimit (limitDb);
            appliedAttenuationDb = limitDb;
        }
    }
    else if (limitDb < 100.0f)
    {
        dryGain = juce::Decibels::decibelsToGain (-limitDb);
    }

    hostIn.push (mono.data(), numSamples);
    moveHostToModel();
    runModel (dryGain);
    moveModelToHost();

    hostOut.pop (mono.data(), numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), mono.data(), numSamples);
}

void FramedNoiseReducer::moveHostToModel()
{
    if (! resampling)
    {
        while (hostIn.size() > 0)
        {
            const int n = juce::jmin (hostIn.size(), (int) mono.size());
            hostIn.pop (mono.data(), n);
            modelIn.push (mono.data(), n);
        }

        return;
    }

    while (hostIn.size() >= hostChunk)
    {
        hostIn.pop (hostChunkBuf.data(), hostChunk);
        toModel->Resample (hostChunkBuf.data(), (size_t) hostChunk, modelChunkBuf.data(), (size_t) modelChunk);
        modelIn.push (modelChunkBuf.data(), modelChunk);
    }
}

void FramedNoiseReducer::runModel (float dryGain)
{
    const int hop = format.hopSize;

    while (modelIn.size() >= hop)
    {
        modelIn.pop (hopIn.data(), hop);
        processHop (hopIn.data(), hopOut.data());

        // 原音延遲到跟模型輸出同一時間點再混回去，才不會有相位抵消
        dryDelay.push (hopIn.data(), hop);
        dryDelay.pop (hopDry.data(), hop);

        if (dryGain > 0.0f)
            for (int i = 0; i < hop; ++i)
                hopOut[(size_t) i] += dryGain * (hopDry[(size_t) i] - hopOut[(size_t) i]);

        modelOut.push (hopOut.data(), hop);
    }
}

void FramedNoiseReducer::moveModelToHost()
{
    if (! resampling)
    {
        while (modelOut.size() > 0)
        {
            const int n = juce::jmin (modelOut.size(), (int) mono.size());
            modelOut.pop (mono.data(), n);
            hostOut.push (mono.data(), n);
        }

        return;
    }

    while (modelOut.size() >= modelChunk)
    {
        modelOut.pop (modelChunkBuf.data(), modelChunk);
        toHost->Resample (modelChunkBuf.data(), (size_t) modelChunk, hostChunkBuf.data(), (size_t) hostChunk);
        hostOut.push (hostChunkBuf.data(), hostChunk);
    }
}
