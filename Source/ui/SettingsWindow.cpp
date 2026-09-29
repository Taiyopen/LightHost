#include "SettingsWindow.h"
#include "../IconMenu.hpp"
#include "../AppTheme.h"
#include "../audio/LoopbackDevices.h"
#include "../dsp/AecProcessor.h"

namespace
{
    class AudioSettingsTab : public juce::Component,
                             private juce::ChangeListener
    {
    public:
        explicit AudioSettingsTab (IconMenu& owner_)
            : owner (owner_),
              audioSettings (owner.getDeviceManager(), 0, 256, 0, 256, false, false, true, true)
        {
            addAndMakeVisible (audioSettings);
            owner.getDeviceManager().addChangeListener (this);
        }

        ~AudioSettingsTab() override
        {
            owner.getDeviceManager().removeChangeListener (this);
        }

        void resized() override
        {
            audioSettings.setBounds (getLocalBounds());
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            owner.saveAudioDeviceStateAndReload();
        }

        IconMenu& owner;
        juce::AudioDeviceSelectorComponent audioSettings;
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
            nrToggle.onClick = [this] { applyNrToggle(); };
            addAndMakeVisible (nrToggle);

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

                owner.setAecStrengthPercent (static_cast<float> (strengthSlider.getValue()));

                if (auto* aec = owner.getAecProcessor())
                    aec->setStrength (static_cast<float> (strengthSlider.getValue()));
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

                owner.setReferenceGainDb (static_cast<float> (gainSlider.getValue()));
                owner.reloadActivePlugins();
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

            aecToggle.setToggleState (owner.isAecEnabled(), juce::dontSendNotification);
            nrToggle.setToggleState (owner.isNrEnabled(), juce::dontSendNotification);
            strengthSlider.setValue (owner.getAecStrengthPercent(), juce::dontSendNotification);
            gainSlider.setValue (owner.getReferenceGainDb(), juce::dontSendNotification);

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

            if (! owner.shouldUseSystemLoopbackReference())
            {
                referenceCombo.setSelectedId (1, juce::dontSendNotification);
            }
            else if (owner.getReferenceDeviceId().isEmpty())
            {
                referenceCombo.setSelectedId (3, juce::dontSendNotification);
            }
            else
            {
                const juce::String selectedId = owner.getReferenceDeviceId();
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
                owner.setUseSystemLoopbackReference (false);
            }
            else if (selectedId == 2)
            {
                owner.setUseSystemLoopbackReference (true);
                owner.setReferenceDeviceId ({});
            }
            else if (selectedId == 3)
            {
                owner.setUseSystemLoopbackReference (true);
                owner.setReferenceDeviceId ({});
            }
            else if (selectedId >= 4)
            {
                const auto outputDevices = enumerateLoopbackOutputDevices();
                const int listIndex = selectedId - 4;

                if (juce::isPositiveAndBelow (listIndex, outputDevices.size()))
                {
                    owner.setUseSystemLoopbackReference (true);
                    owner.setReferenceDeviceId (outputDevices.getReference (listIndex).id);
                }
            }

            owner.reloadActivePlugins();
        }
       #endif

        void applyAecToggle()
        {
            owner.setAecEnabled (aecToggle.getToggleState());
            owner.reloadActivePlugins();
            refreshFromOwner();
        }

        void applyNrToggle()
        {
            owner.setNrEnabled (nrToggle.getToggleState());
            owner.reloadActivePlugins();
        }

        void updateControlStates()
        {
            const bool aecOn = owner.isAecEnabled();
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

        IconMenu& owner;
        juce::ToggleButton aecToggle, nrToggle;
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
            editButton.onClick = [this] { performOnSelection (&IconMenu::openPluginEditorForSortedIndex); };
            addAndMakeVisible (editButton);

            bypassButton.setButtonText ("Bypass");
            bypassButton.onClick = [this] { performOnSelection (&IconMenu::togglePluginBypass); refreshList(); };
            addAndMakeVisible (bypassButton);

            moveUpButton.setButtonText ("Move Up");
            moveUpButton.onClick = [this]
            {
                const int row = listBox.getSelectedRow();

                if (row <= 0)
                    return;

                owner.movePluginUp (row);
                refreshList (row - 1);
            };
            addAndMakeVisible (moveUpButton);

            moveDownButton.setButtonText ("Move Down");
            moveDownButton.onClick = [this]
            {
                const int row = listBox.getSelectedRow();

                if (row < 0 || row >= static_cast<int> (plugins.size()) - 1)
                    return;

                owner.movePluginDown (row);
                refreshList (row + 1);
            };
            addAndMakeVisible (moveDownButton);

            deleteButton.setButtonText ("Delete");
            deleteButton.onClick = [this]
            {
                const int row = listBox.getSelectedRow();

                if (row < 0)
                    return;

                owner.deletePluginAtSortedIndex (row);
                refreshList();
            };
            addAndMakeVisible (deleteButton);

            addButton.setButtonText ("Add Plugin...");
            addButton.onClick = [this] { showAddPluginMenu(); };
            addAndMakeVisible (addButton);

            refreshList();
        }

        void refreshList (int selectRow = -1)
        {
            plugins = owner.getTimeSortedPluginList();
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
            const bool bypassed = owner.isPluginBypassed (plugin);
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
                owner.openPluginEditorForSortedIndex (row);
        }

    private:
        using PluginAction = void (IconMenu::*) (int);

        void performOnSelection (PluginAction action)
        {
            const int row = listBox.getSelectedRow();

            if (juce::isPositiveAndBelow (row, static_cast<int> (plugins.size())))
                (owner.*action) (row);
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
        }

        void showAddPluginMenu()
        {
            juce::PopupMenu menu;
            owner.getKnownPluginList().addToMenu (menu, owner.getPluginSortMethod());

            menu.showMenuAsync (juce::PopupMenu::Options(),
                                [this] (int result)
                                {
                                    if (result <= 0)
                                        return;

                                    const int index = owner.getKnownPluginList().getIndexChosenByMenu (result);

                                    if (index >= 0)
                                    {
                                        owner.addActivePlugin (*owner.getKnownPluginList().getType (index));
                                        refreshList();
                                    }
                                });
        }

        IconMenu& owner;
        std::vector<juce::PluginDescription> plugins;
        juce::ListBox listBox;
        juce::TextButton editButton, bypassButton, moveUpButton, moveDownButton, deleteButton, addButton;
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
          aecTab (ownerIn),
          pluginTab (ownerIn)
    {
        tabs.addTab ("Audio", juce::Colours::transparentBlack, &audioTab, false);
        tabs.addTab ("AEC", juce::Colours::transparentBlack, &aecTab, false);
        tabs.addTab ("Plugins", juce::Colours::transparentBlack, &pluginTab, false);
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
            aecTab.refreshFromOwner();
        else if (index == 2)
            pluginTab.refreshList();
    }

    IconMenu& owner;
    juce::TabbedComponent tabs;
    AudioSettingsTab audioTab;
    AecSettingsTab aecTab;
    PluginSettingsTab pluginTab;
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
    setResizeLimits (480, 420, 900, 900);
    setSize (560, 520);
    centreWithSize (getWidth(), getHeight());

    restoreWindowStateFromString (getAppProperties().getUserSettings()->getValue ("settingsWindowPos"));
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

SettingsWindow::~SettingsWindow()
{
    getAppProperties().getUserSettings()->setValue ("settingsWindowPos", getWindowStateAsString());
    clearContentComponent();
}

void SettingsWindow::closeButtonPressed()
{
   #if JUCE_MAC
    juce::Process::setDockIconVisible (false);
   #endif
    owner.closeSettingsWindow();
}
