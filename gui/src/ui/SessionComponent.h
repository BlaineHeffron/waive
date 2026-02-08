#pragma once

#include <JuceHeader.h>
#include "TimelineComponent.h"
#include "ToolDiff.h"

class EditSession;
class UndoableCommandHandler;
class MixerComponent;
class ToolSidebarComponent;
class ProjectManager;
class PianoRollComponent;

namespace te = tracktion;

namespace waive { class ToolRegistry; class ModelManager; class JobQueue;
                  class AiAgent; class AiSettings; class ChatPanelComponent; }

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
                      ProjectManager* projectMgr = nullptr,
                      waive::AiAgent* aiAgent = nullptr,
                      waive::AiSettings* aiSettings = nullptr);
    ~SessionComponent() override;

    void resized() override;

    TimelineComponent& getTimeline();
    ToolSidebarComponent* getToolSidebar();

    void toggleToolSidebar();
    void toggleChatPanel();
    void openPianoRoll (te::MidiClip& clip);
    void closePianoRoll();
    waive::ChatPanelComponent* getChatPanelForTesting();
    MixerComponent& getMixerForTesting();
    void play();
    void stop();
    void record();
    void goToStart();
    void recordFromMic();

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
    enum class PanelLayoutMode
    {
        timelineMixer,
        timelinePianoMixer
    };

    void timerCallback() override;
    void selectionChanged() override;
    void runTransportAction (const juce::String& action);
    void stopRecording();
    void refreshMicInputDevices();
    te::WaveInputDevice* getSelectedMicInputDevice() const;
    void requestMicrophoneAccess (std::function<void (bool)> callback);
    bool ensureMicInputReady (juce::String& errorMessage);
    juce::String getMicAccessHelpText() const;
    void applyTempo (double bpm, bool coalesce);
    void applyTimeSignature (int numerator, int denominator);
    int getSelectedTimeSigNumerator() const;
    int getSelectedTimeSigDenominator() const;
    void applyPanelLayoutMode (PanelLayoutMode mode);

    EditSession& editSession;
    UndoableCommandHandler& commandHandler;
    bool suppressControlCallbacks = false;

    // Transport toolbar
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton recordButton;
    juce::TextButton stopRecordButton;
    juce::TextButton recordFromMicButton { "Mic Rec" };
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
    juce::Label recordStatusLabel;
    juce::Label selectionStatusLabel;
    juce::Label micInputLabel { {}, "Input" };
    juce::ComboBox micInputCombo;

    // Main areas
    std::unique_ptr<TimelineComponent> timeline;
    std::unique_ptr<MixerComponent> mixer;

    juce::StretchableLayoutManager layoutManager;
    std::unique_ptr<juce::StretchableLayoutResizerBar> resizerBar;

    // Tool sidebar
    std::unique_ptr<ToolSidebarComponent> toolSidebar;
    std::unique_ptr<juce::StretchableLayoutResizerBar> sidebarResizer;
    juce::StretchableLayoutManager horizontalLayout;
    juce::Component contentArea;
    bool sidebarVisible = true;
    static constexpr int defaultSidebarWidth = 280;

    // Chat panel
    std::unique_ptr<waive::ChatPanelComponent> chatPanel;
    juce::TextButton chatClipButton { "Clip Side" };
    juce::TextButton chatCloseButton { "X" };
    bool chatPanelVisible = false;
    bool chatPanelClippedToSide = false;
    int chatPanelDockedWidth = 360;
    juce::Rectangle<int> chatFloatingBounds;

    // Piano roll panel
    std::unique_ptr<PianoRollComponent> pianoRollPanel;
    std::unique_ptr<juce::StretchableLayoutResizerBar> pianoRollResizerBar;
    bool pianoRollVisible = false;
    juce::TextButton closePianoRollButton { "X" };
    PanelLayoutMode panelLayoutMode = PanelLayoutMode::timelineMixer;
    bool panelLayoutInitialised = false;

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
    int micInputRefreshTicks = 0;
    bool micPermissionRequestInFlight = false;
};
