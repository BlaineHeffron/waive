#pragma once

#include <JuceHeader.h>

namespace waive
{

struct ColourPalette
{
    // Backgrounds
    juce::Colour windowBg;
    juce::Colour panelBg;
    juce::Colour surfaceBg;
    juce::Colour surfaceBgAlt;
    juce::Colour insetBg;

    // Borders
    juce::Colour border;
    juce::Colour borderSubtle;
    juce::Colour borderFocused;

    // Primary
    juce::Colour primary;
    juce::Colour primaryHover;
    juce::Colour primaryPressed;

    // Accent
    juce::Colour accent;
    juce::Colour accentMuted;

    // Selection
    juce::Colour selection;
    juce::Colour selectionBorder;
    juce::Colour toolPreview;

    // Status
    juce::Colour success;
    juce::Colour warning;
    juce::Colour danger;
    juce::Colour record;

    // Text
    juce::Colour textPrimary;
    juce::Colour textSecondary;
    juce::Colour textMuted;
    juce::Colour textOnPrimary;
    juce::Colour textOnDanger;

    // DAW-specific
    juce::Colour clipDefault;
    juce::Colour clipSelected;
    juce::Colour waveform;
    juce::Colour playhead;
    juce::Colour automationCurve;
    juce::Colour automationPoint;
    juce::Colour gridMajor;
    juce::Colour gridMinor;
    juce::Colour meterNormal;
    juce::Colour meterWarning;
    juce::Colour meterClip;
    juce::Colour trimHandle;

    // Track colors (12-color palette)
    juce::Colour trackColor1;
    juce::Colour trackColor2;
    juce::Colour trackColor3;
    juce::Colour trackColor4;
    juce::Colour trackColor5;
    juce::Colour trackColor6;
    juce::Colour trackColor7;
    juce::Colour trackColor8;
    juce::Colour trackColor9;
    juce::Colour trackColor10;
    juce::Colour trackColor11;
    juce::Colour trackColor12;
};

ColourPalette makeDarkPalette();

} // namespace waive
