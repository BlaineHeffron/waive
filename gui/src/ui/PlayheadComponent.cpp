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
    auto pos = editSession.getEdit().getTransport().getPosition().inSeconds();
    int x = timeline.timeToX (pos);

    if (x >= 0 && x < getWidth())
    {
        auto* pal = waive::getWaivePalette (*this);
        g.setColour (pal ? pal->playhead : juce::Colours::white);
        g.drawVerticalLine (x, 0.0f, (float) getHeight());
    }
}

void PlayheadComponent::timerCallback()
{
    repaint();
}
