#pragma once

#include "../INoiseReducer.h"
#include <memory>
#include <vector>

struct NoiseReducerChoice
{
    const char* id;              // 存進設定檔
    const char* displayName;     // 顯示在設定視窗
    // 以下為比較表用的中文說明（UTF-8，交給 JUCE 時要用 String::fromUTF8）
    const char* bandwidth;       // 處理到多高的頻率
    const char* maxReduction;    // 「最多壓幾 dB」怎麼做到的
    const char* note;            // 一句話特色
};

/** 設定視窗列出的降噪演算法，依顯示順序 */
const std::vector<NoiseReducerChoice>& getNoiseReducerChoices();

/** 依 id 建立降噪；模型載入失敗時回傳 nullptr，error 說明原因。會讀檔，不可在音訊執行緒呼叫 */
std::unique_ptr<INoiseReducer> createNoiseReducer (const juce::String& id, juce::String& error);
