#pragma once

#include <JuceHeader.h>
#include <optional>
#include <thread>
#include <tracktion_engine/tracktion_engine.h>

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

    int getSelectedFormatForTesting() const;
    void selectFormatForTesting (int formatId);
    int getSelectedRangeForTesting() const;
    void selectRangeForTesting (int rangeId);
    bool isBitDepthVisibleForTesting() const;
    bool isOggQualityVisibleForTesting() const;
    bool isCustomRangeVisibleForTesting() const;
    void setCustomRangeForTesting (const juce::String& startSeconds, const juce::String& endSeconds);
    void setLoopRangeForTesting (double startSeconds, double endSeconds, bool enabled);
    bool triggerRenderForTesting();
    void setStemsModeForTesting (bool shouldRenderStems);
    juce::String getOutputPathForTesting() const;
    juce::String getOutputLabelTextForTesting() const;

private:
    bool prepareRenderData();
    void updateFileExtension();
    void updateFormatOptions();
    void updateOutputMode();
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

    struct RenderData
    {
        int formatId = 1;
        double sampleRate = 48000.0;
        int bitDepth = 24;
        double oggQuality = 0.6;
        double rangeStartSeconds = 0.0;
        double rangeEndSeconds = 0.0;
        juce::BigInteger tracksMask;
        bool doStems = false;
        juce::File outputFile;
        bool normalize = false;
        double normalizeLevel = -0.1;
        juce::ValueTree editState;
        juce::File editFile;
        tracktion::engine::Engine* enginePtr = nullptr;
    };

    std::optional<RenderData> pendingRenderData;

    std::thread renderThread;
    bool rendering = false;
};
