#pragma once

#include <JuceHeader.h>

class TimelineComponent;
class EditSession;

//==============================================================================
/** Horizontal time ruler with second markings. Click to seek. */
class TimeRulerComponent : public juce::Component
{
public:
    TimeRulerComponent (EditSession& session, TimelineComponent& timeline);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    EditSession& editSession;
    TimelineComponent& timeline;
};
