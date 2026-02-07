#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include <unordered_set>
#include "EditSession.h"
#include "SelectionManager.h"

namespace te = tracktion;

class EditSession;
class TrackLaneComponent;
class PlayheadComponent;
class TimeRulerComponent;

//==============================================================================
/** Main timeline container — manages zoom, scroll, track lanes, and playhead. */
class TimelineComponent : public juce::Component,
                          public juce::DragAndDropTarget,
                          private SelectionManager::Listener,
                          private EditSession::Listener,
                          private juce::Timer
{
public:
    enum class SnapResolution
    {
        bar = 0,
        beat,
        halfBeat,
        quarterBeat
    };

    TimelineComponent (EditSession& session);
    ~TimelineComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // DragAndDropTarget
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details) override;

    // Coordinate conversion
    int timeToX (double seconds) const;
    double xToTime (int x) const;
    int trackIndexAtY (int y) const;
    double snapTimeToGrid (double seconds) const;
    void getGridLineTimes (double startSeconds, double endSeconds,
                           juce::Array<double>& majorLines,
                           juce::Array<double>& minorLines) const;

    double getPixelsPerSecond() const   { return pixelsPerSecond; }
    double getScrollOffsetSeconds() const { return scrollOffsetSeconds; }
    bool isSnapEnabled() const          { return snapEnabled; }
    SnapResolution getSnapResolution() const { return snapResolution; }
    bool getShowBarsBeatsRuler() const  { return showBarsBeatsRuler; }

    void setSnapEnabled (bool enabled);
    void setSnapResolution (SnapResolution resolution);
    void setShowBarsBeatsRuler (bool shouldShow);

    static constexpr int rulerHeight = 30;
    static constexpr int trackHeaderWidth = 120;
    static constexpr int trackLaneHeight = 108;

    SelectionManager& getSelectionManager()  { return *selectionManager; }
    EditSession& getEditSession()            { return editSession; }

    void rebuildTracks();
    void deleteSelectedClips();
    void duplicateSelectedClips();
    void splitSelectedClipsAtPlayhead();

    // Test helpers for deterministic UI coverage.
    bool addAutomationPointForTrackParam (int trackIndex,
                                          const juce::String& paramID,
                                          double timeSeconds,
                                          float normalisedValue);
    bool moveAutomationPointForTrackParam (int trackIndex,
                                           const juce::String& paramID,
                                           int pointIndex,
                                           double timeSeconds,
                                           float normalisedValue);
    void selectClipsByIDForPreview (const juce::Array<te::EditItemID>& clipIDs);
    void clearPreviewSelection();

private:
    void selectionChanged() override;
    void editAboutToChange() override;
    void editChanged() override;
    void timerCallback() override;

    EditSession& editSession;

    double pixelsPerSecond = 100.0;
    double scrollOffsetSeconds = 0.0;
    bool snapEnabled = true;
    SnapResolution snapResolution = SnapResolution::beat;
    bool showBarsBeatsRuler = true;

    std::unique_ptr<SelectionManager> selectionManager;
    std::unique_ptr<TimeRulerComponent> ruler;
    std::unique_ptr<PlayheadComponent> playhead;

    juce::Viewport trackViewport;
    juce::Component trackContainer;
    std::vector<std::unique_ptr<TrackLaneComponent>> trackLanes;

    int lastTrackCount = 0;

public:
    std::unordered_set<te::EditItemID> previewClipIDs;
};
