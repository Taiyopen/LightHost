#include "Routing.h"

// 存檔格式：chainInputs=0,2;routes=chain>0,in1>2
// 舊格式 chainInput=0（只有一組）讀取時照樣接受

namespace
{
    bool isNumber (const juce::String& text)
    {
        return text.isNotEmpty() && text.containsOnly ("0123456789");
    }
}

bool Routing::isChainInput (int inputPair) const
{
    return chainInputPairs.count (inputPair) > 0;
}

void Routing::setChainInput (int inputPair, bool shouldFeedChain)
{
    if (shouldFeedChain)
        chainInputPairs.insert (inputPair);
    else
        chainInputPairs.erase (inputPair);
}

bool Routing::isRouted (int source, int outputPair) const
{
    return routes.count ({ source, outputPair }) > 0;
}

void Routing::setRouted (int source, int outputPair, bool shouldRoute)
{
    if (shouldRoute)
        routes.insert ({ source, outputPair });
    else
        routes.erase ({ source, outputPair });
}

juce::String Routing::toString() const
{
    juce::StringArray inputs;

    for (int pair : chainInputPairs)
        inputs.add (juce::String (pair));

    juce::StringArray items;

    for (const auto& [source, outputPair] : routes)
        items.add ((source == processedChain ? juce::String ("chain") : "in" + juce::String (source))
                   + ">" + juce::String (outputPair));

    return "chainInputs=" + inputs.joinIntoString (",") + ";routes=" + items.joinIntoString (",");
}

Routing Routing::fromString (const juce::String& text)
{
    Routing routing;

    for (const auto& part : juce::StringArray::fromTokens (text, ";", {}))
    {
        const auto key = part.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = part.fromFirstOccurrenceOf ("=", false, false).trim();

        if (key == "chainInputs" || key == "chainInput")
        {
            for (const auto& item : juce::StringArray::fromTokens (value, ",", {}))
                if (isNumber (item.trim()))
                    routing.chainInputPairs.insert (item.trim().getIntValue());
        }
        else if (key == "routes")
        {
            for (const auto& item : juce::StringArray::fromTokens (value, ",", {}))
            {
                const auto sourceText = item.upToFirstOccurrenceOf (">", false, false).trim();
                const auto outputText = item.fromFirstOccurrenceOf (">", false, false).trim();

                if (! isNumber (outputText))
                    continue;

                if (sourceText == "chain")
                    routing.routes.insert ({ processedChain, outputText.getIntValue() });
                else if (sourceText.startsWith ("in") && isNumber (sourceText.substring (2)))
                    routing.routes.insert ({ sourceText.substring (2).getIntValue(), outputText.getIntValue() });
            }
        }
    }

    return routing;
}

Routing Routing::makeDefault (int firstInputPair, int firstOutputPair)
{
    Routing routing;

    if (firstInputPair >= 0)
        routing.chainInputPairs.insert (firstInputPair);

    if (firstOutputPair >= 0)
        routing.routes.insert ({ processedChain, firstOutputPair });

    return routing;
}
