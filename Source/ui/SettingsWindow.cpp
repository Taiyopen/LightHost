#include "SettingsWindow.h"
#include "../IconMenu.hpp"
#include "../AppSettings.h"
#include "../AppTheme.h"
#include "../audio/LoopbackDevices.h"
#include "../dsp/noise/NoiseReducerFactory.h"
#include "NoiseCompareWindow.h"

namespace
{
    class AudioSettingsTab : public juce::Component,
                             private juce::ChangeListener
    {
    public:
        explicit AudioSettingsTab (IconMenu& owner_)
            : owner (owner_),
              audioSettings (owner.getEngine().getDeviceManager(), 0, 256, 0, 256, false, false, true, true)
        {
            addAndMakeVisible (audioSettings);
            owner.getEngine().getDeviceManager().addChangeListener (this);
        }

        ~AudioSettingsTab() override
        {
            owner.getEngine().getDeviceManager().removeChangeListener (this);
        }

        void resized() override
        {
            audioSettings.setBounds (getLocalBounds());
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            owner.getEngine().saveAudioDeviceStateAndRebuild();
        }

        IconMenu& owner;
        juce::AudioDeviceSelectorComponent audioSettings;
    };

    class RoutingSettingsTab : public juce::Component,
                               private juce::ChangeListener
    {
    public:
        explicit RoutingSettingsTab (IconMenu& owner_)
            : owner (owner_)
        {
            hint.setText ("Processing: inputs sent through echo cancellation, noise reduction and plugins.\n"
                          "Output columns: where each sound goes. Sounds sharing an input or output are mixed.",
                          juce::dontSendNotification);
            hint.setFont (juce::FontOptions { 12.0f });
            hint.setColour (juce::Label::textColourId, juce::Colours::grey);
            hint.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (hint);

            viewport.setViewedComponent (&grid, false);
            addAndMakeVisible (viewport);

            // Audio 分頁換裝置或改勾選聲道時跟著更新
            owner.getEngine().getDeviceManager().addChangeListener (this);
            refresh();
        }

        ~RoutingSettingsTab() override
        {
            owner.getEngine().getDeviceManager().removeChangeListener (this);
        }

        void refresh()
        {
            auto& engine = owner.getEngine();
            const auto routing = engine.getEffectiveRouting();
            const auto inputs = engine.getActiveInputPairs();
            const auto outputs = engine.getActiveOutputPairs();

            toggles.clear();
            labels.clear();

            constexpr int rowLabelWidth = 220;
            constexpr int columnWidth = 110;
            constexpr int dividerGap = 24;   // Processing 欄與輸出欄之間的間隔
            constexpr int rowHeight = 28;
            constexpr int headerHeight = 40;
            constexpr int firstOutputX = rowLabelWidth + columnWidth + dividerGap;

            auto addLabel = [this] (const juce::String& text, juce::Rectangle<int> bounds, juce::Justification justification)
            {
                auto* label = labels.add (new juce::Label ({}, text));
                label->setJustificationType (justification);
                label->setMinimumHorizontalScale (0.6f);
                label->setBounds (bounds);
                grid.addAndMakeVisible (label);
            };

            auto addToggle = [this] (bool isOn, juce::Rectangle<int> cell, std::function<void (bool)> onChange)
            {
                auto* toggle = toggles.add (new juce::ToggleButton());
                toggle->setToggleState (isOn, juce::dontSendNotification);
                toggle->onClick = [toggle, onChange] { onChange (toggle->getToggleState()); };
                toggle->setBounds (cell.withSizeKeepingCentre (24, cell.getHeight() - 4));
                grid.addAndMakeVisible (toggle);
            };

            addLabel ("Processing", { rowLabelWidth, 0, columnWidth, headerHeight }, juce::Justification::centred);

            for (size_t c = 0; c < outputs.size(); ++c)
                addLabel (outputs[c].name, { firstOutputX + (int) c * columnWidth, 0, columnWidth, headerHeight },
                          juce::Justification::centred);

            struct Row
            {
                int source;
                juce::String name;
            };

            std::vector<Row> rows { { Routing::processedChain, "Processed microphone" } };

            for (const auto& pair : inputs)
                rows.push_back ({ pair.index, pair.name + " (raw)" });

            for (size_t r = 0; r < rows.size(); ++r)
            {
                const int y = headerHeight + (int) r * rowHeight;
                const int source = rows[r].source;

                addLabel (rows[r].name, { 0, y, rowLabelWidth, rowHeight }, juce::Justification::centredLeft);

                // 處理後的聲音本身不能再送回處理鏈
                if (source == Routing::processedChain)
                {
                    addLabel ("-", { rowLabelWidth, y, columnWidth, rowHeight }, juce::Justification::centred);
                }
                else
                {
                    addToggle (routing.isChainInput (source), { rowLabelWidth, y, columnWidth, rowHeight },
                               [this, source] (bool isOn)
                               {
                                   auto updated = owner.getEngine().getEffectiveRouting();
                                   updated.setChainInput (source, isOn);
                                   owner.getEngine().setRouting (updated);
                               });
                }

                for (size_t c = 0; c < outputs.size(); ++c)
                {
                    const int outputPair = outputs[c].index;

                    addToggle (routing.isRouted (source, outputPair),
                               { firstOutputX + (int) c * columnWidth, y, columnWidth, rowHeight },
                               [this, source, outputPair] (bool isOn)
                               {
                                   auto updated = owner.getEngine().getEffectiveRouting();
                                   updated.setRouted (source, outputPair, isOn);
                                   owner.getEngine().setRouting (updated);
                               });
                }
            }

            if (outputs.empty())
                addLabel ("No output channels are enabled on the Audio page.",
                          { firstOutputX, 0, 360, headerHeight }, juce::Justification::centredLeft);

            grid.setSize (firstOutputX + juce::jmax (360, (int) outputs.size() * columnWidth),
                          headerHeight + (int) rows.size() * rowHeight);
            grid.repaint();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            hint.setBounds (area.removeFromTop (40));
            area.removeFromTop (4);
            viewport.setBounds (area);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            refresh();
        }

        IconMenu& owner;
        juce::Label hint;

        // 宣告順序：grid 要比 viewport 與格子裡的元件都晚毀
        juce::Component grid;
        juce::Viewport viewport;
        juce::OwnedArray<juce::Label> labels;
        juce::OwnedArray<juce::ToggleButton> toggles;
    };

    class AecSettingsTab : public juce::Component
    {
    public:
        explicit AecSettingsTab (IconMenu& owner_)
            : owner (owner_)
        {
            aecToggle.setButtonText ("Echo Cancellation (AEC)");
            aecToggle.onClick = [this] { applyAecToggle(); };
            addAndMakeVisible (aecToggle);

            nrToggle.setButtonText ("Noise Reduction");
            nrToggle.onClick = [this] { owner.getEngine().setNoiseReductionEnabled (nrToggle.getToggleState()); };
            addAndMakeVisible (nrToggle);

            nrAlgorithmLabel.setText ("Algorithm", juce::dontSendNotification);
            nrAlgorithmLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (nrAlgorithmLabel);

            const auto& choices = getNoiseReducerChoices();
            for (int i = 0; i < (int) choices.size(); ++i)
                nrAlgorithmCombo.addItem (choices[(size_t) i].displayName, i + 1);

            nrAlgorithmCombo.onChange = [this]
            {
                if (ignoreCallbacks || nrAlgorithmCombo.getSelectedId() <= 0)
                    return;

                owner.getEngine().setNoiseReducerAlgorithm (getNoiseReducerChoices()[(size_t) nrAlgorithmCombo.getSelectedId() - 1].id);
                refreshFromOwner();
            };
            addAndMakeVisible (nrAlgorithmCombo);

            compareButton.setButtonText ("Compare...");
            compareButton.onClick = [this] { openCompareWindow(); };
            addAndMakeVisible (compareButton);

            nrMaxLabel.setText ("Max Reduction", juce::dontSendNotification);
            nrMaxLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (nrMaxLabel);

            // 最右邊代表不限制；唱歌時調低可以減少長音被削掉
            nrMaxSlider.setRange (6.0, nrMaxUnlimitedPosition, 1.0);
            nrMaxSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            nrMaxSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
            nrMaxSlider.textFromValueFunction = [] (double v)
            {
                return v >= nrMaxUnlimitedPosition ? juce::String ("Unlimited") : juce::String ((int) v) + " dB";
            };
            nrMaxSlider.valueFromTextFunction = [] (const juce::String& text)
            {
                return text.containsIgnoreCase ("unlim") ? nrMaxUnlimitedPosition : text.getDoubleValue();
            };
            nrMaxSlider.onValueChange = [this]
            {
                if (ignoreCallbacks)
                    return;

                const double v = nrMaxSlider.getValue();
                owner.getEngine().setNoiseReductionMaxAttenuation (v >= nrMaxUnlimitedPosition ? 100.0f : (float) v);
            };
            addAndMakeVisible (nrMaxSlider);

            nrErrorLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);
            nrErrorLabel.setFont (juce::FontOptions { 12.0f });
            addAndMakeVisible (nrErrorLabel);

            strengthLabel.setText ("AEC Strength", juce::dontSendNotification);
            strengthLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (strengthLabel);

            strengthSlider.setRange (25.0, 150.0, 1.0);
            strengthSlider.setTextValueSuffix (" %");
            strengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            strengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
            strengthSlider.onValueChange = [this]
            {
                if (ignoreCallbacks)
                    return;

                owner.getEngine().setAecStrength (static_cast<float> (strengthSlider.getValue()));
            };
            addAndMakeVisible (strengthSlider);

            gainLabel.setText ("Reference Level", juce::dontSendNotification);
            gainLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (gainLabel);

            gainSlider.setRange (-40.0, 40.0, 0.5);
            gainSlider.setTextValueSuffix (" dB");
            gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
            gainSlider.onValueChange = [this]
            {
                if (ignoreCallbacks)
                    return;

                getSettings().setReferenceGainDb (static_cast<float> (gainSlider.getValue()));
                owner.getEngine().rebuildGraph();
            };
            addAndMakeVisible (gainSlider);

           #if JUCE_WINDOWS
            referenceLabel.setText ("Speaker Reference", juce::dontSendNotification);
            referenceLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (referenceLabel);

            referenceCombo.onChange = [this]
            {
                if (ignoreCallbacks)
                    return;

                applyReferenceSelection();
            };
            addAndMakeVisible (referenceCombo);
           #endif

            monitorButton.setButtonText ("Open AEC Monitor...");
            monitorButton.onClick = [this] { owner.openAecMonitorWindow(); };
            addAndMakeVisible (monitorButton);

            refreshFromOwner();
        }

        void refreshFromOwner()
        {
            ignoreCallbacks = true;

            aecToggle.setToggleState (getSettings().isAecEnabled(), juce::dontSendNotification);
            nrToggle.setToggleState (getSettings().isNrEnabled(), juce::dontSendNotification);

            const auto& choices = getNoiseReducerChoices();
            for (int i = 0; i < (int) choices.size(); ++i)
                if (getSettings().getNrAlgorithm() == choices[(size_t) i].id)
                    nrAlgorithmCombo.setSelectedId (i + 1, juce::dontSendNotification);

            const float maxDb = getSettings().getNrMaxAttenuationDb();
            nrMaxSlider.setValue (maxDb >= 100.0f ? nrMaxUnlimitedPosition : maxDb, juce::dontSendNotification);

            const auto error = owner.getEngine().getNoiseReducerError();
            nrErrorLabel.setText (error.isEmpty() ? juce::String() : "Could not load the model, using Simple: " + error,
                                  juce::dontSendNotification);
            strengthSlider.setValue (getSettings().getAecStrengthPercent(), juce::dontSendNotification);
            gainSlider.setValue (getSettings().getReferenceGainDb(), juce::dontSendNotification);

           #if JUCE_WINDOWS
            rebuildReferenceCombo();
           #endif

            updateControlStates();
            ignoreCallbacks = false;
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            const int rowHeight = 28;
            const int gap = 8;

            auto placeRow = [&] (juce::Component& comp)
            {
                comp.setBounds (area.removeFromTop (rowHeight));
                area.removeFromTop (gap);
            };

            placeRow (aecToggle);
            placeRow (nrToggle);

            auto algorithmRow = area.removeFromTop (rowHeight);
            nrAlgorithmLabel.setBounds (algorithmRow.removeFromLeft (140));
            compareButton.setBounds (algorithmRow.removeFromRight (100));
            algorithmRow.removeFromRight (6);
            nrAlgorithmCombo.setBounds (algorithmRow);
            area.removeFromTop (gap);

            auto maxRow = area.removeFromTop (rowHeight);
            nrMaxLabel.setBounds (maxRow.removeFromLeft (140));
            nrMaxSlider.setBounds (maxRow);
            nrErrorLabel.setBounds (area.removeFromTop (20));
            area.removeFromTop (gap);

            auto strengthRow = area.removeFromTop (rowHeight);
            strengthLabel.setBounds (strengthRow.removeFromLeft (140));
            strengthSlider.setBounds (strengthRow);
            area.removeFromTop (gap);

            auto gainRow = area.removeFromTop (rowHeight);
            gainLabel.setBounds (gainRow.removeFromLeft (140));
            gainSlider.setBounds (gainRow);
            area.removeFromTop (gap);

           #if JUCE_WINDOWS
            auto refRow = area.removeFromTop (rowHeight);
            referenceLabel.setBounds (refRow.removeFromLeft (140));
            referenceCombo.setBounds (refRow);
            area.removeFromTop (gap);
           #endif

            placeRow (monitorButton);
        }

    private:
       #if JUCE_WINDOWS
        void rebuildReferenceCombo()
        {
            referenceCombo.clear (juce::dontSendNotification);

            referenceCombo.addItem ("App Output (recommended for ASIO)", 1);
            referenceCombo.addSeparator();
            referenceCombo.addItem ("System Loopback (advanced)", 2);
            referenceCombo.addItem ("System Default (" + getDefaultLoopbackDeviceName() + ")", 3);

            const auto outputDevices = enumerateLoopbackOutputDevices();
            for (int i = 0; i < outputDevices.size(); ++i)
                referenceCombo.addItem (outputDevices.getReference (i).name, 4 + i);

            if (! getSettings().useSystemLoopbackReference())
            {
                referenceCombo.setSelectedId (1, juce::dontSendNotification);
            }
            else if (getSettings().getReferenceDeviceId().isEmpty())
            {
                referenceCombo.setSelectedId (3, juce::dontSendNotification);
            }
            else
            {
                const juce::String selectedId = getSettings().getReferenceDeviceId();
                int matchedId = 3;

                for (int i = 0; i < outputDevices.size(); ++i)
                {
                    if (outputDevices.getReference (i).id == selectedId)
                    {
                        matchedId = 4 + i;
                        break;
                    }
                }

                referenceCombo.setSelectedId (matchedId, juce::dontSendNotification);
            }
        }

        void applyReferenceSelection()
        {
            const int selectedId = referenceCombo.getSelectedId();

            if (selectedId == 1)
            {
                getSettings().setUseSystemLoopbackReference (false);
            }
            else if (selectedId == 2)
            {
                getSettings().setUseSystemLoopbackReference (true);
                getSettings().setReferenceDeviceId ({});
            }
            else if (selectedId == 3)
            {
                getSettings().setUseSystemLoopbackReference (true);
                getSettings().setReferenceDeviceId ({});
            }
            else if (selectedId >= 4)
            {
                const auto outputDevices = enumerateLoopbackOutputDevices();
                const int listIndex = selectedId - 4;

                if (juce::isPositiveAndBelow (listIndex, outputDevices.size()))
                {
                    getSettings().setUseSystemLoopbackReference (true);
                    getSettings().setReferenceDeviceId (outputDevices.getReference (listIndex).id);
                }
            }

            owner.getEngine().rebuildGraph();
        }
       #endif

        void applyAecToggle()
        {
            getSettings().setAecEnabled (aecToggle.getToggleState());
            owner.getEngine().rebuildGraph();
            refreshFromOwner();
        }

        void openCompareWindow()
        {
            if (compareWindow != nullptr)
            {
                compareWindow->toFront (true);
                return;
            }

            compareWindow = std::make_unique<NoiseCompareWindow> (owner.getEngine(), getSettings().getNrAlgorithm());
            compareWindow->onClose = [this] { compareWindow = nullptr; };
        }

        void updateControlStates()
        {
            const bool aecOn = getSettings().isAecEnabled();
            strengthLabel.setEnabled (aecOn);
            strengthSlider.setEnabled (aecOn);
            gainLabel.setEnabled (aecOn);
            gainSlider.setEnabled (aecOn);
           #if JUCE_WINDOWS
            referenceLabel.setEnabled (aecOn);
            referenceCombo.setEnabled (aecOn);
           #endif
            monitorButton.setEnabled (aecOn);
        }

        static constexpr double nrMaxUnlimitedPosition = 61.0;

        IconMenu& owner;
        juce::ToggleButton aecToggle, nrToggle;
        juce::Label nrAlgorithmLabel, nrMaxLabel, nrErrorLabel;
        juce::ComboBox nrAlgorithmCombo;
        juce::Slider nrMaxSlider;
        juce::TextButton compareButton;
        std::unique_ptr<NoiseCompareWindow> compareWindow;
        juce::Label strengthLabel, gainLabel;
       #if JUCE_WINDOWS
        juce::Label referenceLabel;
        juce::ComboBox referenceCombo;
       #endif
        juce::Slider strengthSlider, gainSlider;
        juce::TextButton monitorButton;
        bool ignoreCallbacks = false;
    };

    class PluginSettingsTab : public juce::Component,
                              private juce::ListBoxModel
    {
    public:
        explicit PluginSettingsTab (IconMenu& owner_)
            : owner (owner_)
        {
            listBox.setModel (this);
            listBox.setRowHeight (24);
            addAndMakeVisible (listBox);

            editButton.setButtonText ("Edit");
            editButton.onClick = [this] { performOnSelection ([this] (int row) { owner.openPluginEditor (row); }); };
            addAndMakeVisible (editButton);

            bypassButton.setButtonText ("Bypass");
            bypassButton.onClick = [this] { performOnSelection ([this] (int row) { owner.getEngine().togglePluginBypass (row); }); refreshList(); };
            addAndMakeVisible (bypassButton);

            moveUpButton.setButtonText ("Move Up");
            moveUpButton.onClick = [this]
            {
                const int row = listBox.getSelectedRow();

                if (row <= 0)
                    return;

                owner.getEngine().movePluginUp (row);
                refreshList (row - 1);
            };
            addAndMakeVisible (moveUpButton);

            moveDownButton.setButtonText ("Move Down");
            moveDownButton.onClick = [this]
            {
                const int row = listBox.getSelectedRow();

                if (row < 0 || row >= static_cast<int> (plugins.size()) - 1)
                    return;

                owner.getEngine().movePluginDown (row);
                refreshList (row + 1);
            };
            addAndMakeVisible (moveDownButton);

            deleteButton.setButtonText ("Delete");
            deleteButton.onClick = [this]
            {
                const int row = listBox.getSelectedRow();

                if (row < 0)
                    return;

                owner.getEngine().removePlugin (row);
                refreshList();
            };
            addAndMakeVisible (deleteButton);

            addButton.setButtonText ("Add Plugin...");
            addButton.onClick = [this] { showAddPluginMenu(); };
            addAndMakeVisible (addButton);

            mixLabel.setText ("Mix (dry / wet)", juce::dontSendNotification);
            addAndMakeVisible (mixLabel);

            mixSlider.setRange (0.0, 100.0, 1.0);
            mixSlider.setTextValueSuffix (" %");
            mixSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            mixSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
            mixSlider.onValueChange = [this]
            {
                const int row = listBox.getSelectedRow();

                if (juce::isPositiveAndBelow (row, static_cast<int> (plugins.size())))
                    owner.getEngine().setPluginMix (row, static_cast<float> (mixSlider.getValue()));
            };
            addAndMakeVisible (mixSlider);

            refreshList();
        }

        void refreshList (int selectRow = -1)
        {
            plugins = owner.getPlugins().getSortedPlugins();
            listBox.updateContent();
            listBox.repaint();

            if (juce::isPositiveAndBelow (selectRow, static_cast<int> (plugins.size())))
                listBox.selectRow (selectRow);

            updateButtons();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            auto buttonRow = area.removeFromBottom (32);
            area.removeFromBottom (8);

            auto mixRow = area.removeFromBottom (28);
            mixLabel.setBounds (mixRow.removeFromLeft (120));
            mixSlider.setBounds (mixRow);
            area.removeFromBottom (8);

            const int buttonWidth = juce::jmax (72, buttonRow.getWidth() / 6);
            for (auto* button : { &editButton, &bypassButton, &moveUpButton, &moveDownButton, &deleteButton, &addButton })
            {
                button->setBounds (buttonRow.removeFromLeft (buttonWidth).reduced (0, 0));
                buttonRow.removeFromLeft (4);
            }

            listBox.setBounds (area);
        }

        int getNumRows() override
        {
            return static_cast<int> (plugins.size());
        }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
        {
            if (! juce::isPositiveAndBelow (row, static_cast<int> (plugins.size())))
                return;

            if (rowIsSelected)
                g.fillAll (juce::Colours::lightblue.withAlpha (0.35f));

            const auto& plugin = plugins[static_cast<size_t> (row)];
            const bool bypassed = owner.getPlugins().isBypassed (plugin);
            juce::String text = plugin.name;

            if (bypassed)
                text += "  (bypassed)";

            g.setColour (getLookAndFeel().findColour (juce::ListBox::textColourId));
            g.setFont (juce::FontOptions { 14.0f });
            g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        }

        void selectedRowsChanged (int) override
        {
            updateButtons();
        }

        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            if (juce::isPositiveAndBelow (row, static_cast<int> (plugins.size())))
                owner.openPluginEditor (row);
        }

    private:
        using PluginAction = std::function<void (int)>;

        void performOnSelection (PluginAction action)
        {
            const int row = listBox.getSelectedRow();

            if (juce::isPositiveAndBelow (row, static_cast<int> (plugins.size())))
                action (row);
        }

        void updateButtons()
        {
            const int row = listBox.getSelectedRow();
            const bool hasSelection = juce::isPositiveAndBelow (row, static_cast<int> (plugins.size()));

            editButton.setEnabled (hasSelection);
            bypassButton.setEnabled (hasSelection);
            deleteButton.setEnabled (hasSelection);
            moveUpButton.setEnabled (hasSelection && row > 0);
            moveDownButton.setEnabled (hasSelection && row < static_cast<int> (plugins.size()) - 1);

            mixLabel.setEnabled (hasSelection);
            mixSlider.setEnabled (hasSelection);
            mixSlider.setValue (hasSelection ? owner.getEngine().getPluginMix (row) : 100.0, juce::dontSendNotification);
        }

        void showAddPluginMenu()
        {
            juce::PopupMenu menu;
            owner.getPlugins().getKnownPlugins().addToMenu (menu, owner.getPlugins().getSortMethod());

            menu.showMenuAsync (juce::PopupMenu::Options(),
                                [this] (int result)
                                {
                                    if (result <= 0)
                                        return;

                                    const int index = owner.getPlugins().getKnownPlugins().getIndexChosenByMenu (result);

                                    if (index >= 0)
                                    {
                                        owner.getEngine().addPlugin (*owner.getPlugins().getKnownPlugins().getType (index));
                                        refreshList();
                                    }
                                });
        }

        IconMenu& owner;
        std::vector<juce::PluginDescription> plugins;
        juce::ListBox listBox;
        juce::TextButton editButton, bypassButton, moveUpButton, moveDownButton, deleteButton, addButton;
        juce::Label mixLabel;
        juce::Slider mixSlider;
    };

    class UpdatesSettingsTab : public juce::Component,
                               private juce::ChangeListener
    {
    public:
        explicit UpdatesSettingsTab (Updater& updaterIn)
            : updater (updaterIn)
        {
            autoCheckToggle.setButtonText ("Check for updates automatically");
            autoCheckToggle.setToggleState (getSettings().isAutoUpdateCheckEnabled(), juce::dontSendNotification);
            autoCheckToggle.onClick = [this]
            {
                const bool enabled = autoCheckToggle.getToggleState();
                getSettings().setAutoUpdateCheckEnabled (enabled);
                updater.setAutoCheck (enabled);
            };
            addAndMakeVisible (autoCheckToggle);

            versionLabel.setText ("Current version: " + Updater::getCurrentVersion(), juce::dontSendNotification);
            addAndMakeVisible (versionLabel);
            addAndMakeVisible (statusLabel);

            checkButton.setButtonText ("Check Now");
            checkButton.onClick = [this] { updater.checkNow(); };
            addAndMakeVisible (checkButton);

            installButton.setButtonText ("Install Update");
            installButton.onClick = [this] { updater.downloadAndInstall(); };
            addAndMakeVisible (installButton);

            updater.addChangeListener (this);
            refresh();
        }

        ~UpdatesSettingsTab() override
        {
            updater.removeChangeListener (this);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            const int rowHeight = 28;
            const int gap = 8;

            autoCheckToggle.setBounds (area.removeFromTop (rowHeight));
            area.removeFromTop (gap);
            versionLabel.setBounds (area.removeFromTop (rowHeight));
            statusLabel.setBounds (area.removeFromTop (rowHeight));
            area.removeFromTop (gap);

            auto buttonRow = area.removeFromTop (rowHeight);
            checkButton.setBounds (buttonRow.removeFromLeft (120));
            buttonRow.removeFromLeft (8);
            installButton.setBounds (buttonRow.removeFromLeft (140));
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            refresh();
        }

        void refresh()
        {
            using State = Updater::State;
            const auto version = updater.getAvailableRelease().version;
            juce::String status;

            switch (updater.getState())
            {
                case State::idle:              status = "Not checked yet."; break;
                case State::checking:          status = "Checking for updates..."; break;
                case State::upToDate:          status = "You have the latest version."; break;
                case State::available:         status = "Version " + version + " is available."; break;
                case State::downloading:       status = "Downloading version " + version + "..."; break;
                case State::downloadFailed:    status = "Could not install the update: " + updater.getErrorMessage(); break;
                case State::installerLaunched: status = "Starting the installer..."; break;
                case State::checkFailed:       status = "Could not check for updates: " + updater.getErrorMessage(); break;
            }

            statusLabel.setText (status, juce::dontSendNotification);
            checkButton.setEnabled (updater.getState() != State::checking && updater.getState() != State::downloading);
            installButton.setEnabled (updater.canInstall());
        }

        Updater& updater;
        juce::ToggleButton autoCheckToggle;
        juce::Label versionLabel, statusLabel;
        juce::TextButton checkButton, installButton;
    };
}

class SettingsWindow::SettingsPanel : public juce::Component,
                                      private juce::ChangeListener
{
public:
    explicit SettingsPanel (IconMenu& ownerIn)
        : owner (ownerIn),
          tabs (juce::TabbedButtonBar::TabsAtTop),
          audioTab (ownerIn),
          routingTab (ownerIn),
          aecTab (ownerIn),
          pluginTab (ownerIn),
          updatesTab (ownerIn.getUpdater())
    {
        tabs.addTab ("Audio", juce::Colours::transparentBlack, &audioTab, false);
        tabs.addTab ("Routing", juce::Colours::transparentBlack, &routingTab, false);
        tabs.addTab ("AEC", juce::Colours::transparentBlack, &aecTab, false);
        tabs.addTab ("Plugins", juce::Colours::transparentBlack, &pluginTab, false);
        tabs.addTab ("Updates", juce::Colours::transparentBlack, &updatesTab, false);
        tabs.setCurrentTabIndex (0);
        tabs.getTabbedButtonBar().addChangeListener (this);
        addAndMakeVisible (tabs);
    }

    ~SettingsPanel() override
    {
        tabs.getTabbedButtonBar().removeChangeListener (this);
    }

    void resized() override
    {
        tabs.setBounds (getLocalBounds());
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const int index = tabs.getCurrentTabIndex();

        if (index == 1)
            routingTab.refresh();
        else if (index == 2)
            aecTab.refreshFromOwner();
        else if (index == 3)
            pluginTab.refreshList();
    }

    IconMenu& owner;
    juce::TabbedComponent tabs;
    AudioSettingsTab audioTab;
    RoutingSettingsTab routingTab;
    AecSettingsTab aecTab;
    PluginSettingsTab pluginTab;
    UpdatesSettingsTab updatesTab;
};

SettingsWindow::SettingsWindow (IconMenu& owner_)
    : DocumentWindow ("Settings",
                      getDialogBackgroundColour(),
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner (owner_)
{
    panel = new SettingsPanel (owner);
    setContentOwned (panel, true);
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (480, 500, 900, 900);
    setSize (560, 600);
    centreWithSize (getWidth(), getHeight());

    restoreWindowStateFromString (getSettings().getWindowState (AppSettings::Window::settings));
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

SettingsWindow::~SettingsWindow()
{
    getSettings().setWindowState (AppSettings::Window::settings, getWindowStateAsString());
    clearContentComponent();
}

void SettingsWindow::closeButtonPressed()
{
   #if JUCE_MAC
    juce::Process::setDockIconVisible (false);
   #endif
    owner.closeSettingsWindow();
}
