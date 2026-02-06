#include "TimeRulerComponent.h"
#include "TimelineComponent.h"
#include "EditSession.h"

#include <cmath>

namespace te = tracktion;

TimeRulerComponent::TimeRulerComponent (EditSession& session, TimelineComponent& tl)
    : editSession (session), timeline (tl)
{
}

void TimeRulerComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2a2a));

    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromLeft (TimelineComponent::trackHeaderWidth);
    g.setColour (juce::Colour (0xff252525));
    g.fillRect (headerBounds);

    const double pps = timeline.getPixelsPerSecond();
    const double scrollOffset = timeline.getScrollOffsetSeconds();
    const double startTime = scrollOffset;
    const double endTime = scrollOffset + bounds.getWidth() / pps;

    g.setColour (juce::Colours::grey.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));

    if (timeline.getShowBarsBeatsRuler())
    {
        juce::Array<double> majorLines, minorLines;
        timeline.getGridLineTimes (startTime, endTime, majorLines, minorLines);

        g.setColour (juce::Colours::grey.withAlpha (0.35f));
        for (auto t : minorLines)
        {
            const int x = timeline.timeToX (t);
            g.drawVerticalLine (x, (float) bounds.getHeight() * 0.6f, (float) bounds.getHeight());
        }

        g.setColour (juce::Colours::lightgrey.withAlpha (0.7f));
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
}

void TimeRulerComponent::mouseDown (const juce::MouseEvent& e)
{
    double time = timeline.xToTime (e.x);
    if (time >= 0.0)
        editSession.getEdit().getTransport().setPosition (te::TimePosition::fromSeconds (time));
}
