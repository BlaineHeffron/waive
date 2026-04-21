#include "TimelineComponent.h"
#include "EditSession.h"
#include "TrackLaneComponent.h"
#include "PlayheadComponent.h"
#include "TimeRulerComponent.h"
#include "ClipEditActions.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"
#include "UiMessageHelpers.h"

#include <cmath>

namespace
{
double normalisedToParameterValue (float normalised, const juce::Range<float>& valueRange)
{
    return juce::jmap (juce::jlimit (0.0f, 1.0f, normalised),
                       0.0f, 1.0f,
                       valueRange.getStart(), valueRange.getEnd());
}
}

//==============================================================================
TimelineComponent::TimelineComponent (EditSession& session)
    : editSession (session)
{
    selectionManager = std::make_unique<SelectionManager>();
    selectionManager->setEdit (&editSession.getEdit());
    selectionManager->addListener (this);
    editSession.addListener (this);

    ruler = std::make_unique<TimeRulerComponent> (editSession, *this);
    addAndMakeVisible (ruler.get());

    trackViewport.setViewedComponent (&trackContainer, false);
    trackViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (trackViewport);

    playhead = std::make_unique<PlayheadComponent> (editSession, *this);
    addAndMakeVisible (playhead.get());

    // Horizontal scrollbar
    horizontalScrollbar.setOrientation (false);
    horizontalScrollbar.addListener (this);
    addAndMakeVisible (horizontalScrollbar);

    rebuildTracks();

    // 5Hz for structural changes
    startTimerHz (5);

    setTitle ("Timeline");
    setDescription ("Timeline view - shows tracks, clips, playhead, and timeline ruler");
    setWantsKeyboardFocus (true);
}

TimelineComponent::~TimelineComponent()
{
    stopTimer();
    horizontalScrollbar.removeListener (this);
    editSession.removeListener (this);
    selectionManager->removeListener (this);
}

void TimelineComponent::resized()
{
    auto bounds = getLocalBounds();

    ruler->setBounds (bounds.removeFromTop (waive::Spacing::rulerHeight));

    // Reserve space for horizontal scrollbar
    auto scrollbarArea = bounds.removeFromBottom (waive::Spacing::scrollbarThickness);
    horizontalScrollbar.setBounds (scrollbarArea);

    trackViewport.setBounds (bounds);

    // Playhead overlay covers entire component
    playhead->setBounds (getLocalBounds());

    // Size track container
    int totalHeight = (int) trackLanes.size() * trackLaneHeight;
    int contentWidth = juce::jmax (bounds.getWidth(),
                                   waive::Spacing::trackHeaderWidth + (int) (pixelsPerSecond * 60.0));
    trackContainer.setSize (contentWidth, juce::jmax (totalHeight, bounds.getHeight()));

    int y = 0;
    for (auto& lane : trackLanes)
    {
        lane->setBounds (0, y, contentWidth, trackLaneHeight);
        y += trackLaneHeight;
    }

    // Update scrollbar range based on edit length
    double maxTime = 60.0;  // default
    auto& edit = editSession.getEdit();
    for (auto* track : te::getAudioTracks (edit))
    {
        for (auto* clip : track->getClips())
        {
            double clipEnd = clip->getPosition().getEnd().inSeconds();
            maxTime = juce::jmax (maxTime, clipEnd);
        }
    }
    maxTime += 10.0;  // padding

    double viewportWidthSecs = bounds.getWidth() / pixelsPerSecond;
    horizontalScrollbar.setRangeLimits (0.0, maxTime);
    horizontalScrollbar.setCurrentRange (scrollOffsetSeconds, viewportWidthSecs, juce::dontSendNotification);
}

void TimelineComponent::paint (juce::Graphics& g)
{
    auto* pal = waive::getWaivePalette (*this);
    g.fillAll (pal ? pal->windowBg : juce::Colour (0xff1e1e1e));

    if (trackLanes.empty())
    {
        g.setFont (waive::Fonts::body());
        g.setColour (pal ? pal->textMuted : juce::Colour (0xff808080));
        g.drawText ("Click '+ Track' to add your first track", getLocalBounds(), juce::Justification::centred, true);
    }
}

void TimelineComponent::mouseWheelMove (const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        // Check if over track area for track height zoom
        if (e.y > waive::Spacing::rulerHeight && e.x < waive::Spacing::trackHeaderWidth)
        {
            // Track height zoom
            int delta = wheel.deltaY > 0 ? 10 : -10;
            setTrackLaneHeight (trackLaneHeight + delta);
            return;
        }

        // Horizontal zoom
        double zoomFactor = 1.0 + wheel.deltaY * 0.3;
        double mouseTime = xToTime (e.x);

        pixelsPerSecond = juce::jlimit (10.0, 1000.0, pixelsPerSecond * zoomFactor);

        // Adjust scroll to keep mouse position stable
        scrollOffsetSeconds = mouseTime - e.x / pixelsPerSecond;
        scrollOffsetSeconds = juce::jmax (0.0, scrollOffsetSeconds);

        resized();
        repaint();
    }
    else if (e.mods.isShiftDown())
    {
        // Shift+wheel: horizontal scroll
        scrollOffsetSeconds -= wheel.deltaY * 2.0 / pixelsPerSecond;
        scrollOffsetSeconds = juce::jmax (0.0, scrollOffsetSeconds);

        // Update scrollbar position
        double viewportWidthSecs = getWidth() / pixelsPerSecond;
        horizontalScrollbar.setCurrentRange (scrollOffsetSeconds, viewportWidthSecs, juce::dontSendNotification);

        resized();
        repaint();
    }
    else
    {
        // Plain wheel: vertical scroll (handled by viewport)
        // Do nothing, let parent viewport handle it
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
        {
            waive::showMessageBoxAsyncSafe (juce::MessageBoxIconType::WarningIcon,
                                            "Cannot Drop File", "The selected file does not exist.");
            return;
        }

        auto localPos = details.localPosition;
        double dropTime = snapTimeToGrid (juce::jmax (0.0, xToTime (localPos.x)));
        int laneIndex = trackIndexAtY (localPos.y - waive::Spacing::rulerHeight);

        auto& edit = editSession.getEdit();
        auto tracks = te::getAudioTracks (edit);

        if (tracks.isEmpty())
        {
            waive::showMessageBoxAsyncSafe (juce::MessageBoxIconType::WarningIcon,
                                            "Cannot Drop File", "No tracks available. Add a track first.");
            return;
        }

        auto* track = getAudioTrackForLaneIndex (laneIndex);
        if (track == nullptr)
            track = tracks.getFirst();
        te::AudioFile audioFile (edit.engine, file);
        auto duration = audioFile.getLength();

        if (duration <= 0.0)
        {
            waive::showMessageBoxAsyncSafe (juce::MessageBoxIconType::WarningIcon,
                                            "Cannot Drop File", "The file format is not supported or could not be read.");
            return;
        }

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
    return waive::Spacing::trackHeaderWidth + (int) ((seconds - scrollOffsetSeconds) * pixelsPerSecond);
}

double TimelineComponent::xToTime (int x) const
{
    return scrollOffsetSeconds + (x - waive::Spacing::trackHeaderWidth) / pixelsPerSecond;
}

int TimelineComponent::trackIndexAtY (int y) const
{
    if (y < 0) return 0;
    return (y + trackViewport.getViewPositionY()) / trackLaneHeight;
}

double TimelineComponent::snapTimeToGrid (double seconds) const
{
    if (! snapEnabled)
        return juce::jmax (0.0, seconds);

    auto& sequence = editSession.getEdit().tempoSequence;
    auto time = te::TimePosition::fromSeconds (juce::jmax (0.0, seconds));
    auto beats = sequence.toBeats (time).inBeats();

    switch (snapResolution)
    {
        case SnapResolution::bar:
        {
            auto barsBeats = sequence.toBarsAndBeats (time);
            const auto nearestBar = juce::jmax (0, (int) std::round (barsBeats.getTotalBars()));
            te::tempo::BarsAndBeats snappedBarsBeats;
            snappedBarsBeats.bars = nearestBar;
            return sequence.toTime (snappedBarsBeats).inSeconds();
        }
        case SnapResolution::beat:
        {
            auto snappedBeats = std::round (beats);
            return sequence.toTime (te::BeatPosition::fromBeats (snappedBeats)).inSeconds();
        }
        case SnapResolution::halfBeat:
        {
            constexpr double step = 0.5;
            auto snappedBeats = std::round (beats / step) * step;
            return sequence.toTime (te::BeatPosition::fromBeats (snappedBeats)).inSeconds();
        }
        case SnapResolution::quarterBeat:
        {
            constexpr double step = 0.25;
            auto snappedBeats = std::round (beats / step) * step;
            return sequence.toTime (te::BeatPosition::fromBeats (snappedBeats)).inSeconds();
        }
    }

    return sequence.toTime (te::BeatPosition::fromBeats (beats)).inSeconds();
}

void TimelineComponent::getGridLineTimes (double startSeconds, double endSeconds,
                                          juce::Array<double>& majorLines,
                                          juce::Array<double>& minorLines) const
{
    majorLines.clear();
    minorLines.clear();

    auto& sequence = editSession.getEdit().tempoSequence;
    auto startBeat = sequence.toBeats (te::TimePosition::fromSeconds (juce::jmax (0.0, startSeconds))).inBeats();
    auto endBeat = sequence.toBeats (te::TimePosition::fromSeconds (juce::jmax (0.0, endSeconds))).inBeats();

    int lineCount = 0;
    constexpr int maxLines = 10000;

    auto addLine = [&] (double beatValue)
    {
        auto timeSeconds = sequence.toTime (te::BeatPosition::fromBeats (beatValue)).inSeconds();
        if (timeSeconds < startSeconds - 0.1 || timeSeconds > endSeconds + 0.1)
            return;

        auto barsBeats = sequence.toBarsAndBeats (te::TimePosition::fromSeconds (timeSeconds));
        const bool isBarLine = std::abs (barsBeats.beats.inBeats()) < 1.0e-4;

        if (isBarLine)
            majorLines.addIfNotAlreadyThere (timeSeconds);
        else
            minorLines.addIfNotAlreadyThere (timeSeconds);
    };

    if (snapResolution == SnapResolution::quarterBeat)
    {
        constexpr double step = 0.25;
        auto first = std::floor (startBeat / step) * step;
        for (double beat = first; beat <= endBeat + step; beat += step)
        {
            if (++lineCount > maxLines)
                break;
            addLine (beat);
        }
        return;
    }

    if (snapResolution == SnapResolution::halfBeat)
    {
        constexpr double step = 0.5;
        auto first = std::floor (startBeat / step) * step;
        for (double beat = first; beat <= endBeat + step; beat += step)
        {
            if (++lineCount > maxLines)
                break;
            addLine (beat);
        }
        return;
    }

    auto firstWholeBeat = std::floor (startBeat);
    for (double beat = firstWholeBeat; beat <= endBeat + 1.0; beat += 1.0)
    {
        if (++lineCount > maxLines)
            break;
        addLine (beat);
    }
}

void TimelineComponent::setSnapEnabled (bool enabled)
{
    if (snapEnabled == enabled)
        return;

    snapEnabled = enabled;
    repaint();
}

void TimelineComponent::setSnapResolution (SnapResolution resolution)
{
    if (snapResolution == resolution)
        return;

    snapResolution = resolution;
    repaint();
}

void TimelineComponent::setShowBarsBeatsRuler (bool shouldShow)
{
    if (showBarsBeatsRuler == shouldShow)
        return;

    showBarsBeatsRuler = shouldShow;
    ruler->repaint();
}

void TimelineComponent::setTrackLaneHeight (int height)
{
    int newHeight = juce::jlimit (60, 300, height);
    if (trackLaneHeight == newHeight)
        return;

    trackLaneHeight = newHeight;
    resized();
    repaint();
}


void TimelineComponent::rebuildTracks()
{
    selectionManager->deselectAll();

    trackLanes.clear();
    laneDescriptors.clear();
    trackContainer.removeAllChildren();

    for (auto* track : editSession.getEdit().getTrackList())
        if (track != nullptr && track->getParentFolderTrack() == nullptr)
            appendTrackLaneRecursive (*track, 0);

    lastTrackCount = getDisplayTrackCount();
    resized();
}

void TimelineComponent::timerCallback()
{
    if (getDisplayTrackCount() != lastTrackCount)
        rebuildTracks();

    // Poll all track lanes
    for (auto& lane : trackLanes)
        if (lane != nullptr)
            lane->pollState();
}

void TimelineComponent::scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved == &horizontalScrollbar)
    {
        if (std::abs (newRangeStart - scrollOffsetSeconds) > 0.001)
        {
            scrollOffsetSeconds = newRangeStart;
            resized();
            repaint();
        }
    }
}

void TimelineComponent::deleteSelectedClips()
{
    auto selected = selectionManager->getSelectedClips();
    if (selected.isEmpty())
        return;

    waive::deleteClips (editSession, selected);
    selectionManager->deselectAll();
    rebuildTracks();
}

void TimelineComponent::duplicateSelectedClips()
{
    auto selected = selectionManager->getSelectedClips();
    if (selected.isEmpty())
        return;

    for (auto* clip : selected)
        if (clip != nullptr)
            waive::duplicateClip (editSession, *clip);

    rebuildTracks();
}

void TimelineComponent::splitSelectedClipsAtPlayhead()
{
    auto selected = selectionManager->getSelectedClips();
    if (selected.isEmpty())
        return;

    auto splitTime = editSession.getEdit().getTransport().getPosition().inSeconds();
    for (auto* clip : selected)
        if (clip != nullptr)
            waive::splitClipAtPosition (editSession, *clip, splitTime);

    rebuildTracks();
}

void TimelineComponent::selectClipsByIDForPreview (const juce::Array<te::EditItemID>& clipIDs)
{
    previewClipIDs.clear();
    for (auto id : clipIDs)
        previewClipIDs.insert (id);
    trackContainer.repaint();
}

void TimelineComponent::clearPreviewSelection()
{
    previewClipIDs.clear();
    trackContainer.repaint();
}

bool TimelineComponent::addAutomationPointForTrackParam (int trackIndex,
                                                         const juce::String& paramID,
                                                         double timeSeconds,
                                                         float normalisedValue)
{
    auto tracks = te::getAudioTracks (editSession.getEdit());
    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return false;

    auto* track = tracks.getUnchecked (trackIndex);
    if (track == nullptr)
        return false;

    auto params = track->getAllAutomatableParams();
    te::AutomatableParameter* selectedParam = nullptr;
    for (auto* param : params)
    {
        if (param != nullptr && param->paramID == paramID)
        {
            selectedParam = param;
            break;
        }
    }

    if (selectedParam == nullptr)
        return false;

    const auto pointTime = te::TimePosition::fromSeconds (snapTimeToGrid (timeSeconds));
    const auto pointValue = (float) normalisedToParameterValue (normalisedValue, selectedParam->getValueRange());

    return editSession.performEdit ("Add Automation Point", [&] (te::Edit& edit)
    {
        selectedParam->getCurve().addPoint (pointTime, pointValue, 0.0f, &edit.getUndoManager());
    });
}

bool TimelineComponent::moveAutomationPointForTrackParam (int trackIndex,
                                                          const juce::String& paramID,
                                                          int pointIndex,
                                                          double timeSeconds,
                                                          float normalisedValue)
{
    auto tracks = te::getAudioTracks (editSession.getEdit());
    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return false;

    auto* track = tracks.getUnchecked (trackIndex);
    if (track == nullptr)
        return false;

    auto params = track->getAllAutomatableParams();
    te::AutomatableParameter* selectedParam = nullptr;
    for (auto* param : params)
    {
        if (param != nullptr && param->paramID == paramID)
        {
            selectedParam = param;
            break;
        }
    }

    if (selectedParam == nullptr)
        return false;

    auto& curve = selectedParam->getCurve();
    if (! juce::isPositiveAndBelow (pointIndex, curve.getNumPoints()))
        return false;

    const auto pointTime = te::TimePosition::fromSeconds (snapTimeToGrid (timeSeconds));
    const auto pointValue = (float) normalisedToParameterValue (normalisedValue, selectedParam->getValueRange());

    return editSession.performEdit ("Move Automation Point", true, [&] (te::Edit& edit)
    {
        curve.movePoint (*selectedParam, pointIndex, pointTime, pointValue, false, &edit.getUndoManager());
    });
}

void TimelineComponent::selectionChanged()
{
    trackContainer.repaint();
}

void TimelineComponent::editAboutToChange()
{
    selectionManager->deselectAll();
    collapsedFolderTrackIDs.clear();
    trackLanes.clear();
    trackContainer.removeAllChildren();
    lastTrackCount = -1;
}

void TimelineComponent::editChanged()
{
    selectionManager->setEdit (&editSession.getEdit());
    rebuildTracks();
    ruler->repaint();
    playhead->repaint();
}

std::vector<TrackLaneComponent*> TimelineComponent::getTrackLaneComponentsForTesting() const
{
    std::vector<TrackLaneComponent*> result;
    result.reserve (trackLanes.size());
    for (auto& lane : trackLanes)
        result.push_back (lane.get());
    return result;
}

bool TimelineComponent::isFolderCollapsed (te::EditItemID trackID) const
{
    return collapsedFolderTrackIDs.count (trackID) > 0;
}

void TimelineComponent::toggleFolderCollapsed (te::EditItemID trackID)
{
    if (collapsedFolderTrackIDs.count (trackID) > 0)
        collapsedFolderTrackIDs.erase (trackID);
    else
        collapsedFolderTrackIDs.insert (trackID);

    rebuildTracks();
}

te::AudioTrack* TimelineComponent::getAudioTrackForLaneIndex (int laneIndex) const
{
    if (! juce::isPositiveAndBelow (laneIndex, (int) trackLanes.size()))
        return nullptr;

    if (auto* audioTrack = trackLanes[(size_t) laneIndex]->getAudioTrack())
        return audioTrack;

    for (int i = laneIndex + 1; i < (int) trackLanes.size(); ++i)
        if (auto* audioTrack = trackLanes[(size_t) i]->getAudioTrack())
            return audioTrack;

    for (int i = laneIndex - 1; i >= 0; --i)
        if (auto* audioTrack = trackLanes[(size_t) i]->getAudioTrack())
            return audioTrack;

    return nullptr;
}

void TimelineComponent::appendTrackLaneRecursive (te::Track& track, int depth)
{
    auto* audioTrack = dynamic_cast<te::AudioTrack*> (&track);
    auto* folderTrack = dynamic_cast<te::FolderTrack*> (&track);

    if (audioTrack == nullptr && folderTrack == nullptr)
        return;

    auto lane = std::make_unique<TrackLaneComponent> (track, *this, (int) trackLanes.size(), depth);
    trackContainer.addAndMakeVisible (lane.get());
    laneDescriptors.push_back ({ &track, depth });
    trackLanes.push_back (std::move (lane));

    if (folderTrack == nullptr || isFolderCollapsed (track.itemID))
        return;

    for (auto* child : folderTrack->getAllSubTracks (false))
        if (child != nullptr)
            appendTrackLaneRecursive (*child, depth + 1);
}

int TimelineComponent::getDisplayTrackCount() const
{
    int count = 0;
    for (auto* track : editSession.getEdit().getTrackList())
        if (dynamic_cast<te::AudioTrack*> (track) != nullptr
            || dynamic_cast<te::FolderTrack*> (track) != nullptr)
            ++count;
    return count;
}
