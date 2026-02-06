#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class TimelineComponent;
class ClipComponent;

//==============================================================================
/** Single track's horizontal clip lane. */
class TrackLaneComponent : public juce::Component,
                           private juce::Timer
{
public:
    TrackLaneComponent (te::AudioTrack& track, TimelineComponent& timeline);
    ~TrackLaneComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void updateClips();

    te::AudioTrack& getTrack()  { return track; }

private:
    void timerCallback() override;

    te::AudioTrack& track;
    TimelineComponent& timeline;

    juce::Label headerLabel;
    std::vector<std::unique_ptr<ClipComponent>> clipComponents;

    int lastClipCount = -1;
};
