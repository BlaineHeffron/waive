#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class TimelineComponent;

//==============================================================================
/** Thin vertical playhead line, updated at 30Hz. */
class PlayheadComponent : public juce::Component,
                          private juce::Timer
{
public:
    PlayheadComponent (te::Edit& edit, TimelineComponent& timeline);
    ~PlayheadComponent() override;

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    te::Edit& edit;
    TimelineComponent& timeline;
};
