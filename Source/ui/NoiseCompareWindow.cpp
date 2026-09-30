#include "NoiseCompareWindow.h"
#include "../AppTheme.h"
#include "../audio/AudioEngine.h"
#include "../dsp/noise/NoiseReducerBenchmark.h"
#include "../dsp/noise/NoiseReducerFactory.h"
#include <map>

namespace
{
    /** 這個視窗的文字是中文；JUCE 直接吃 const char* 會當成 ASCII，要明確當 UTF-8 */
    juce::String zh (const char* utf8) { return juce::String::fromUTF8 (utf8); }
}

class NoiseCompareWindow::Panel : public juce::Component,
                                  private juce::TableListBoxModel,
                                  private juce::Timer
{
public:
    Panel (AudioEngine& engineIn, const juce::String& currentIdIn)
        : engine (engineIn), currentId (currentIdIn)
    {
        auto& header = table.getHeader();
        header.addColumn (zh ("演算法"),         columnName,      200);
        header.addColumn (zh ("延遲（計算）"),   columnLatency,    90);
        header.addColumn (zh ("延遲（實測）"),   columnMeasured,   90);
        header.addColumn (zh ("CPU（本機）"),    columnCpu,        90);
        header.addColumn (zh ("處理頻寬"),       columnBandwidth, 110);
        header.addColumn (zh ("最多壓幾 dB"),    columnMax,        90);
        header.addColumn (zh ("說明"),           columnNotes,     400);
        header.setStretchToFitActive (true);

        table.setModel (this);
        table.setRowHeight (26);
        addAndMakeVisible (table);

        measureButton.setButtonText (zh ("在這台電腦上實測（CPU 與延遲）"));
        measureButton.onClick = [this] { startBenchmark (true); };
        addAndMakeVisible (measureButton);

        addAndMakeVisible (status);

        totalLatency.setFont (juce::FontOptions { 15.0f, juce::Font::bold });
        addAndMakeVisible (totalLatency);
        refreshTotalLatency();
        startTimer (1000);   // 裝置設定或開關改了，最上方的總延遲跟著變

        footnote.setText (zh ("延遲（計算）：這個演算法在目前裝置設定下讓聲音多晚多少（不是 48 kHz 時要轉換取樣率，約再多 20 毫秒）。"
                              "延遲（實測）：用測試訊號實際比對輸入輸出的時間差，應與計算值相差不到 2 毫秒。"
                              "CPU：佔單一核心的百分比，用模擬人聲的測試訊號量測，實際講話時會有些差異。"
                              "混回原音：把對齊時間的原音按比例混回，達成「最多壓幾 dB」。"
                              "音質好壞請用耳朵判斷。"),
                          juce::dontSendNotification);
        footnote.setFont (juce::FontOptions { 12.0f });
        footnote.setColour (juce::Label::textColourId, juce::Colours::grey);
        footnote.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (footnote);

        benchmark.onResult = [this] (const NoiseReducerBenchmark::Result& r)
        {
            auto& stored = results[r.id];
            const double previousCpu = stored.cpuPercent;
            const double previousMeasured = stored.measuredLatencyMs;
            stored = r;

            if (stored.cpuPercent < 0.0)  // 只算延遲時保留之前實測的結果
            {
                stored.cpuPercent = previousCpu;
                stored.measuredLatencyMs = previousMeasured;
            }

            ++finishedCount;
            status.setText ((running ? zh ("量測中… ") : juce::String()) + juce::String (finishedCount) + " / "
                                + juce::String ((int) getNoiseReducerChoices().size()),
                            juce::dontSendNotification);
            table.updateContent();
            table.repaint();
        };

        benchmark.onFinished = [this]
        {
            running = false;
            measureButton.setEnabled (true);
            status.setText (lastRunMeasuredCpu ? zh ("完成。") : juce::String(), juce::dontSendNotification);
        };

        // 一打開就先算延遲（只載入模型，很快）
        startBenchmark (false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        totalLatency.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);

        footnote.setBounds (area.removeFromBottom (48));
        area.removeFromBottom (6);

        auto buttonRow = area.removeFromBottom (30);
        measureButton.setBounds (buttonRow.removeFromLeft (200));
        buttonRow.removeFromLeft (10);
        status.setBounds (buttonRow);
        area.removeFromBottom (8);

        table.setBounds (area);
    }

private:
    enum Columns { columnName = 1, columnLatency, columnMeasured, columnCpu, columnBandwidth, columnMax, columnNotes };

    void timerCallback() override
    {
        refreshTotalLatency();
    }

    void refreshTotalLatency()
    {
        totalLatency.setText (zh ("目前麥克風總延遲：") + engine.getLatencyReport().describe(), juce::dontSendNotification);
    }

    void startBenchmark (bool measureCpu)
    {
        if (benchmark.isRunning())
            return;

        double sampleRate = 48000.0;
        int blockSize = 256;

        if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
        {
            sampleRate = device->getCurrentSampleRate();
            blockSize = device->getCurrentBufferSizeSamples();
        }

        running = true;
        lastRunMeasuredCpu = measureCpu;
        finishedCount = 0;
        measureButton.setEnabled (false);
        status.setText (measureCpu ? zh ("量測中…（每種約 2～4 秒）") : zh ("載入模型中…"),
                        juce::dontSendNotification);
        benchmark.start (sampleRate, blockSize, measureCpu);
    }

    int getNumRows() override
    {
        return (int) getNoiseReducerChoices().size();
    }

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool) override
    {
        if (juce::isPositiveAndBelow (row, getNumRows()) && currentId == getNoiseReducerChoices()[(size_t) row].id)
            g.fillAll (juce::Colours::lightblue.withAlpha (0.25f));
        else if (row % 2 == 1)
            g.fillAll (juce::Colours::white.withAlpha (0.03f));
    }

    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (row, getNumRows()))
            return;

        const auto& choice = getNoiseReducerChoices()[(size_t) row];
        const auto found = results.find (choice.id);
        const auto* result = found != results.end() ? &found->second : nullptr;
        juce::String text;

        switch (column)
        {
            case columnName:
                text = juce::String (choice.displayName) + (currentId == choice.id ? zh ("（目前）") : juce::String());
                break;

            case columnLatency:
                text = result == nullptr ? juce::String ("...") : ! result->loaded ? zh ("無法使用")
                                                 : juce::String (juce::roundToInt (result->latencyMs)) + " ms";
                break;

            case columnMeasured:
                text = result == nullptr || ! result->loaded || result->measuredLatencyMs < 0.0
                           ? "-" : juce::String (juce::roundToInt (result->measuredLatencyMs)) + " ms";
                break;

            case columnCpu:
                text = result == nullptr || ! result->loaded || result->cpuPercent < 0.0
                           ? "-" : juce::String (result->cpuPercent, 1) + " %";
                break;

            case columnBandwidth: text = zh (choice.bandwidth); break;
            case columnMax:       text = zh (choice.maxReduction); break;
            case columnNotes:     text = result != nullptr && ! result->loaded ? zh ("無法載入：") + result->error
                                                                               : zh (choice.note);
                                  break;
            default: break;
        }

        g.setColour (getLookAndFeel().findColour (juce::ListBox::textColourId));
        g.setFont (juce::FontOptions { 13.0f });
        g.drawText (text, 6, 0, width - 8, height, juce::Justification::centredLeft, true);
    }

    AudioEngine& engine;
    const juce::String currentId;
    std::map<juce::String, NoiseReducerBenchmark::Result> results;
    int finishedCount = 0;
    bool running = false;
    bool lastRunMeasuredCpu = false;

    juce::TableListBox table;
    juce::TextButton measureButton;
    juce::Label status, footnote, totalLatency;
    NoiseReducerBenchmark benchmark;   // 最後宣告、最先毀：先停背景執行緒，再拆介面
};

NoiseCompareWindow::NoiseCompareWindow (AudioEngine& engine, const juce::String& currentAlgorithmId)
    : DocumentWindow (zh ("降噪演算法比較"),
                      getDialogBackgroundColour(),
                      DocumentWindow::closeButton)
{
    panel = new Panel (engine, currentAlgorithmId);
    setContentOwned (panel, true);
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setResizeLimits (700, 400, 1700, 900);
    setSize (1080, 470);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

NoiseCompareWindow::~NoiseCompareWindow()
{
    clearContentComponent();
}

void NoiseCompareWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}
