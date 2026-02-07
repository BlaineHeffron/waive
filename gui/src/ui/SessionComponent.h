#pragma once

#include <JuceHeader.h>
#include "TimelineComponent.h"
#include "ToolDiff.h"

class EditSession;
class UndoableCommandHandler;
class MixerComponent;
class ToolSidebarComponent;
class ProjectManager;

namespace waive { class ToolRegistry; class ModelManager; class JobQueue; }

//==============================================================================
/** Transport toolbar + timeline + mixer. */
class SessionComponent : public juce::Component,
                         private juce::Timer,
                         private SelectionManager::Listener
{
public:
    SessionComponent (EditSession& session, UndoableCommandHandler& handler,
                      waive::ToolRegistry* toolReg = nullptr,
                      waive::ModelManager* modelMgr = nullptr,
                      waive::JobQueue* jobQueue = nullptr,
                      ProjectManager* projectMgr = nullptr);
    ~SessionComponent() override;

    void resized() override;

    TimelineComponent& getTimeline();
    ToolSidebarComponent* getToolSidebar();

    void toggleToolSidebar();
    void play();
    void stop();
    void record();
    void goToStart();

    // Test helpers for no-user UI coverage.
    void setTempoForTesting (double bpm);
    void setTimeSignatureForTesting (int numerator, int denominator);
    void insertTempoMarkerAtPlayheadForTesting (double bpm);
    void insertTimeSigMarkerAtPlayheadForTesting (int numerator, int denominator);
    void setSnapForTesting (bool enabled, TimelineComponent::SnapResolution resolution);
    double snapTimeForTesting (double seconds) const;
    void setLoopEnabledForTesting (bool enabled);
    void setLoopRangeForTesting (double loopInSeconds, double loopOutSeconds);
    void setPunchEnabledForTesting (bool enabled);
    juce::Array<int> getToolPreviewTracksForTesting() const;
    juce::String getTransportTooltipForTesting (const juce::String& controlName);
    juce::String getSelectionStatusTextForTesting() const;

    void applyToolPreviewDiff (const juce::Array<waive::ToolDiffEntry>& changes);
    void clearToolPreview();

private:
    void timerCallback() override;
    void selectionChanged() override;
    void runTransportAction (const juce::String& action);
    void applyTempo (double bpm, bool coalesce);
    void applyTimeSignature (int numerator, int denominator);
    int getSelectedTimeSigNumerator() const;
    int getSelectedTimeSigDenominator() const;

    EditSession& editSession;
    UndoableCommandHandler& commandHandler;
    bool suppressControlCallbacks = false;

    // Transport toolbar
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton recordButton;
    juce::TextButton addTrackButton;
    juce::ToggleButton loopButton { "Loop" };
    juce::ToggleButton punchButton { "Punch" };
    juce::TextButton setLoopInButton { "Set In" };
    juce::TextButton setLoopOutButton { "Set Out" };
    juce::Label tempoLabel { {}, "Tempo" };
    juce::Slider tempoSlider;
    juce::Label timeSigLabel { {}, "Sig" };
    juce::ComboBox timeSigNumeratorBox;
    juce::ComboBox timeSigDenominatorBox;
    juce::TextButton addTempoMarkerButton { "+Tempo" };
    juce::TextButton addTimeSigMarkerButton { "+Sig" };
    juce::ToggleButton snapToggle { "Snap" };
    juce::ComboBox snapResolutionBox;
    juce::ToggleButton barsBeatsToggle { "Bars" };
    juce::ToggleButton clickToggle { "Click" };
    juce::Label positionLabel;
    juce::Label selectionStatusLabel;

    // Main areas
    std::unique_ptr<TimelineComponent> timeline;
    std::unique_ptr<MixerComponent> mixer;

    juce::StretchableLayoutManager layoutManager;
    std::unique_ptr<juce::StretchableLayoutResizerBar> resizerBar;

    // Tool sidebar
    std::unique_ptr<ToolSidebarComponent> toolSidebar;
    std::unique_ptr<juce::StretchableLayoutResizerBar> sidebarResizer;
    juce::StretchableLayoutManager horizontalLayout;
    bool sidebarVisible = true;
    static constexpr int defaultSidebarWidth = 280;

    // Cache for change detection in timerCallback
    double lastTempo = -1.0;
    int lastNumerator = -1;
    int lastDenominator = -1;
    bool lastLoopState = false;
    bool lastPunchState = false;
    bool lastClickState = false;
    bool lastSnapState = false;
    bool lastBarsBeatsState = false;
    int lastSnapResolution = -1;
};
