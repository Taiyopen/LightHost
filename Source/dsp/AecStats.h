#pragma once

#include <JuceHeader.h>

/** AEC 即時監控快照（由 UI 執行緒讀取） */
struct AecMonitorSnapshot
{
    bool aecEnabled = false;
    juce::String referenceSource;
    juce::String loopbackStatus;
    float referenceGainDb = 0.0f;

    float aecStrengthPercent = 100.0f;

    float referenceDelayMs = 0.0f;
    float referenceTargetDelayMs = 0.0f;
    float referenceLevelDb = -100.0f;
    float micRawLevelDb = -100.0f;
    float micLevelDb = -100.0f;
    float outputLevelDb = -100.0f;
    float echoRemovedDb = 0.0f;
    float erleDb = 0.0f;

    juce::int64 referenceSamplesReceived = 0;
    juce::int64 samplesProcessed = 0;
    juce::int64 referenceUnderruns = 0;
};

/** AecProcessor 內部累積的執行緒安全統計 */
struct AecProcessorStats
{
    float aecStrengthPercent = 100.0f;
    float referenceDelayMs = 0.0f;
    float referenceTargetDelayMs = 0.0f;
    float referenceLevelDb = -100.0f;
    float micRawLevelDb = -100.0f;
    float micLevelDb = -100.0f;
    float outputLevelDb = -100.0f;
    float echoRemovedDb = 0.0f;
    float erleDb = 0.0f;
    juce::int64 referenceSamplesReceived = 0;
    juce::int64 samplesProcessed = 0;
    juce::int64 referenceUnderruns = 0;
};

inline float dbToMeterProportion (float db, float floorDb = -60.0f, float ceilingDb = 0.0f)
{
    return juce::jlimit (0.0f, 1.0f, (db - floorDb) / (ceilingDb - floorDb));
}

inline float attenuationDbToMeterProportion (float attenuationDb, float ceilingDb = 40.0f)
{
    return juce::jlimit (0.0f, 1.0f, attenuationDb / ceilingDb);
}
