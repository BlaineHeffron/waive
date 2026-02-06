#include "WaiveColours.h"

namespace waive
{

ColourPalette makeDarkPalette()
{
    ColourPalette p;

    // Backgrounds
    p.windowBg      = juce::Colour (0xff1a1a1a);
    p.panelBg       = juce::Colour (0xff222222);
    p.surfaceBg     = juce::Colour (0xff2a2a2a);
    p.surfaceBgAlt  = juce::Colour (0xff252525);
    p.insetBg       = juce::Colour (0xff1f1f1f);

    // Borders
    p.border        = juce::Colour (0xff3a3a3a);
    p.borderSubtle  = juce::Colour (0xff303030);
    p.borderFocused = juce::Colour (0xff5588bb);

    // Primary
    p.primary       = juce::Colour (0xff4488cc);
    p.primaryHover  = juce::Colour (0xff5599dd);
    p.primaryPressed = juce::Colour (0xff3377bb);

    // Accent
    p.accent        = juce::Colour (0xfff0b429);
    p.accentMuted   = juce::Colour (0xffc09020);

    // Selection
    p.selection     = juce::Colour (0xff4477aa);
    p.selectionBorder = juce::Colours::white;
    p.toolPreview   = juce::Colour (0xfff0b429);

    // Status
    p.success       = juce::Colour (0xff4caf50);
    p.warning       = juce::Colour (0xffff9800);
    p.danger        = juce::Colour (0xffef5350);
    p.record        = juce::Colour (0xffcc3333);

    // Text
    p.textPrimary   = juce::Colour (0xffe0e0e0);
    p.textSecondary = juce::Colour (0xffaaaaaa);
    p.textMuted     = juce::Colour (0xff777777);
    p.textOnPrimary = juce::Colours::white;
    p.textOnDanger  = juce::Colours::white;

    // DAW-specific
    p.clipDefault   = juce::Colour (0xff3a5a3a);
    p.clipSelected  = juce::Colour (0xff4477aa);
    p.waveform      = juce::Colours::white.withAlpha (0.8f);
    p.playhead      = juce::Colours::white;
    p.automationCurve = juce::Colour (0xff77b7ff);
    p.automationPoint = juce::Colours::white.withAlpha (0.9f);
    p.gridMajor     = juce::Colours::lightgrey.withAlpha (0.2f);
    p.gridMinor     = juce::Colours::grey.withAlpha (0.12f);
    p.meterNormal   = juce::Colours::limegreen;
    p.meterClip     = juce::Colours::red;
    p.trimHandle    = juce::Colours::white.withAlpha (0.3f);

    return p;
}

} // namespace waive
