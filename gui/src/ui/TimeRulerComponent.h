#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class TimelineComponent;

//==============================================================================
/** Horizontal time ruler with second markings. Click to seek. */
class TimeRulerComponent : public juce::Component
{
public:
    TimeRulerComponent (te::Edit& edit, TimelineComponent& timeline);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    te::Edit& edit;
    TimelineComponent& timeline;
};
