#include <JuceHeader.h>
#include "PluginWindow.h"
#include "AppTheme.h"

class PluginWindow;
static Array<PluginWindow*> activePluginWindows;

static bool isBoundsOnScreen (const Rectangle<int>& bounds)
{
    for (auto& display : Desktop::getInstance().getDisplays().displays)
        if (display.totalArea.contains (bounds))
            return true;

    return false;
}

PluginWindow::PluginWindow (Component* const pluginEditor,
                            AudioProcessorGraph::Node::Ptr node,
                            WindowFormatType t)
    : DocumentWindow (pluginEditor->getName(),
                      getDialogBackgroundColour(),
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner (std::move (node)),
      type (t)
{
    setUsingNativeTitleBar (true);
    setContentOwned (pluginEditor, true);

    if (auto* editor = dynamic_cast<AudioProcessorEditor*> (pluginEditor))
        setResizable (editor->isResizable(), false);

    const int width  = jmax (400, pluginEditor->getWidth());
    const int height = jmax (300, pluginEditor->getHeight());
    setSize (width, height);

    const int savedX = owner->properties.getWithDefault (getLastXProp (type), -1);
    const int savedY = owner->properties.getWithDefault (getLastYProp (type), -1);

    if (savedX >= 0 && savedY >= 0)
    {
        setTopLeftPosition (savedX, savedY);

        if (! isBoundsOnScreen (getBounds()))
            centreWithSize (getWidth(), getHeight());
    }
    else
    {
        centreWithSize (getWidth(), getHeight());
    }

    owner->properties.set (getOpenProp (type), true);
    activePluginWindows.add (this);
}

void PluginWindow::showAndFocus (PluginWindow* window)
{
    if (window == nullptr)
        return;

    window->setVisible (true);
    window->centreWithSize (window->getWidth(), window->getHeight());
    window->toFront (true);
    window->setAlwaysOnTop (true);
    window->setAlwaysOnTop (false);
}

void PluginWindow::closeCurrentlyOpenWindowsFor (const uint32 nodeId)
{
    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked (i)->owner->nodeID.uid == nodeId)
            delete activePluginWindows.getUnchecked (i);
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    while (activePluginWindows.size() > 0)
        delete activePluginWindows.getUnchecked (activePluginWindows.size() - 1);
}

bool PluginWindow::containsActiveWindows()
{
    return activePluginWindows.size() > 0;
}

class ProcessorProgramPropertyComp : public PropertyComponent,
                                     private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
        : PropertyComponent (name), owner (p), index (index_)
    {
        owner.addListener (this);
    }

    ~ProcessorProgramPropertyComp() override
    {
        owner.removeListener (this);
    }

    void refresh() override {}

    void audioProcessorParameterChanged (AudioProcessor*, int, float) override {}
    void audioProcessorChanged (AudioProcessor*, const ChangeDetails&) override {}

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor : public AudioProcessorEditor
{
public:
    explicit ProgramAudioProcessorEditor (AudioProcessor* const p)
        : AudioProcessorEditor (p)
    {
        jassert (p != nullptr);
        setOpaque (true);
        addAndMakeVisible (panel);

        Array<PropertyComponent*> programs;
        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        for (int i = 0; i < numPrograms; ++i)
        {
            String name (p->getProgramName (i).trim());
            if (name.isEmpty())
                name = "Unnamed";

            auto* pc = new ProcessorProgramPropertyComp (name, *p, i);
            programs.add (pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties (programs);
        setSize (400, jlimit (25, 400, totalHeight));
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::grey);
    }

    void resized() override
    {
        panel.setBounds (getLocalBounds());
    }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgramAudioProcessorEditor)
};

PluginWindow* PluginWindow::getWindowFor (AudioProcessorGraph::Node::Ptr node, WindowFormatType type)
{
    if (node == nullptr)
        return nullptr;

    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked (i)->owner == node
            && activePluginWindows.getUnchecked (i)->type == type)
            return activePluginWindows.getUnchecked (i);

    AudioProcessor* processor = node->getProcessor();
    if (processor == nullptr)
        return nullptr;

    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        if (processor->hasEditor())
            ui = processor->createEditorIfNeeded();

        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui = new GenericAudioProcessorEditor (processor);
        else if (type == Programs)
            ui = new ProgramAudioProcessorEditor (processor);
    }

    if (ui != nullptr)
    {
        if (auto* plugin = dynamic_cast<AudioPluginInstance*> (processor))
            ui->setName (plugin->getName());

        return new PluginWindow (ui, node, type);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    activePluginWindows.removeFirstMatchingValue (this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set (getLastXProp (type), getX());
    owner->properties.set (getLastYProp (type), getY());
}

void PluginWindow::closeButtonPressed()
{
    owner->properties.set (getOpenProp (type), false);
    delete this;
}
