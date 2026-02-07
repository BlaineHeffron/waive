#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class EditSession;

//==============================================================================
/** Single vertical channel strip: fader + pan + meter + name. */
class MixerChannelStrip : public juce::Component
{
public:
    /** Track strip. */
    MixerChannelStrip (te::AudioTrack& track, EditSession& session);

    /** Master strip. */
    MixerChannelStrip (te::Edit& edit, EditSession& session);

    ~MixerChannelStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void setHighlighted (bool shouldHighlight);
    bool isHighlightedForTesting() const       { return highlighted; }

    void pollState();

    static constexpr int stripWidth = 80;

private:
    void setupControls();

    EditSession& editSession;
    te::AudioTrack* track = nullptr;
    te::Edit* masterEdit = nullptr;

    juce::Label nameLabel;
    juce::Slider faderSlider;
    juce::Slider panKnob;
    juce::ToggleButton soloButton;
    juce::ToggleButton muteButton;
    juce::ToggleButton armButton;
    juce::ComboBox inputCombo;

    // Metering
    te::LevelMeasurer::Client meterClient;
    float peakL = 0.0f;
    float peakR = 0.0f;
    float lastPeakL = 0.0f;
    float lastPeakR = 0.0f;
    float peakHoldL = -60.0f;
    float peakHoldR = -60.0f;
    int peakHoldDecayCounterL = 0;
    int peakHoldDecayCounterR = 0;
    juce::Rectangle<int> lastMeterBounds;
    bool isMaster = false;
    bool highlighted = false;
    bool suppressControlCallbacks = false;

    // Cache meter gradient
    juce::ColourGradient meterGradient;
    juce::Rectangle<int> cachedMeterGradientBounds;
};
