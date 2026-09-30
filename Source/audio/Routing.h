#pragma once

#include <JuceHeader.h>
#include <set>
#include <utility>

/**
 * 哪些聲音送到哪些輸出。以「聲道組」為單位：第 p 組 = 裝置的第 2p、2p+1 聲道
 * （跟 Audio 分頁的立體聲成對勾選一致）。用裝置上的絕對編號，取消勾選再勾回來設定仍在。
 */
struct Routing
{
    /** 來源編號：處理後的麥克風；其他非負數是輸入聲道組（原音） */
    static constexpr int processedChain = -1;

    std::set<int> chainInputPairs;        // 送進處理鏈的輸入組；多組時先混在一起
    std::set<std::pair<int, int>> routes; // { 來源, 輸出聲道組 }

    bool isChainInput (int inputPair) const;
    void setChainInput (int inputPair, bool shouldFeedChain);
    bool isRouted (int source, int outputPair) const;
    void setRouted (int source, int outputPair, bool shouldRoute);

    juce::String toString() const;
    static Routing fromString (const juce::String& text);

    /** 沒設定過時的預設：第一組輸入 → 處理 → 第一組輸出（跟舊版行為相同） */
    static Routing makeDefault (int firstInputPair, int firstOutputPair);
};
