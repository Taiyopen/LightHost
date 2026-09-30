// 降噪演算法的離線檢查：純白噪音是否被壓低、「最多壓幾 dB」是否生效、
// 44.1 kHz（要轉取樣率）是否正常、有沒有 NaN，以及每秒音訊要花多少 CPU 時間。
// 用法：NoiseBench.exe（在建置輸出資料夾執行，旁邊要有 models\ 與 DLL）

#include <JuceHeader.h>
#include "dsp/noise/NoiseReducerFactory.h"
#include <chrono>
#include <cstdio>

namespace
{
    struct Result
    {
        double attenuationDb = 0.0;
        double realtimeFactor = 0.0; // 處理時間 / 音訊長度
        bool finite = true;
    };

    Result run (INoiseReducer& reducer, double sampleRate, float maxAttenuationDb)
    {
        constexpr int blockSize = 256;
        const int totalSamples = (int) sampleRate * 6;
        const int skipSamples = (int) sampleRate * 2; // 前 2 秒讓模型收斂，不列入計算

        reducer.setMaxAttenuationDb (maxAttenuationDb);
        reducer.prepare (sampleRate, blockSize);

        juce::Random random (1234);
        juce::AudioBuffer<float> buffer (2, blockSize);
        double inEnergy = 0.0, outEnergy = 0.0, seconds = 0.0;
        Result result;

        for (int pos = 0; pos < totalSamples; pos += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float noise = (random.nextFloat() * 2.0f - 1.0f) * 0.05f; // 約 -32 dBFS 的白噪音
                buffer.setSample (0, i, noise);
                buffer.setSample (1, i, noise);

                if (pos >= skipSamples)
                    inEnergy += (double) noise * noise;
            }

            const auto start = std::chrono::steady_clock::now();
            reducer.process (buffer);
            seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

            for (int i = 0; i < blockSize; ++i)
            {
                const float y = buffer.getSample (0, i);
                result.finite = result.finite && std::isfinite (y);

                if (pos >= skipSamples)
                    outEnergy += (double) y * y;
            }
        }

        result.attenuationDb = 10.0 * std::log10 (inEnergy / juce::jmax (outEnergy, 1.0e-20));
        result.realtimeFactor = seconds / ((double) totalSamples / sampleRate);
        return result;
    }

    /** 用類似人聲的訊號（間隔隨機的脈衝經過兩個共振峰）比對輸入輸出，找出時間差 */
    double measureLatencyMs (INoiseReducer& reducer, double sampleRate)
    {
        constexpr int blockSize = 256;
        const int total = (int) sampleRate * 4;
        std::vector<float> input ((size_t) total), output ((size_t) total);

        juce::Random random (7);
        double y1a = 0, y2a = 0, y1b = 0, y2b = 0;
        auto resonator = [sampleRate] (double freq, double bandwidth, double x, double& y1, double& y2)
        {
            const double r = std::exp (-juce::MathConstants<double>::pi * bandwidth / sampleRate);
            const double y = x + 2.0 * r * std::cos (juce::MathConstants<double>::twoPi * freq / sampleRate) * y1 - r * r * y2;
            y2 = y1; y1 = y;
            return y;
        };

        int nextPulse = 0;
        for (int i = 0; i < total; ++i)
        {
            double x = 0.0;
            if (i == nextPulse)
            {
                x = 1.0;
                nextPulse += (int) (sampleRate / (100.0 + random.nextFloat() * 60.0)); // 100–160 Hz、不規則
            }

            const double voiced = resonator (700.0, 120.0, x, y1a, y2a) + 0.6 * resonator (1200.0, 150.0, x, y1b, y2b);
            const double syllable = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * i / sampleRate);
            input[(size_t) i] = (float) (0.02 * voiced * syllable);
        }

        reducer.setMaxAttenuationDb (100.0f);
        reducer.prepare (sampleRate, blockSize);
        juce::AudioBuffer<float> buffer (2, blockSize);

        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            buffer.copyFrom (0, 0, input.data() + pos, blockSize);
            buffer.copyFrom (1, 0, input.data() + pos, blockSize);
            reducer.process (buffer);
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize, output.begin() + pos);
        }

        const int start = (int) sampleRate, length = (int) sampleRate * 2, maxLag = (int) (sampleRate * 0.1);
        int bestLag = 0;
        double best = -1.0;

        for (int lag = 0; lag < maxLag; ++lag)
        {
            double sum = 0.0;
            for (int i = 0; i < length; ++i)
                sum += (double) input[(size_t) (start + i)] * output[(size_t) (start + i + lag)];

            if (sum > best) { best = sum; bestLag = lag; }
        }

        return 1000.0 * bestLag / sampleRate;
    }
}

namespace
{
    /** 計算值與實測差超過 2 ms 就算失敗；回傳失敗數 */
    int checkLatencies()
    {
        int failures = 0;
        std::printf ("\n%-26s %8s %14s %14s\n", "algorithm", "rate", "computed(ms)", "measured(ms)");

        for (const auto& choice : getNoiseReducerChoices())
        {
            for (double rate : { 48000.0, 44100.0 })
            {
                juce::String error;
                auto reducer = createNoiseReducer (choice.id, error);
                if (reducer == nullptr)
                    continue;

                const double measured = measureLatencyMs (*reducer, rate);
                const double computed = 1000.0 * reducer->getLatencySeconds();
                const bool ok = std::abs (computed - measured) < 2.0;
                failures += ok ? 0 : 1;
                std::printf ("%-26s %8.0f %14.1f %14.1f  %s\n", choice.displayName, rate, computed, measured,
                             ok ? "" : "<- mismatch");
            }
        }

        return failures;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit; // 部分 JUCE 類別需要訊息執行緒環境
    int failures = 0;

    std::printf ("%-18s %8s %8s %14s %14s %8s\n", "algorithm", "rate", "limit", "reduction(dB)", "cpu(x rt)", "finite");

    for (const auto& choice : getNoiseReducerChoices())
    {
        if (juce::String (choice.id) == "simple")
            continue;

        for (double rate : { 48000.0, 44100.0 })
        {
            for (float limit : { 100.0f, 20.0f })
            {
                juce::String error;
                auto reducer = createNoiseReducer (choice.id, error);

                if (reducer == nullptr)
                {
                    std::printf ("%-18s LOAD FAILED: %s\n", choice.displayName, error.toRawUTF8());
                    ++failures;
                    break;
                }

                const auto r = run (*reducer, rate, limit);
                std::printf ("%-18s %8.0f %8s %14.1f %14.4f %8s\n", choice.displayName, rate,
                             limit >= 100.0f ? "none" : "20 dB", r.attenuationDb, r.realtimeFactor,
                             r.finite ? "yes" : "NO");

                // 預期：不限制時噪音至少壓 10 dB；限制 20 dB 時落在 14～26 dB；CPU 比即時快；沒有 NaN
                const bool ok = r.finite
                                && r.realtimeFactor < 1.0
                                && (limit >= 100.0f ? r.attenuationDb > 10.0
                                                    : std::abs (r.attenuationDb - 20.0) < 6.0);
                if (! ok)
                {
                    std::printf ("  ^ unexpected result\n");
                    ++failures;
                }
            }
        }
    }

    failures += checkLatencies();

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
