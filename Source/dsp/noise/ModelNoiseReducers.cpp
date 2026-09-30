#include "ModelNoiseReducers.h"
#include "OnnxStreamingModel.h"
#include "rnnoise.h"
#include <stdexcept>

// DeepFilterNet 的 C 介面（third_party/deepfilternet，見其 README）
extern "C"
{
    DFState* df_create (const char* path, float attenLim, const char* logLevel);
    size_t df_get_frame_length (DFState* state);
    void df_set_atten_lim (DFState* state, float limDb);
    float df_process_frame (DFState* state, float* input, float* output);
    void df_free (DFState* state);
}

//==============================================================================
RNNoiseReducer::RNNoiseReducer()
{
    state = rnnoise_create (nullptr);

    if (state == nullptr)
        throw std::runtime_error ("RNNoise could not be created.");

    const auto frame = (size_t) rnnoise_get_frame_size();
    scaledIn.assign (frame, 0.0f);
    scaledOut.assign (frame, 0.0f);
}

RNNoiseReducer::~RNNoiseReducer()
{
    rnnoise_destroy (state);
}

FramedNoiseReducer::Format RNNoiseReducer::getFormat() const
{
    const int frame = rnnoise_get_frame_size();
    // 輸出比輸入晚兩格（20 ms）；NoiseBench 實測確認
    return { 48000, frame, 2 * frame, false };
}

void RNNoiseReducer::processHop (const float* in, float* out)
{
    // RNNoise 吃的是 16 位元整數的數值範圍
    const int n = (int) scaledIn.size();

    for (int i = 0; i < n; ++i)
        scaledIn[(size_t) i] = in[i] * 32768.0f;

    rnnoise_process_frame (state, scaledOut.data(), scaledIn.data());

    for (int i = 0; i < n; ++i)
        out[i] = scaledOut[(size_t) i] * (1.0f / 32768.0f);
}

void RNNoiseReducer::resetModel()
{
    rnnoise_init (state, nullptr);
}

//==============================================================================
FastEnhancerReducer::FastEnhancerReducer (const juce::File& modelFile)
    : model (std::make_unique<OnnxStreamingModel> (modelFile))
{
    hopSize = (int) model->inputSize ("wav_in");
    // 第一個狀態是 STFT 的重疊部分，長度 = n_fft − hop，也就是輸出比輸入晚的樣本數
    latencySamples = (int) model->inputSize ("cache_in_0");

    if (hopSize <= 0 || latencySamples <= 0 || model->output ("wav_out") == nullptr)
        throw std::runtime_error ("Unexpected FastEnhancer model format.");
}

FastEnhancerReducer::~FastEnhancerReducer() = default;

FramedNoiseReducer::Format FastEnhancerReducer::getFormat() const
{
    return { 48000, hopSize, latencySamples, false };
}

void FastEnhancerReducer::processHop (const float* in, float* out)
{
    std::copy (in, in + hopSize, model->input ("wav_in"));

    // 推論出錯時不能讓例外跑出音訊執行緒；這一格讓原音通過
    try { model->run(); }
    catch (const std::exception&) { std::copy (in, in + hopSize, out); return; }

    std::copy (model->output ("wav_out"), model->output ("wav_out") + hopSize, out);
    model->feedbackStates();
}

void FastEnhancerReducer::resetModel()
{
    model->clearStates();
}

//==============================================================================
GtcrnReducer::GtcrnReducer (const juce::File& modelFile)
    : model (std::make_unique<OnnxStreamingModel> (modelFile))
{
    if (model->inputSize ("mix") != (size_t) numBins * 2 || model->output ("enh") == nullptr)
        throw std::runtime_error ("Unexpected GTCRN model format.");

    // 與訓練時相同：週期性 Hann 窗開根號，分析與合成各乘一次，50% 重疊時剛好還原
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize));

    frame.assign ((size_t) fftSize, 0.0f);
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    overlap.assign ((size_t) fftSize, 0.0f);
}

GtcrnReducer::~GtcrnReducer() = default;

FramedNoiseReducer::Format GtcrnReducer::getFormat() const
{
    return { 16000, hop, fftSize - hop, false };
}

void GtcrnReducer::processHop (const float* in, float* out)
{
    // 分析：保留最近 512 個樣本、加窗、FFT
    std::copy (frame.begin() + hop, frame.end(), frame.begin());
    std::copy (in, in + hop, frame.begin() + hop);

    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = frame[(size_t) i] * window[(size_t) i];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    fft.performRealOnlyForwardTransform (fftData.data(), true);

    // JUCE 的輸出是 [實部, 虛部] 交錯，剛好就是模型 mix [1,257,1,2] 的排列
    std::copy (fftData.begin(), fftData.begin() + numBins * 2, model->input ("mix"));

    // 推論出錯時不能讓例外跑出音訊執行緒；這一格沿用原本的頻譜
    try
    {
        model->run();
        std::copy (model->output ("enh"), model->output ("enh") + numBins * 2, fftData.begin());
        model->feedbackStates();
    }
    catch (const std::exception&) {}

    // 合成：反 FFT（JUCE 已除以 N）、加窗、重疊相加
    fft.performRealOnlyInverseTransform (fftData.data());

    for (int i = 0; i < fftSize; ++i)
        overlap[(size_t) i] += fftData[(size_t) i] * window[(size_t) i];

    std::copy (overlap.begin(), overlap.begin() + hop, out);
    std::copy (overlap.begin() + hop, overlap.end(), overlap.begin());
    std::fill (overlap.begin() + hop, overlap.end(), 0.0f);
}

void GtcrnReducer::resetModel()
{
    model->clearStates();
    std::fill (frame.begin(), frame.end(), 0.0f);
    std::fill (overlap.begin(), overlap.end(), 0.0f);
}

//==============================================================================
DeepFilterReducer::DeepFilterReducer (const juce::File& modelArchive)
{
    // df_create 找不到檔案時會直接讓程式崩潰，先自己檢查
    if (! modelArchive.existsAsFile())
        throw std::runtime_error ("Model file not found: " + modelArchive.getFullPathName().toStdString());

    state = df_create (modelArchive.getFullPathName().toRawUTF8(), 100.0f, nullptr);

    if (state == nullptr)
        throw std::runtime_error ("DeepFilterNet could not be created.");

    frameLength = (int) df_get_frame_length (state);
    inCopy.assign ((size_t) frameLength, 0.0f);
}

DeepFilterReducer::~DeepFilterReducer()
{
    if (state != nullptr)
        df_free (state);
}

FramedNoiseReducer::Format DeepFilterReducer::getFormat() const
{
    // 模型會往後看，輸出比輸入晚三格（30 ms）；介面沒有提供，數值由 NoiseBench 實測。
    // 這裡只用來顯示延遲（DeepFilterNet 自己處理「最多壓幾 dB」，不混回原音）
    return { 48000, frameLength, 3 * frameLength, true };
}

void DeepFilterReducer::processHop (const float* in, float* out)
{
    // 介面要的是可寫的輸入指標
    std::copy (in, in + frameLength, inCopy.begin());
    df_process_frame (state, inCopy.data(), out);
}

void DeepFilterReducer::applyNativeAttenuationLimit (float db)
{
    df_set_atten_lim (state, db);
}
