#include "NoiseReducerFactory.h"
#include "ModelNoiseReducers.h"
#include "../SimpleNoiseReducer.h"

namespace
{
    /** 模型檔放在執行檔旁的 models 資料夾（CMake 建置後與安裝檔都會複製過去） */
    juce::File getModelFile (const char* name)
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                   .getSiblingFile ("models")
                   .getChildFile (name);
    }
}

const std::vector<NoiseReducerChoice>& getNoiseReducerChoices()
{
    static const std::vector<NoiseReducerChoice> choices
    {
        { "rnnoise",        "RNNoise",                   "全頻段（24 kHz）", "混回原音",
          "非常省 CPU。對鍵盤聲、突發雜音效果不錯；壓得重時聲音可能有點悶。" },
        { "deepfilternet",  "DeepFilterNet 3",           "全頻段（24 kHz）", "模型內建",
          "最乾淨。會往後看 30 毫秒，所以延遲最高。" },
        { "fastenhancer_t", "FastEnhancer T (lightest)", "全頻段（24 kHz）", "混回原音",
          "FastEnhancer 最輕的版本，降噪力道最弱。" },
        { "fastenhancer_b", "FastEnhancer B",            "全頻段（24 kHz）", "混回原音",
          "輕量，降噪力道中等。" },
        { "fastenhancer_s", "FastEnhancer S",            "全頻段（24 kHz）", "混回原音",
          "音質與 CPU 用量最平衡。" },
        { "fastenhancer",   "FastEnhancer M",            "全頻段（24 kHz）", "混回原音",   // id 沿用最早的版本，已存的設定不用改
          "FastEnhancer 降噪最強，也最吃 CPU。" },
        { "gtcrn",          "GTCRN (16 kHz)",            "只到 8 kHz",       "混回原音",
          "模型極小，但 8 kHz 以上會被切掉，聲音像電話。" },
        { "simple",         "Simple (legacy)",           "全頻段（24 kHz）", "-",
          "只有噪音閘，不是 AI。幾乎不吃 CPU。" },
    };

    return choices;
}

std::unique_ptr<INoiseReducer> createNoiseReducer (const juce::String& id, juce::String& error)
{
    try
    {
        if (id == "rnnoise")        return std::make_unique<RNNoiseReducer>();
        if (id == "deepfilternet")  return std::make_unique<DeepFilterReducer> (getModelFile ("DeepFilterNet3_onnx.tar.gz"));
        if (id == "fastenhancer")   return std::make_unique<FastEnhancerReducer> (getModelFile ("fastenhancer_m.onnx"));
        if (id == "fastenhancer_s") return std::make_unique<FastEnhancerReducer> (getModelFile ("fastenhancer_s.onnx"));
        if (id == "fastenhancer_b") return std::make_unique<FastEnhancerReducer> (getModelFile ("fastenhancer_b.onnx"));
        if (id == "fastenhancer_t") return std::make_unique<FastEnhancerReducer> (getModelFile ("fastenhancer_t.onnx"));
        if (id == "gtcrn")          return std::make_unique<GtcrnReducer> (getModelFile ("gtcrn_simple.onnx"));
        if (id == "simple")         return std::make_unique<SimpleNoiseReducer>();

        error = "Unknown noise reduction algorithm: " + id;
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
    }

    return nullptr;
}
