#include "TimelineComponent.h"
#include "EditSession.h"
#include "TrackLaneComponent.h"
#include "PlayheadComponent.h"
#include "TimeRulerComponent.h"
#include "SelectionManager.h"

//==============================================================================
TimelineComponent::TimelineComponent (EditSession& session)
    : editSession (session)
{
    selectionManager = std::make_unique<SelectionManager>();

    ruler = std::make_unique<TimeRulerComponent> (editSession.getEdit(), *this);
    addAndMakeVisible (ruler.get());

    trackViewport.setViewedComponent (&trackContainer, false);
    trackViewport.setScrollBarsShown (true, true);
    addAndMakeVisible (trackViewport);

    playhead = std::make_unique<PlayheadComponent> (editSession.getEdit(), *this);
    addAndMakeVisible (playhead.get());

    rebuildTracks();

    // 5Hz for structural changes
    startTimerHz (5);
}

TimelineComponent::~TimelineComponent()
{
    stopTimer();
}

void TimelineComponent::resized()
{
    auto bounds = getLocalBounds();

    ruler->setBounds (bounds.removeFromTop (rulerHeight));
    trackViewport.setBounds (bounds);

    // Playhead overlay covers entire component
    playhead->setBounds (getLocalBounds());

    // Size track container
    int totalHeight = (int) trackLanes.size() * trackLaneHeight;
    int contentWidth = juce::jmax (bounds.getWidth(), (int) (pixelsPerSecond * 60.0));
    trackContainer.setSize (contentWidth, juce::jmax (totalHeight, bounds.getHeight()));

    int y = 0;
    for (auto& lane : trackLanes)
    {
        lane->setBounds (0, y, contentWidth, trackLaneHeight);
        y += trackLaneHeight;
    }
}

void TimelineComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}

void TimelineComponent::mouseWheelMove (const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        // Zoom
        double zoomFactor = 1.0 + wheel.deltaY * 0.3;
        double mouseTime = xToTime (e.x);

        pixelsPerSecond = juce::jlimit (10.0, 1000.0, pixelsPerSecond * zoomFactor);

        // Adjust scroll to keep mouse position stable
        scrollOffsetSeconds = mouseTime - e.x / pixelsPerSecond;
        scrollOffsetSeconds = juce::jmax (0.0, scrollOffsetSeconds);

        resized();
        repaint();
    }
    else
    {
        // Horizontal scroll
        scrollOffsetSeconds -= wheel.deltaY * 2.0;
        scrollOffsetSeconds = juce::jmax (0.0, scrollOffsetSeconds);
        resized();
        repaint();
    }
}

//==============================================================================
bool TimelineComponent::isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description.toString() == "LibraryFile";
}

void TimelineComponent::itemDropped (const juce::DragAndDropTarget::SourceDetails& details)
{
    if (auto* fileTree = dynamic_cast<juce::FileTreeComponent*> (details.sourceComponent.get()))
    {
        auto file = fileTree->getSelectedFile();
        if (! file.existsAsFile())
            return;

        auto localPos = details.localPosition;
        double dropTime = xToTime (localPos.x);
        int trackIdx = trackIndexAtY (localPos.y - rulerHeight);

        auto& edit = editSession.getEdit();
        auto tracks = te::getAudioTracks (edit);

        if (trackIdx < 0 || trackIdx >= tracks.size())
            trackIdx = 0;

        if (tracks.isEmpty())
            return;

        auto* track = tracks[trackIdx];
        te::AudioFile audioFile (edit.engine, file);
        auto duration = audioFile.getLength();

        editSession.performEdit ("Insert Audio Clip", [&] (te::Edit&)
        {
            track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                                   { { te::TimePosition::fromSeconds (dropTime),
                                       te::TimePosition::fromSeconds (dropTime + duration) },
                                     te::TimeDuration() },
                                   false);
        });
    }
}

//==============================================================================
int TimelineComponent::timeToX (double seconds) const
{
    return (int) ((seconds - scrollOffsetSeconds) * pixelsPerSecond);
}

double TimelineComponent::xToTime (int x) const
{
    return scrollOffsetSeconds + x / pixelsPerSecond;
}

int TimelineComponent::trackIndexAtY (int y) const
{
    if (y < 0) return 0;
    return y / trackLaneHeight;
}

//==============================================================================
void TimelineComponent::rebuildTracks()
{
    trackLanes.clear();
    trackContainer.removeAllChildren();

    auto tracks = te::getAudioTracks (editSession.getEdit());

    for (auto* track : tracks)
    {
        auto lane = std::make_unique<TrackLaneComponent> (*track, *this);
        trackContainer.addAndMakeVisible (lane.get());
        trackLanes.push_back (std::move (lane));
    }

    lastTrackCount = tracks.size();
    resized();
}

void TimelineComponent::timerCallback()
{
    auto tracks = te::getAudioTracks (editSession.getEdit());
    if (tracks.size() != lastTrackCount)
        rebuildTracks();
}
