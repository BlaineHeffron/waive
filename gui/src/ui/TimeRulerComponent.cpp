#include "TimeRulerComponent.h"
#include "TimelineComponent.h"

TimeRulerComponent::TimeRulerComponent (te::Edit& e, TimelineComponent& tl)
    : edit (e), timeline (tl)
{
}

void TimeRulerComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2a2a));

    auto bounds = getLocalBounds();
    double pps = timeline.getPixelsPerSecond();
    double scrollOffset = timeline.getScrollOffsetSeconds();

    double startTime = scrollOffset;
    double endTime = scrollOffset + bounds.getWidth() / pps;

    // Choose tick interval based on zoom level
    double interval = 1.0;
    if (pps < 20)       interval = 10.0;
    else if (pps < 50)  interval = 5.0;
    else if (pps < 200) interval = 1.0;
    else                 interval = 0.5;

    double firstTick = std::ceil (startTime / interval) * interval;

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (11.0f));

    for (double t = firstTick; t <= endTime; t += interval)
    {
        int x = timeline.timeToX (t);

        g.drawVerticalLine (x, (float) bounds.getHeight() * 0.5f, (float) bounds.getHeight());

        int mins = (int) t / 60;
        double secs = t - mins * 60;
        juce::String label;
        if (mins > 0)
            label = juce::String (mins) + ":" + juce::String (secs, 1);
        else
            label = juce::String (t, 1) + "s";

        g.drawText (label, x + 2, 0, 60, bounds.getHeight() / 2,
                    juce::Justification::centredLeft, false);
    }
}

void TimeRulerComponent::mouseDown (const juce::MouseEvent& e)
{
    double time = timeline.xToTime (e.x);
    if (time >= 0.0)
        edit.getTransport().setPosition (te::TimePosition::fromSeconds (time));
}
