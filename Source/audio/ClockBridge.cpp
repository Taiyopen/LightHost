#include "ClockBridge.h"

namespace
{
    // 控制參數以「秒」為單位，跟區塊大小、呼叫頻率無關（與 AecProcessor 的時脈追蹤同一組數值換算而來）
    constexpr double smoothingSeconds = 1.0;     // 緩衝量平滑的時間常數
    constexpr double proportionalGain = 0.096;   // 每偏離 1 秒 → 修正 9.6%（偏 10 ms ≈ 1000 ppm）
    constexpr double integralGain = 0.0096;      // 把長期固定的時脈差吃掉
    constexpr double maxCorrection = 0.002;      // ±2000 ppm
}

void ClockBridge::prepare (int numChannelsIn, int capacityFrames)
{
    numChannels = juce::jmax (1, numChannelsIn);
    capacity = juce::jmax (1024, capacityFrames);
    ring.assign ((size_t) capacity * (size_t) numChannels, 0.0f);
    writeFrame.store (0);
    readFrame.store (0);
    position = 0.0;
    starved = true;
    integral = 0.0;
    interleavedScratch.clear();
}

void ClockBridge::write (const float* const* channels, int numFrames)
{
    const auto w = writeFrame.load (std::memory_order_relaxed);
    const auto free = (juce::int64) capacity - (w - readFrame.load (std::memory_order_acquire));
    numFrames = (int) juce::jlimit ((juce::int64) 0, (juce::int64) numFrames, free);

    for (int i = 0; i < numFrames; ++i)
    {
        float* frame = ring.data() + (size_t) ((w + i) % capacity) * (size_t) numChannels;

        for (int ch = 0; ch < numChannels; ++ch)
            frame[ch] = channels[ch] != nullptr ? channels[ch][i] : 0.0f;
    }

    writeFrame.store (w + numFrames, std::memory_order_release);
}

void ClockBridge::writeInterleaved (const float* interleaved, int numFrames)
{
    const auto w = writeFrame.load (std::memory_order_relaxed);
    const auto free = (juce::int64) capacity - (w - readFrame.load (std::memory_order_acquire));
    numFrames = (int) juce::jlimit ((juce::int64) 0, (juce::int64) numFrames, free);

    for (int i = 0; i < numFrames; ++i)
    {
        float* frame = ring.data() + (size_t) ((w + i) % capacity) * (size_t) numChannels;

        for (int ch = 0; ch < numChannels; ++ch)
            frame[ch] = interleaved != nullptr ? interleaved[i * numChannels + ch] : 0.0f;
    }

    writeFrame.store (w + numFrames, std::memory_order_release);
}

void ClockBridge::writeSilence (int numFrames)
{
    writeInterleaved (nullptr, numFrames);
}

void ClockBridge::configureReader (double sourceRateIn, double readerRateIn, int target)
{
    sourceRate = sourceRateIn > 0.0 ? sourceRateIn : 48000.0;
    readerRate = readerRateIn > 0.0 ? readerRateIn : 48000.0;
    ratio = sourceRate / readerRate;
    targetFrames = juce::jmax (8, target);
    starved = true;
    integral = 0.0;
    driftPpm.store (0.0);
}

int ClockBridge::getBufferedFrames() const
{
    return (int) (writeFrame.load (std::memory_order_acquire) - readFrame.load (std::memory_order_acquire));
}

float ClockBridge::sampleAt (int channel, juce::int64 frame) const
{
    return ring[(size_t) (((frame % capacity) + capacity) % capacity) * (size_t) numChannels + (size_t) channel];
}

bool ClockBridge::prepareRead (int numFrames, double& step)
{
    const auto w = writeFrame.load (std::memory_order_acquire);
    double lead = (double) w - position;
    const double needed = numFrames * ratio * (1.0 + maxCorrection) + 4.0;

    // 讀取端落後太多（例如裝置剛重新開始）或資料不夠讀：停下來等，讀取位置不往回拉
    if (! starved && (lead < needed || lead > capacity - needed))
    {
        resyncs.fetch_add (1, std::memory_order_relaxed);
        starved = true;
    }

    if (starved)
    {
        const double resumeAt = juce::jmax ((double) targetFrames, needed + 4.0);

        if (lead < resumeAt)
            return false;

        // 資料夠了：直接跳到剛好差目標緩衝量的位置，暫時的大差距不會把積分帶歪
        position = (double) w - resumeAt;
        lead = resumeAt;
        smoothedLead = resumeAt;
        starved = false;
    }

    const double dt = numFrames / readerRate;   // 這次讀取涵蓋幾秒

    smoothedLead += juce::jmin (1.0, dt / smoothingSeconds) * (lead - smoothedLead);
    const double errorFrames = smoothedLead - targetFrames;
    const double errorSeconds = errorFrames / sourceRate;

    if (std::abs (errorFrames) < targetFrames / 2.0)
        integral = juce::jlimit (-maxCorrection, maxCorrection, integral + errorSeconds * integralGain * dt);

    const double correction = juce::jlimit (-maxCorrection, maxCorrection, errorSeconds * proportionalGain + integral);
    driftPpm.store (correction * 1.0e6, std::memory_order_relaxed);
    step = ratio * (1.0 + correction);
    return true;
}

bool ClockBridge::read (float* const* dest, int numDestChannels, int numFrames)
{
    double step = 1.0;

    if (! prepareRead (numFrames, step))
    {
        for (int ch = 0; ch < numDestChannels; ++ch)
            juce::FloatVectorOperations::clear (dest[ch], numFrames);
        return false;
    }

    double pos = position;

    for (int i = 0; i < numFrames; ++i)
    {
        const auto base = (juce::int64) std::floor (pos);
        const float t = (float) (pos - (double) base);

        for (int ch = 0; ch < numDestChannels; ++ch)
        {
            if (ch >= numChannels)
            {
                dest[ch][i] = 0.0f;
                continue;
            }

            const float y0 = sampleAt (ch, base - 1), y1 = sampleAt (ch, base), y2 = sampleAt (ch, base + 1), y3 = sampleAt (ch, base + 2);
            dest[ch][i] = y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
        }

        pos += step;
    }

    position = pos;
    readFrame.store ((juce::int64) std::floor (position) - 2, std::memory_order_release);
    return true;
}

bool ClockBridge::readInterleaved (float* dest, int numFrames)
{
    double step = 1.0;

    if (! prepareRead (numFrames, step))
    {
        std::fill (dest, dest + numFrames * numChannels, 0.0f);
        return false;
    }

    double pos = position;

    for (int i = 0; i < numFrames; ++i)
    {
        const auto base = (juce::int64) std::floor (pos);
        const float t = (float) (pos - (double) base);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float y0 = sampleAt (ch, base - 1), y1 = sampleAt (ch, base), y2 = sampleAt (ch, base + 1), y3 = sampleAt (ch, base + 2);
            dest[i * numChannels + ch] = y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
        }

        pos += step;
    }

    position = pos;
    readFrame.store ((juce::int64) std::floor (position) - 2, std::memory_order_release);
    return true;
}
