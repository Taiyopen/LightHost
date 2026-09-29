#include "SettingsWindow.h"
#include "../IconMenu.hpp"
#include "../AppSettings.h"
#include "../AppTheme.h"
#include "../audio/LoopbackDevices.h"

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

        void applyNrToggle()
        {
            getSettings().setNrEnabled (nrToggle.getToggleState());
            owner.getEngine().rebuildGraph();
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
          aecTab (ownerIn),
          pluginTab (ownerIn),
          updatesTab (ownerIn.getUpdater())
    {
        tabs.addTab ("Audio", juce::Colours::transparentBlack, &audioTab, false);
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
            aecTab.refreshFromOwner();
        else if (index == 2)
            pluginTab.refreshList();
    }

    IconMenu& owner;
    juce::TabbedComponent tabs;
    AudioSettingsTab audioTab;
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
    setResizeLimits (480, 420, 900, 900);
    setSize (560, 520);
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
