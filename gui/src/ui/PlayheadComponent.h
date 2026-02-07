#pragma once

#include <JuceHeader.h>

class TimelineComponent;
class EditSession;

//==============================================================================
/** Thin vertical playhead line, updated at 30Hz. */
class PlayheadComponent : public juce::Component,
                          private juce::Timer
{
public:
    PlayheadComponent (EditSession& session, TimelineComponent& timeline);
    ~PlayheadComponent() override;

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    EditSession& editSession;
    TimelineComponent& timeline;
    int lastPlayheadX = -1;
};
