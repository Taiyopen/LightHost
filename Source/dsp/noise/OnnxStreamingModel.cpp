#include "OnnxStreamingModel.h"
#include <onnxruntime_cxx_api.h>
#include <stdexcept>

namespace
{
    Ort::Env& getEnv()
    {
        static Ort::Env env (ORT_LOGGING_LEVEL_WARNING, "LightHost");
        return env;
    }

    struct Tensor
    {
        std::string name;
        std::vector<int64_t> shape;
        std::vector<float> data;
        Ort::Value value { nullptr };
    };

    std::vector<Tensor> createTensors (Ort::Session& session, bool inputs)
    {
        Ort::AllocatorWithDefaultOptions allocator;
        const auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        const size_t count = inputs ? session.GetInputCount() : session.GetOutputCount();
        std::vector<Tensor> tensors (count);

        for (size_t i = 0; i < count; ++i)
        {
            auto& t = tensors[i];
            t.name = (inputs ? session.GetInputNameAllocated (i, allocator)
                             : session.GetOutputNameAllocated (i, allocator)).get();

            const auto info = inputs ? session.GetInputTypeInfo (i) : session.GetOutputTypeInfo (i);
            const auto tensorInfo = info.GetTensorTypeAndShapeInfo();

            if (tensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                throw std::runtime_error ("Model tensor " + t.name + " is not float.");

            t.shape = tensorInfo.GetShape();
            size_t elements = 1;

            for (auto& dim : t.shape)
            {
                if (dim <= 0)
                    throw std::runtime_error ("Model tensor " + t.name + " has a dynamic shape.");
                elements *= (size_t) dim;
            }

            t.data.assign (elements, 0.0f);
            t.value = Ort::Value::CreateTensor<float> (memory, t.data.data(), t.data.size(),
                                                       t.shape.data(), t.shape.size());
        }

        return tensors;
    }

    std::string stateOutputNameFor (const std::string& inputName)
    {
        if (inputName.rfind ("cache_in_", 0) == 0)
            return "cache_out_" + inputName.substr (9);

        return inputName + "_out";
    }
}

struct OnnxStreamingModel::Impl
{
    Ort::Session session { nullptr };
    Ort::RunOptions runOptions;
    std::vector<Tensor> inputs, outputs;
    std::vector<const char*> inputNames, outputNames;
    std::vector<const OrtValue*> inputValues;
    std::vector<OrtValue*> outputValues;
    std::vector<std::pair<Tensor*, const Tensor*>> statePairs; // 輸入狀態 ← 輸出狀態

    Tensor* find (std::vector<Tensor>& list, const char* name)
    {
        for (auto& t : list)
            if (t.name == name)
                return &t;
        return nullptr;
    }
};

OnnxStreamingModel::OnnxStreamingModel (const juce::File& modelFile)
    : impl (std::make_unique<Impl>())
{
    if (! modelFile.existsAsFile())
        throw std::runtime_error ("Model file not found: " + modelFile.getFullPathName().toStdString());

    Ort::SessionOptions options;
    // 在音訊執行緒上直接跑，不要另開執行緒池
    options.SetIntraOpNumThreads (1);
    options.SetInterOpNumThreads (1);
    options.SetExecutionMode (ORT_SEQUENTIAL);
    options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

    impl->session = Ort::Session (getEnv(), modelFile.getFullPathName().toWideCharPointer(), options);
    impl->inputs = createTensors (impl->session, true);
    impl->outputs = createTensors (impl->session, false);

    for (auto& t : impl->inputs)
    {
        impl->inputNames.push_back (t.name.c_str());
        impl->inputValues.push_back (t.value);

        if (auto* out = impl->find (impl->outputs, stateOutputNameFor (t.name).c_str()))
            if (out->data.size() == t.data.size())
                impl->statePairs.push_back ({ &t, out });
    }

    for (auto& t : impl->outputs)
    {
        impl->outputNames.push_back (t.name.c_str());
        impl->outputValues.push_back (t.value);
    }
}

OnnxStreamingModel::~OnnxStreamingModel() = default;

float* OnnxStreamingModel::input (const char* name)
{
    auto* t = impl->find (impl->inputs, name);
    return t != nullptr ? t->data.data() : nullptr;
}

const float* OnnxStreamingModel::output (const char* name) const
{
    auto* t = impl->find (impl->outputs, name);
    return t != nullptr ? t->data.data() : nullptr;
}

size_t OnnxStreamingModel::inputSize (const char* name) const
{
    auto* t = impl->find (impl->inputs, name);
    return t != nullptr ? t->data.size() : 0;
}

void OnnxStreamingModel::run()
{
    // 輸出張量已綁在自己的緩衝區上，ONNX Runtime 會直接寫進去
    Ort::ThrowOnError (Ort::GetApi().Run (impl->session, impl->runOptions,
                                          impl->inputNames.data(), impl->inputValues.data(), impl->inputNames.size(),
                                          impl->outputNames.data(), impl->outputNames.size(),
                                          impl->outputValues.data()));
}

void OnnxStreamingModel::feedbackStates()
{
    for (auto& [in, out] : impl->statePairs)
        std::copy (out->data.begin(), out->data.end(), in->data.begin());
}

void OnnxStreamingModel::clearStates()
{
    for (auto& [in, out] : impl->statePairs)
        std::fill (in->data.begin(), in->data.end(), 0.0f);
}
