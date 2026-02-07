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
    p.meterWarning  = juce::Colours::yellow;
    p.meterClip     = juce::Colours::red;
    p.trimHandle    = juce::Colours::white.withAlpha (0.3f);

    // 12 distinct track colors (hue-distributed palette)
    p.trackColor1   = juce::Colour::fromHSV (0.00f, 0.6f, 0.7f, 1.0f);  // Red
    p.trackColor2   = juce::Colour::fromHSV (0.08f, 0.6f, 0.7f, 1.0f);  // Orange
    p.trackColor3   = juce::Colour::fromHSV (0.17f, 0.6f, 0.7f, 1.0f);  // Yellow
    p.trackColor4   = juce::Colour::fromHSV (0.25f, 0.6f, 0.7f, 1.0f);  // Lime
    p.trackColor5   = juce::Colour::fromHSV (0.33f, 0.6f, 0.7f, 1.0f);  // Green
    p.trackColor6   = juce::Colour::fromHSV (0.42f, 0.6f, 0.7f, 1.0f);  // Cyan
    p.trackColor7   = juce::Colour::fromHSV (0.50f, 0.6f, 0.7f, 1.0f);  // Light blue
    p.trackColor8   = juce::Colour::fromHSV (0.58f, 0.6f, 0.7f, 1.0f);  // Blue
    p.trackColor9   = juce::Colour::fromHSV (0.67f, 0.6f, 0.7f, 1.0f);  // Purple
    p.trackColor10  = juce::Colour::fromHSV (0.75f, 0.6f, 0.7f, 1.0f);  // Magenta
    p.trackColor11  = juce::Colour::fromHSV (0.83f, 0.6f, 0.7f, 1.0f);  // Pink
    p.trackColor12  = juce::Colour::fromHSV (0.92f, 0.6f, 0.7f, 1.0f);  // Rose

    return p;
}

} // namespace waive
