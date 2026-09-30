#include "NoiseReducerBenchmark.h"
#include "NoiseReducerFactory.h"
#include <chrono>

namespace
{
    /** 類似人聲的訊號：間隔不規則的脈衝經過兩個共振峰，每秒三個音節，加上背景白噪音 */
    std::vector<float> makeTestSignal (double sampleRate, int numSamples)
    {
        std::vector<float> signal ((size_t) numSamples);
        juce::Random random (7);
        double y1a = 0, y2a = 0, y1b = 0, y2b = 0;

        auto resonator = [sampleRate] (double freq, double bandwidth, double x, double& y1, double& y2)
        {
            const double r = std::exp (-juce::MathConstants<double>::pi * bandwidth / sampleRate);
            const double y = x + 2.0 * r * std::cos (juce::MathConstants<double>::twoPi * freq / sampleRate) * y1 - r * r * y2;
            y2 = y1;
            y1 = y;
            return y;
        };

        int nextPulse = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            double pulse = 0.0;

            if (i == nextPulse)
            {
                pulse = 1.0;
                nextPulse += (int) (sampleRate / (100.0 + random.nextFloat() * 60.0));
            }

            const double voiced = resonator (700.0, 120.0, pulse, y1a, y2a) + 0.6 * resonator (1200.0, 150.0, pulse, y1b, y2b);
            const double syllable = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * i / sampleRate);
            const double noise = (random.nextFloat() * 2.0f - 1.0f) * 0.01;
            signal[(size_t) i] = (float) (0.02 * voiced * syllable + noise);
        }

        return signal;
    }

    /** 把輸出往前挪，找出和輸入對得最整齊的位置（樣本數）；只看中間一秒，最多找 150 ms */
    int findLag (const std::vector<float>& input, const std::vector<float>& output, double sampleRate)
    {
        const int start = (int) (sampleRate * 0.5);
        const int length = (int) sampleRate;
        const int maxLag = juce::jmin ((int) (sampleRate * 0.15), (int) output.size() - start - length);
        int bestLag = 0;
        double best = -1.0;

        for (int lag = 0; lag < maxLag; ++lag)
        {
            double sum = 0.0;
            const float* in = input.data() + start;
            const float* out = output.data() + start + lag;

            for (int i = 0; i < length; ++i)
                sum += (double) in[i] * out[i];

            if (sum > best)
            {
                best = sum;
                bestLag = lag;
            }
        }

        return bestLag;
    }
}

NoiseReducerBenchmark::NoiseReducerBenchmark()
    : juce::Thread ("NoiseReducerBenchmark")
{
    weakThis = this;
}

NoiseReducerBenchmark::~NoiseReducerBenchmark()
{
    stopThread (10000);
}

void NoiseReducerBenchmark::start (double sampleRateIn, int blockSizeIn, bool measureCpuIn)
{
    if (isThreadRunning())
        return;

    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 48000.0;
    blockSize = juce::jmax (32, blockSizeIn);
    measureCpu = measureCpuIn;
    startThread (juce::Thread::Priority::low);
}

void NoiseReducerBenchmark::post (std::function<void()> fn)
{
    juce::MessageManager::callAsync ([weak = weakThis, fn]
    {
        if (weak != nullptr)
            fn();
    });
}

void NoiseReducerBenchmark::run()
{
    const double seconds = 2.0;   // 0.5 秒暖機＋1 秒比對＋最多 0.15 秒位移，2 秒夠用
    const int numSamples = (int) (sampleRate * seconds);
    const auto signal = measureCpu ? makeTestSignal (sampleRate, numSamples) : std::vector<float>();

    for (const auto& choice : getNoiseReducerChoices())
    {
        if (threadShouldExit())
            return;

        Result result;
        result.id = choice.id;

        auto reducer = createNoiseReducer (choice.id, result.error);
        result.loaded = reducer != nullptr;

        if (reducer != nullptr)
        {
            reducer->prepare (sampleRate, blockSize);
            result.latencyMs = 1000.0 * reducer->getLatencySeconds();

            if (measureCpu)
            {
                reducer->setMaxAttenuationDb (100.0f);
                juce::AudioBuffer<float> buffer (2, blockSize);
                std::vector<float> output ((size_t) numSamples, 0.0f);
                double busy = 0.0;

                for (int pos = 0; pos + blockSize <= numSamples && ! threadShouldExit(); pos += blockSize)
                {
                    buffer.copyFrom (0, 0, signal.data() + pos, blockSize);
                    buffer.copyFrom (1, 0, signal.data() + pos, blockSize);

                    const auto begin = std::chrono::steady_clock::now();
                    reducer->process (buffer);
                    busy += std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count();

                    std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize, output.begin() + pos);
                }

                result.cpuPercent = 100.0 * busy / seconds;
                result.measuredLatencyMs = 1000.0 * findLag (signal, output, sampleRate) / sampleRate;
            }
        }

        post ([this, result] { if (onResult) onResult (result); });
    }

    post ([this] { if (onFinished) onFinished(); });
}
