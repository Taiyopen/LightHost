// AEC 參考訊號時脈追蹤的離線檢查：模擬喇叭與麥克風時脈差 0、±300、±1000 ppm，
// 跑 2 分鐘後確認沒有再硬性重新對齊，且量到的漂移與模擬值相符。
// 用法：AecDriftTest.exe

#include <JuceHeader.h>
#include "dsp/AecProcessor.h"
#include <cstdio>

namespace
{
    bool runCase (double driftPpm)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;
        const int numBlocks = (int) (sampleRate * 120.0 / blockSize);

        AecProcessor aec;
        aec.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        aec.prepareToPlay (sampleRate, blockSize);

        juce::Random random (42);
        juce::AudioBuffer<float> mic (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> reference (blockSize * 2);
        double pending = 0.0;
        juce::int64 resyncsAfterWarmup = -1;

        for (int b = 0; b < numBlocks; ++b)
        {
            // 喇叭那邊的時脈快或慢 driftPpm：每個區塊送進來的參考樣本數平均是 blockSize × (1 + drift)
            pending += blockSize * (1.0 + driftPpm * 1.0e-6);
            const int count = (int) pending;
            pending -= count;

            for (int i = 0; i < count; ++i)
                reference[(size_t) i] = (random.nextFloat() * 2.0f - 1.0f) * 0.1f;

            aec.pushReference (reference.data(), count);

            for (int i = 0; i < blockSize; ++i)
            {
                const float s = (random.nextFloat() * 2.0f - 1.0f) * 0.1f;
                mic.setSample (0, i, s);
                mic.setSample (1, i, s);
            }

            aec.processBlock (mic, midi);

            if (b == (int) (sampleRate * 20.0 / blockSize)) // 前 20 秒讓追蹤收斂
                resyncsAfterWarmup = aec.getStats().referenceUnderruns;
        }

        const auto stats = aec.getStats();
        const auto resyncs = stats.referenceUnderruns - resyncsAfterWarmup;
        const bool ok = resyncs == 0 && std::abs (stats.referenceDriftPpm - driftPpm) < 50.0;

        std::printf ("drift %+6.0f ppm -> measured %+7.1f ppm, resyncs after warm-up %lld, lead %.1f ms  %s\n",
                     driftPpm, stats.referenceDriftPpm, (long long) resyncs, stats.referenceDelayMs,
                     ok ? "OK" : "FAIL");
        return ok;
    }

    /** AEC 回報的延遲要與實測相符，而且不因主機區塊大小改變；喇叭參考保持靜音 */
    bool runLatencyCase (int blockSize, double sampleRate)
    {
        const int total = (int) sampleRate * 3;

        AecProcessor aec;
        aec.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        aec.prepareToPlay (sampleRate, blockSize);

        // 不規則脈衝經過共振峰的類人聲訊號
        juce::Random random (3);
        std::vector<float> input ((size_t) total), output ((size_t) total);
        double y1 = 0, y2 = 0;
        int nextPulse = 0;

        for (int i = 0; i < total; ++i)
        {
            double x = 0.0;
            if (i == nextPulse) { x = 1.0; nextPulse += (int) (sampleRate / 48000.0 * (300 + random.nextInt (180))); }
            const double r = std::pow (0.995, 48000.0 / sampleRate), w = juce::MathConstants<double>::twoPi * 800.0 / sampleRate;
            const double y = x + 2.0 * r * std::cos (w) * y1 - r * r * y2;
            y2 = y1; y1 = y;
            input[(size_t) i] = (float) (0.05 * y);
        }

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> silence ((size_t) blockSize, 0.0f);

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            aec.pushReference (silence.data(), blockSize);
            buffer.copyFrom (0, 0, input.data() + pos, blockSize);
            buffer.copyFrom (1, 0, input.data() + pos, blockSize);
            aec.processBlock (buffer, midi);
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize, output.begin() + pos);
        }

        int bestLag = 0;
        double best = -1.0;
        const int start = (int) sampleRate, length = (int) sampleRate;

        for (int lag = 0; lag < (int) (sampleRate * 0.06); ++lag)
        {
            double sum = 0.0;
            for (int i = 0; i < length; ++i)
                sum += (double) input[(size_t) (start + i)] * output[(size_t) (start + i + lag)];
            if (sum > best) { best = sum; bestLag = lag; }
        }

        // AEC 自己用脈衝量，這裡用類人聲量，方法不同會差幾個樣本；0.5 ms 以內算對
        const bool ok = std::abs (bestLag - aec.getLatencySamples()) <= (int) (sampleRate * 0.0005);
        std::printf ("%6.0f Hz block %4d -> measured latency %d samples, reported %d  %s\n",
                     sampleRate, blockSize, bestLag, aec.getLatencySamples(), ok ? "OK" : "FAIL");
        return ok;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    int failures = 0;

    for (double drift : { -1000.0, -300.0, 0.0, 300.0, 1000.0 })
        if (! runCase (drift))
            ++failures;

    std::printf ("\n");

    for (double rate : { 16000.0, 32000.0, 44100.0, 48000.0, 96000.0 })
        for (int blockSize : { 64, 256, 441, 1024 })
            if (! runLatencyCase (blockSize, rate))
                ++failures;

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
