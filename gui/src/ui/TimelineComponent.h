#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class EditSession;
class TrackLaneComponent;
class PlayheadComponent;
class TimeRulerComponent;
class SelectionManager;

//==============================================================================
/** Main timeline container — manages zoom, scroll, track lanes, and playhead. */
class TimelineComponent : public juce::Component,
                          public juce::DragAndDropTarget,
                          private juce::Timer
{
public:
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

    double getPixelsPerSecond() const   { return pixelsPerSecond; }
    double getScrollOffsetSeconds() const { return scrollOffsetSeconds; }

    static constexpr int rulerHeight = 30;
    static constexpr int trackHeaderWidth = 120;
    static constexpr int trackLaneHeight = 80;

    SelectionManager& getSelectionManager()  { return *selectionManager; }
    EditSession& getEditSession()            { return editSession; }

    void rebuildTracks();

private:
    void timerCallback() override;

    EditSession& editSession;

    double pixelsPerSecond = 100.0;
    double scrollOffsetSeconds = 0.0;

    std::unique_ptr<SelectionManager> selectionManager;
    std::unique_ptr<TimeRulerComponent> ruler;
    std::unique_ptr<PlayheadComponent> playhead;

    juce::Viewport trackViewport;
    juce::Component trackContainer;
    std::vector<std::unique_ptr<TrackLaneComponent>> trackLanes;

    int lastTrackCount = 0;
};
