#include "AecMonitorWindow.h"
#include "../IconMenu.hpp"
#include "../AppTheme.h"
#include "../dsp/AecStats.h"
#include <array>

namespace
{
    class LevelMeter : public juce::Component
    {
    public:
        enum class Mode { levelDb, attenuationDb };

        void setMode (Mode newMode)
        {
            mode = newMode;
        }

        void setLevelDb (float newLevelDb)
        {
            mode = Mode::levelDb;
            levelDb = newLevelDb;
            repaint();
        }

        void setAttenuationDb (float newAttenuationDb)
        {
            mode = Mode::attenuationDb;
            attenuationDb = newAttenuationDb;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.15f));
            g.fillRoundedRectangle (bounds, 3.0f);

            const float proportion = mode == Mode::attenuationDb
                                   ? attenuationDbToMeterProportion (attenuationDb)
                                   : dbToMeterProportion (levelDb);
            auto fill = bounds.removeFromLeft (bounds.getWidth() * proportion);

            g.setColour (mode == Mode::attenuationDb
                             ? juce::Colours::deepskyblue.withAlpha (0.9f)
                             : (proportion > 0.85f ? juce::Colours::orangered
                                                   : juce::Colours::limegreen.withAlpha (0.85f)));
            g.fillRoundedRectangle (fill, 3.0f);

            const juce::String text = mode == Mode::attenuationDb
                                    ? ("+" + juce::String (attenuationDb, 1) + " dB")
                                    : (juce::String (levelDb, 1) + " dB");

            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::FontOptions { 12.0f });
            g.drawText (text,
                        getLocalBounds().reduced (6, 0),
                        juce::Justification::centredLeft,
                        true);
        }

    private:
        Mode mode = Mode::levelDb;
        float levelDb = -100.0f;
        float attenuationDb = 0.0f;
    };

    class StatRow : public juce::Component
    {
    public:
        StatRow (const juce::String& labelText)
        {
            label.setText (labelText, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (label);
            addAndMakeVisible (value);
        }

        void setValue (const juce::String& text)
        {
            value.setText (text, juce::dontSendNotification);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            label.setBounds (area.removeFromLeft (juce::jmax (120, area.getWidth() / 2)));
            value.setBounds (area);
        }

    private:
        juce::Label label, value;
    };
}

class AecMonitorWindow::MonitorPanel : public juce::Component
{
public:
    MonitorPanel (IconMenu& owner_)
        : owner (owner_)
    {
        title.setText ("AEC Monitor", juce::dontSendNotification);
        title.setFont (juce::FontOptions { 18.0f, juce::Font::bold });
        title.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title);

        addAndMakeVisible (statusRow);
        addAndMakeVisible (referenceSourceRow);
        addAndMakeVisible (loopbackRow);
        addAndMakeVisible (referenceGainRow);
        addAndMakeVisible (aecStrengthRow);
        addAndMakeVisible (referenceDelayRow);
        addAndMakeVisible (erleRow);
        addAndMakeVisible (underrunsRow);
        addAndMakeVisible (samplesRow);

        for (auto& channel : channels)
        {
            channel.label.setText (channel.name, juce::dontSendNotification);
            channel.label.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (channel.label);
            addAndMakeVisible (channel.meter);
        }

        channels[2].meter.setMode (LevelMeter::Mode::attenuationDb);

        hint.setText ("Mic (input) is the live microphone. Output is after AEC3."
                      " ERLE comes from AEC3 internal estimate."
                      " Echo Attenuation is per-frame mic vs output while reference is active.",
                      juce::dontSendNotification);
        hint.setJustificationType (juce::Justification::topLeft);
        hint.setFont (juce::FontOptions { 11.0f });
        hint.setColour (juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible (hint);
    }

    void updateSnapshot (const AecMonitorSnapshot& snap)
    {
        statusRow.setValue (snap.aecEnabled ? "Enabled" : "Disabled");
        referenceSourceRow.setValue (snap.referenceSource);
        loopbackRow.setValue (snap.loopbackStatus);
        referenceGainRow.setValue ((snap.referenceGainDb >= 0.0f ? "+" : "")
                                   + juce::String (snap.referenceGainDb, 1) + " dB");
        aecStrengthRow.setValue (juce::String (snap.aecStrengthPercent, 0) + "%");
        referenceDelayRow.setValue (juce::String (snap.referenceDelayMs, 1) + " ms"
                                    + " / " + juce::String (snap.referenceTargetDelayMs, 1) + " ms");
        erleRow.setValue (juce::String (snap.erleDb, 1) + " dB");
        underrunsRow.setValue (juce::String (snap.referenceUnderruns));
        samplesRow.setValue ("ref " + juce::String (snap.referenceSamplesReceived)
                             + " / proc " + juce::String (snap.samplesProcessed));

        channels[0].meter.setLevelDb (snap.referenceLevelDb);
        channels[1].meter.setLevelDb (snap.micRawLevelDb);
        channels[2].meter.setAttenuationDb (snap.echoRemovedDb);
        channels[3].meter.setLevelDb (snap.outputLevelDb);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        title.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);

        layoutRow (area, statusRow);
        layoutRow (area, referenceSourceRow);
        layoutRow (area, loopbackRow);
        layoutRow (area, referenceGainRow);
        layoutRow (area, aecStrengthRow);
        layoutRow (area, referenceDelayRow);
        layoutRow (area, erleRow);
        layoutRow (area, underrunsRow);
        layoutRow (area, samplesRow);

        area.removeFromTop (8);

        for (auto& channel : channels)
        {
            channel.label.setBounds (area.removeFromTop (18));
            area.removeFromTop (2);
            channel.meter.setBounds (area.removeFromTop (22));
            area.removeFromTop (6);
        }

        area.removeFromTop (4);
        hint.setBounds (area.removeFromTop (48));
    }

private:
    struct ChannelMeter
    {
        juce::String name;
        juce::Label label;
        LevelMeter meter;

        ChannelMeter (const juce::String& channelName)
            : name (channelName)
        {
        }
    };

    static void layoutRow (juce::Rectangle<int>& area, juce::Component& row)
    {
        row.setBounds (area.removeFromTop (22));
        area.removeFromTop (2);
    }

    IconMenu& owner;
    juce::Label title, hint;
    StatRow statusRow { "Status" },
            referenceSourceRow { "Reference Source" },
            loopbackRow { "Loopback Thread" },
            referenceGainRow { "Reference Gain" },
            aecStrengthRow { "AEC Strength" },
            referenceDelayRow { "Ref Delay" },
            erleRow { "ERLE" },
            underrunsRow { "Reference Underruns" },
            samplesRow { "Sample Counts" };

    std::array<ChannelMeter, 4> channels { {
        ChannelMeter { "Reference (aligned)" },
        ChannelMeter { "Mic (input)" },
        ChannelMeter { "Echo Attenuation" },
        ChannelMeter { "Output (AEC out)" }
    } };
};

AecMonitorWindow::AecMonitorWindow (IconMenu& owner_)
    : DocumentWindow ("AEC Monitor",
                      getDialogBackgroundColour(),
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner (owner_)
{
    panel = new MonitorPanel (owner);
    setContentOwned (panel, true);
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setResizeLimits (360, 520, 600, 800);
    setSize (420, 560);
    centreWithSize (getWidth(), getHeight());

    restoreWindowStateFromString (getAppProperties().getUserSettings()->getValue ("aecMonitorWindowPos"));
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
    startTimerHz (10);
}

AecMonitorWindow::~AecMonitorWindow()
{
    stopTimer();
    getAppProperties().getUserSettings()->setValue ("aecMonitorWindowPos", getWindowStateAsString());
    clearContentComponent();
}

void AecMonitorWindow::closeButtonPressed()
{
   #if JUCE_MAC
    Process::setDockIconVisible (false);
   #endif
    owner.closeAecMonitorWindow();
}

void AecMonitorWindow::timerCallback()
{
    if (panel != nullptr)
        panel->updateSnapshot (owner.getAecMonitorSnapshot());
}
