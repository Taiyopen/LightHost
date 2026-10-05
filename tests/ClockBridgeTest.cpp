// ClockBridge 的離線檢查：兩個裝置各用各的時脈與取樣率，寫入端每 10 ms 送一包（像 WASAPI），
// 讀取端用自己的區塊大小讀。確認暖機後不再重新對準、量到的時脈差正確、聲音連續（沒有喀聲）。
// 用法：ClockBridgeTest.exe

#include <JuceHeader.h>
#include "audio/ClockBridge.h"
#include "audio/ExternalDevice.h"
#include <cstdio>

namespace
{
    struct Case
    {
        double sourceRate, readerRate, driftPpm;
        int readerBlock;
        bool gaps;   // 每 3 秒停 1 秒
    };

    bool run (const Case& c)
    {
        constexpr double seconds = 120.0, warmup = 5.0, toneHz = 1000.0;
        const int packet = (int) (c.sourceRate / 100.0);
        const int target = (int) (c.sourceRate * 0.025);

        ClockBridge bridge;
        bridge.prepare (2, (int) c.sourceRate);
        bridge.configureReader (c.sourceRate, c.readerRate, target);

        std::vector<float> left ((size_t) packet), right ((size_t) packet);
        std::vector<float> outL ((size_t) c.readerBlock), outR ((size_t) c.readerBlock);
        float* outs[] = { outL.data(), outR.data() };
        const float* ins[] = { left.data(), right.data() };

        juce::int64 written = 0;
        double phase = 0.0;
        const double sourceClock = c.sourceRate * (1.0 + c.driftPpm * 1.0e-6);   // 來源實際的樣本速度
        const int numBlocks = (int) (seconds * c.readerRate / c.readerBlock);

        float y1 = 0.0f, y2 = 0.0f;
        int history = 0;
        double worstResidual = 0.0;
        juce::int64 resyncsAtWarmup = 0;
        int gapCount = 0;
        bool wasPlaying = true;

        for (int b = 0; b < numBlocks; ++b)
        {
            const double t = (double) (b + 1) * c.readerBlock / c.readerRate;
            const bool playing = ! c.gaps || std::fmod (t, 3.0) < 2.0;

            if (wasPlaying && ! playing)
                ++gapCount;
            wasPlaying = playing;

            // 寫入端：到這個時間點為止該送出的整包
            const auto due = (juce::int64) (t * sourceClock / packet) * packet;

            while (written + packet <= due)
            {
                for (int i = 0; i < packet; ++i)
                {
                    left[(size_t) i] = right[(size_t) i] = (float) (0.5 * std::sin (phase));
                    phase += juce::MathConstants<double>::twoPi * toneHz / c.sourceRate;
                }

                if (playing)
                    bridge.write (ins, packet);

                written += packet;
            }

            const bool got = bridge.read (outs, 2, c.readerBlock);

            if (t > warmup && b == (int) (warmup * c.readerRate / c.readerBlock) + 1)
                resyncsAtWarmup = bridge.getResyncCount();

            if (! got)
            {
                history = 0;
                continue;
            }

            // 純正弦波滿足 y[n] = 2cos(w)·y[n-1] − y[n-2]；喀聲、跳點會讓殘差突然變大
            const double w = juce::MathConstants<double>::twoPi * toneHz / c.readerRate;

            for (int i = 0; i < c.readerBlock; ++i)
            {
                const float y = outL[(size_t) i];

                if (history >= 2 && t > warmup)
                    worstResidual = juce::jmax (worstResidual, (double) std::abs (y - (2.0 * std::cos (w) * y1 - y2)));

                y2 = y1;
                y1 = y;
                ++history;
            }
        }

        const auto resyncs = bridge.getResyncCount() - resyncsAtWarmup;
        const bool driftOk = c.gaps || std::abs (bridge.getDriftPpm() - c.driftPpm) < 30.0;
        const bool ok = (c.gaps ? resyncs <= gapCount : resyncs == 0) && driftOk && worstResidual < 0.01;

        std::printf ("%6.0f -> %6.0f Hz, block %4d, drift %+5.0f ppm%s: measured %+7.1f ppm, resyncs %lld, worst jump %.5f  %s\n",
                     c.sourceRate, c.readerRate, c.readerBlock, c.driftPpm, c.gaps ? " (gaps)" : "",
                     bridge.getDriftPpm(), (long long) resyncs, worstResidual, ok ? "OK" : "FAIL");
        return ok;
    }
}

int main (int argc, char** argv)
{
    // --list：只列出 Windows 的音訊端點與格式（不開裝置、不出聲）
    if (argc > 1 && juce::String (argv[1]) == "--list")
    {
        for (bool inputs : { true, false })
            for (const auto& e : enumerateWasapiEndpoints (inputs))
                std::printf ("%s %-50s %6d Hz %2d ch  period %5.2f ms, low-latency min %5.2f ms\n", inputs ? "[In] " : "[Out]", e.name.toRawUTF8(), e.sampleRate, e.numChannels, e.defaultPeriodMs, e.minLowLatencyPeriodMs);

        return 0;
    }

    int failures = 0;

    const Case cases[] =
    {
        { 48000, 48000,    0,  512, false },
        { 48000, 48000,  300,   64, false },
        { 48000, 48000, -300,  512, false },
        { 44100, 48000, -200,  256, false },
        { 48000, 44100,  200,  441, false },
        { 96000, 48000,  100,  512, false },
        { 48000, 96000, -100, 1024, false },
        { 48000, 48000,  200,  256, true  },
    };

    for (const auto& c : cases)
        if (! run (c))
            ++failures;

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
