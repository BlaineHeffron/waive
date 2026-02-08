#pragma once

#include <JuceHeader.h>

class EditSession;
class UndoableCommandHandler;

//==============================================================================
/** Multi-format render dialog for exporting audio (WAV, FLAC, OGG). */
class RenderDialog : public juce::Component
{
public:
    RenderDialog (EditSession& session, UndoableCommandHandler& handler);
    ~RenderDialog() override;

    void resized() override;

private:
    void updateFileExtension();
    void updateFormatOptions();
    void browseForOutputPath();
    void performRender();
    void resetControls();

    EditSession& editSession;
    UndoableCommandHandler& commandHandler;

    // Format selection
    juce::Label formatLabel;
    juce::ComboBox formatCombo;

    // Quality options
    juce::Label sampleRateLabel;
    juce::ComboBox sampleRateCombo;

    juce::Label bitDepthLabel;
    juce::ComboBox bitDepthCombo;

    juce::Label oggQualityLabel;
    juce::Slider oggQualitySlider;

    // Range selection
    juce::Label rangeLabel;
    juce::ComboBox rangeCombo;

    juce::Label startLabel;
    juce::TextEditor startEditor;

    juce::Label endLabel;
    juce::TextEditor endEditor;

    // Output options
    juce::ToggleButton normalizeToggle;
    juce::ToggleButton mixdownToggle;
    juce::ToggleButton stemsToggle;

    juce::Label outputPathLabel;
    juce::TextEditor outputPathEditor;
    juce::TextButton browseButton;

    // Render control
    juce::TextButton renderButton;
    double progressValue = 0.0;
    juce::ProgressBar progressBar;

    bool rendering = false;
};
