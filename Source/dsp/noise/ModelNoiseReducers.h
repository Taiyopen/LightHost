#pragma once

#include "FramedNoiseReducer.h"
#include <map>

struct DenoiseState;
struct DFState;
class OnnxStreamingModel;

/** RNNoise 0.2（48 kHz、每次 480 樣本） */
class RNNoiseReducer : public FramedNoiseReducer
{
public:
    RNNoiseReducer();
    ~RNNoiseReducer() override;

protected:
    Format getFormat() const override;
    void processHop (const float* in, float* out) override;
    void resetModel() override;

private:
    DenoiseState* state = nullptr;
    std::vector<float> scaledIn, scaledOut;
};

/** FastEnhancer（48 kHz；T/B/S 每次 512 樣本、M 每次 320 樣本，ONNX） */
class FastEnhancerReducer : public FramedNoiseReducer
{
public:
    explicit FastEnhancerReducer (const juce::File& modelFile);
    ~FastEnhancerReducer() override;

protected:
    Format getFormat() const override;
    void processHop (const float* in, float* out) override;
    void resetModel() override;

private:
    std::unique_ptr<OnnxStreamingModel> model;
    int hopSize = 320;
    int latencySamples = 704;
};

/** GTCRN（16 kHz、每次 256 樣本；自己做 STFT，模型只處理一格頻譜，ONNX） */
class GtcrnReducer : public FramedNoiseReducer
{
public:
    explicit GtcrnReducer (const juce::File& modelFile);
    ~GtcrnReducer() override;

protected:
    Format getFormat() const override;
    void processHop (const float* in, float* out) override;
    void resetModel() override;

private:
    static constexpr int fftOrder = 9;
    static constexpr int fftSize = 1 << fftOrder;  // 512
    static constexpr int hop = fftSize / 2;         // 256
    static constexpr int numBins = fftSize / 2 + 1; // 257

    std::unique_ptr<OnnxStreamingModel> model;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, frame, fftData, overlap;
};

/** DeepFilterNet 3（48 kHz、每次 480 樣本；自帶「最多壓幾 dB」） */
class DeepFilterReducer : public FramedNoiseReducer
{
public:
    explicit DeepFilterReducer (const juce::File& modelArchive);
    ~DeepFilterReducer() override;

protected:
    Format getFormat() const override;
    void processHop (const float* in, float* out) override;
    void resetModel() override {}   // DeepFilterNet 沒有提供重設；舊的歷史幾個音框後就會被沖掉
    void applyNativeAttenuationLimit (float db) override;

private:
    DFState* state = nullptr;
    int frameLength = 480;
    std::vector<float> inCopy;
};
