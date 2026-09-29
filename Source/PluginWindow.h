#ifndef PluginWindow_h
#define PluginWindow_h

ApplicationProperties& getAppProperties();

class PluginWindow  : public DocumentWindow
{
public:
    enum WindowFormatType
    {
        Normal = 0,
        Generic,
        Programs,
        Parameters,
        NumTypes
    };

    PluginWindow (Component* pluginEditor, juce::AudioProcessorGraph::Node::Ptr node, WindowFormatType);
    ~PluginWindow();

    static PluginWindow* getWindowFor (juce::AudioProcessorGraph::Node::Ptr node, WindowFormatType);

    static void closeCurrentlyOpenWindowsFor (const uint32 nodeId);
    static void closeAllCurrentlyOpenWindows();
    static bool containsActiveWindows();

    static void showAndFocus (PluginWindow* window);

    void moved() override;
    void closeButtonPressed() override;

private:
    juce::AudioProcessorGraph::Node::Ptr owner;
    WindowFormatType type;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

inline String toString (PluginWindow::WindowFormatType type)
{
    switch (type)
    {
        case PluginWindow::Normal:     return "Normal";
        case PluginWindow::Generic:    return "Generic";
        case PluginWindow::Programs:   return "Programs";
        case PluginWindow::Parameters: return "Parameters";
        default:                       return String();
    }
}

inline String getLastXProp (PluginWindow::WindowFormatType type)    { return "uiLastX_" + toString (type); }
inline String getLastYProp (PluginWindow::WindowFormatType type)    { return "uiLastY_" + toString (type); }
inline String getOpenProp  (PluginWindow::WindowFormatType type)    { return "uiopen_"  + toString (type); }


#endif /* PluginWindow_hpp */
