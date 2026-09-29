#pragma once

#include <JuceHeader.h>

inline void applySystemColourScheme (LookAndFeel_V4& laf)
{
    if (Desktop::getInstance().isDarkModeActive())
        laf.setColourScheme (LookAndFeel_V4::getDarkColourScheme());
    else
        laf.setColourScheme (LookAndFeel_V4::getLightColourScheme());
}

inline Colour getDialogBackgroundColour()
{
    return LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId);
}
