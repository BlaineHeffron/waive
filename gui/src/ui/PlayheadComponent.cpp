#include "PlayheadComponent.h"
#include "TimelineComponent.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"

PlayheadComponent::PlayheadComponent (EditSession& session, TimelineComponent& tl)
    : editSession (session), timeline (tl)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

PlayheadComponent::~PlayheadComponent()
{
    stopTimer();
}

void PlayheadComponent::paint (juce::Graphics& g)
{
    auto& transport = editSession.getEdit().getTransport();
    auto* pal = waive::getWaivePalette (*this);

    // Draw loop region overlay
    if (transport.looping)
    {
        auto loopRange = transport.getLoopRange();
        const int x1 = timeline.timeToX (loopRange.getStart().inSeconds());
        const int x2 = timeline.timeToX (loopRange.getEnd().inSeconds());

        g.setColour (pal ? pal->primary.withAlpha (0.08f) : juce::Colour (0xff4477aa).withAlpha (0.08f));
        g.fillRect (x1, 0, x2 - x1, getHeight());
    }

    // Draw playhead line (2px width for better visibility)
    auto pos = transport.getPosition().inSeconds();
    int x = timeline.timeToX (pos);

    if (x >= 0 && x < getWidth())
    {
        g.setColour (pal ? pal->playhead : juce::Colour (0xffff6600));
        g.fillRect ((float) x - 1.0f, 0.0f, 2.0f, (float) getHeight());
    }
}

void PlayheadComponent::timerCallback()
{
    auto pos = editSession.getEdit().getTransport().getPosition().inSeconds();
    int x = timeline.timeToX (pos);

    if (x != lastPlayheadX)
    {
        // Repaint old line region + new line region (wider for 2px line)
        if (lastPlayheadX >= 0)
            repaint (lastPlayheadX - 2, 0, 4, getHeight());
        if (x >= 0)
            repaint (x - 2, 0, 4, getHeight());
        lastPlayheadX = x;
    }
}
