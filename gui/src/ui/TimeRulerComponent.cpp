#include "TimeRulerComponent.h"
#include "TimelineComponent.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"

#include <cmath>

namespace te = tracktion;

namespace
{
void setLoopRangeWithUndo (te::Edit& edit, te::TimePosition start, te::TimePosition end)
{
    auto& transport = edit.getTransport();
    const auto maxEndTime = te::toPosition (edit.getLength() + te::Edit::getMaximumLength() * 0.75);
    const auto clampedStart = juce::jlimit (te::TimePosition(), maxEndTime, start);
    const auto clampedEnd = juce::jlimit (te::TimePosition(), maxEndTime, end);
    auto& undoManager = edit.getUndoManager();

    transport.loopPoint1.setValue (clampedStart, &undoManager);
    transport.loopPoint2.setValue (clampedEnd, &undoManager);
}
}

TimeRulerComponent::TimeRulerComponent (EditSession& session, TimelineComponent& tl)
    : editSession (session), timeline (tl)
{
}

void TimeRulerComponent::paint (juce::Graphics& g)
{
    auto* pal = waive::getWaivePalette (*this);

    g.fillAll (pal ? pal->surfaceBg : juce::Colour (0xff1a1a1a));

    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromLeft (TimelineComponent::trackHeaderWidth);
    g.setColour (pal ? pal->surfaceBgAlt : juce::Colour (0xff222222));
    g.fillRect (headerBounds);

    const double pps = timeline.getPixelsPerSecond();
    const double scrollOffset = timeline.getScrollOffsetSeconds();
    const double startTime = scrollOffset;
    const double endTime = scrollOffset + bounds.getWidth() / pps;

    g.setColour (pal ? pal->textSecondary : juce::Colour (0xff999999));
    g.setFont (waive::Fonts::caption());

    if (timeline.getShowBarsBeatsRuler())
    {
        juce::Array<double> majorLines, minorLines;
        timeline.getGridLineTimes (startTime, endTime, majorLines, minorLines);

        g.setColour (pal ? pal->gridMinor.withAlpha (0.35f) : juce::Colour (0xff2a2a2a));
        for (auto t : minorLines)
        {
            const int x = timeline.timeToX (t);
            g.drawVerticalLine (x, (float) bounds.getHeight() * 0.6f, (float) bounds.getHeight());
        }

        g.setColour (pal ? pal->gridMajor.withAlpha (0.7f) : juce::Colour (0xff3a3a3a));
        for (auto t : majorLines)
        {
            const int x = timeline.timeToX (t);
            g.drawVerticalLine (x, 0.0f, (float) bounds.getHeight());

            auto barsBeats = editSession.getEdit().tempoSequence.toBarsAndBeats (te::TimePosition::fromSeconds (t));
            const auto bar = barsBeats.bars + 1;
            const auto beat = (int) std::floor (barsBeats.beats.inBeats()) + 1;
            const auto label = juce::String (bar) + "." + juce::String (beat);
            g.drawText (label, x + 2, 0, 64, bounds.getHeight() / 2,
                        juce::Justification::centredLeft, false);
        }
    }
    else
    {
        // Fallback second-based ruler.
        double interval = 1.0;
        if (pps < 20)       interval = 10.0;
        else if (pps < 50)  interval = 5.0;
        else if (pps < 200) interval = 1.0;
        else                 interval = 0.5;

        const double firstTick = std::ceil (startTime / interval) * interval;
        for (double t = firstTick; t <= endTime; t += interval)
        {
            const int x = timeline.timeToX (t);
            g.drawVerticalLine (x, (float) bounds.getHeight() * 0.5f, (float) bounds.getHeight());

            const int mins = (int) t / 60;
            const double secs = t - mins * 60;
            juce::String label;
            if (mins > 0)
                label = juce::String (mins) + ":" + juce::String (secs, 1);
            else
                label = juce::String (t, 1) + "s";

            g.drawText (label, x + 2, 0, 60, bounds.getHeight() / 2,
                        juce::Justification::centredLeft, false);
        }
    }

    // Loop region visualization
    auto& transport = editSession.getEdit().getTransport();
    if (transport.looping)
    {
        auto loopRange = transport.getLoopRange();
        const int x1 = timeline.timeToX (loopRange.getStart().inSeconds());
        const int x2 = timeline.timeToX (loopRange.getEnd().inSeconds());

        // Draw loop region bar
        g.setColour (pal ? pal->primary.withAlpha (0.2f) : juce::Colour (0xff4477aa).withAlpha (0.2f));
        g.fillRect (x1, 0, x2 - x1, getHeight());

        // Draw loop markers as triangles
        g.setColour (pal ? pal->primary : juce::Colour (0xff4477aa));
        juce::Path startMarker, endMarker;
        const float markerSize = 6.0f;
        startMarker.addTriangle ((float) x1 - markerSize, 0.0f,
                                 (float) x1 + markerSize, 0.0f,
                                 (float) x1, markerSize);
        endMarker.addTriangle ((float) x2 - markerSize, 0.0f,
                               (float) x2 + markerSize, 0.0f,
                               (float) x2, markerSize);
        g.fillPath (startMarker);
        g.fillPath (endMarker);
    }
}

void TimeRulerComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! beginLoopMarkerDragAtX (e.x))
        seekToX (e.x);
}

void TimeRulerComponent::mouseDrag (const juce::MouseEvent& e)
{
    dragActiveLoopMarkerToX (e.x);
}

void TimeRulerComponent::mouseUp (const juce::MouseEvent&)
{
    if (loopDragMode != None)
        editSession.endCoalescedTransaction();

    loopDragMode = None;
}

void TimeRulerComponent::mouseMove (const juce::MouseEvent& e)
{
    auto& transport = editSession.getEdit().getTransport();

    if (transport.looping)
    {
        auto loopRange = transport.getLoopRange();
        const int x1 = timeline.timeToX (loopRange.getStart().inSeconds());
        const int x2 = timeline.timeToX (loopRange.getEnd().inSeconds());
        constexpr int threshold = 8;

        if (std::abs (e.x - x1) <= threshold || std::abs (e.x - x2) <= threshold)
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    setMouseCursor (juce::MouseCursor::NormalCursor);
}

bool TimeRulerComponent::beginLoopStartDragForTesting()
{
    auto& transport = editSession.getEdit().getTransport();
    if (! transport.looping)
        return false;

    const auto loopRange = transport.getLoopRange();
    return beginLoopMarkerDragAtX (timeline.timeToX (loopRange.getStart().inSeconds()));
}

bool TimeRulerComponent::beginLoopEndDragForTesting()
{
    auto& transport = editSession.getEdit().getTransport();
    if (! transport.looping)
        return false;

    const auto loopRange = transport.getLoopRange();
    return beginLoopMarkerDragAtX (timeline.timeToX (loopRange.getEnd().inSeconds()));
}

void TimeRulerComponent::dragActiveLoopMarkerToTimeForTesting (double seconds)
{
    dragActiveLoopMarkerToX (timeline.timeToX (seconds));
}

void TimeRulerComponent::endLoopMarkerDragForTesting()
{
    if (loopDragMode != None)
        editSession.endCoalescedTransaction();

    loopDragMode = None;
}

bool TimeRulerComponent::beginLoopMarkerDragAtX (int x)
{
    auto& transport = editSession.getEdit().getTransport();
    if (! transport.looping)
        return false;

    const auto loopRange = transport.getLoopRange();
    const int x1 = timeline.timeToX (loopRange.getStart().inSeconds());
    const int x2 = timeline.timeToX (loopRange.getEnd().inSeconds());

    if (std::abs (x - x1) <= loopMarkerHitThreshold)
    {
        loopDragMode = DraggingStart;
        return true;
    }

    if (std::abs (x - x2) <= loopMarkerHitThreshold)
    {
        loopDragMode = DraggingEnd;
        return true;
    }

    return false;
}

void TimeRulerComponent::dragActiveLoopMarkerToX (int x)
{
    if (loopDragMode == None)
        return;

    auto& transport = editSession.getEdit().getTransport();
    const auto loopRange = transport.getLoopRange();
    double newTime = juce::jmax (0.0, timeline.xToTime (x));

    if (loopDragMode == DraggingStart)
    {
        newTime = timeline.snapTimeToGrid (newTime);
        newTime = juce::jmin (newTime, loopRange.getEnd().inSeconds() - 0.1);
        editSession.performEdit ("Adjust Loop Start", true, [newTime, loopRange] (te::Edit& edit)
        {
            setLoopRangeWithUndo (edit, te::TimePosition::fromSeconds (newTime), loopRange.getEnd());
        });
    }
    else if (loopDragMode == DraggingEnd)
    {
        newTime = timeline.snapTimeToGrid (newTime);
        newTime = juce::jmax (newTime, loopRange.getStart().inSeconds() + 0.1);
        editSession.performEdit ("Adjust Loop End", true, [newTime, loopRange] (te::Edit& edit)
        {
            setLoopRangeWithUndo (edit, loopRange.getStart(), te::TimePosition::fromSeconds (newTime));
        });
    }

    repaint();
}

void TimeRulerComponent::seekToX (int x)
{
    const double time = timeline.xToTime (x);
    if (time >= 0.0)
        editSession.getEdit().getTransport().setPosition (te::TimePosition::fromSeconds (time));
}
