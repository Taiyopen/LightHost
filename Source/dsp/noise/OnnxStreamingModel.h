#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

/**
 * 串流用的 ONNX 模型：輸入輸出都是形狀固定的 float 張量，緩衝區在載入時配好並綁定，
 * run() 不再配置輸出張量。狀態張量依名稱配對（xxx ↔ xxx_out、cache_in_N ↔ cache_out_N），
 * 由 feedbackStates() 把輸出狀態抄回輸入。載入失敗時丟出 std::exception。
 */
class OnnxStreamingModel
{
public:
    explicit OnnxStreamingModel (const juce::File& modelFile);
    ~OnnxStreamingModel();

    float* input (const char* name);
    const float* output (const char* name) const;
    size_t inputSize (const char* name) const;

    void run();
    void feedbackStates();
    void clearStates();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
