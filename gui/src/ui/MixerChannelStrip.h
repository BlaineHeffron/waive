#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class EditSession;

//==============================================================================
/** Single vertical channel strip: fader + pan + meter + name. */
class MixerChannelStrip : public juce::Component,
                          private juce::Timer
{
public:
    /** Track strip. */
    MixerChannelStrip (te::AudioTrack& track, EditSession& session);

    /** Master strip. */
    MixerChannelStrip (te::Edit& edit, EditSession& session);

    ~MixerChannelStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    static constexpr int stripWidth = 80;

private:
    void timerCallback() override;
    void setupControls();

    EditSession& editSession;
    te::AudioTrack* track = nullptr;
    te::Edit* masterEdit = nullptr;

    juce::Label nameLabel;
    juce::Slider faderSlider;
    juce::Slider panKnob;

    // Metering
    te::LevelMeasurer::Client meterClient;
    float peakL = 0.0f;
    float peakR = 0.0f;
    bool isMaster = false;
};
