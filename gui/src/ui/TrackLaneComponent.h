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
    static constexpr int automationLaneHeight = 34;

    TrackLaneComponent (te::AudioTrack& track, TimelineComponent& timeline);
    ~TrackLaneComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    void updateClips();

    te::AudioTrack& getTrack()  { return track; }

private:
    void timerCallback() override;
    void refreshAutomationParams();
    void layoutClipComponents();
    juce::Rectangle<int> getClipLaneBounds() const;
    juce::Rectangle<int> getAutomationBounds() const;
    te::AutomatableParameter* getSelectedAutomationParameter() const;
    float normalisedFromAutomationY (int y) const;
    int automationYForNormalisedValue (float normalised) const;
    int findNearbyAutomationPoint (te::AutomationCurve& curve, te::AutomatableParameter& param, int mouseX, int mouseY) const;

    te::AudioTrack& track;
    TimelineComponent& timeline;

    juce::Label headerLabel;
    juce::ComboBox automationParamCombo;
    juce::ReferenceCountedArray<te::AutomatableParameter> automationParams;
    std::vector<std::unique_ptr<ClipComponent>> clipComponents;

    int lastClipCount = -1;
    int lastAutomatableParamCount = -1;
    int draggingAutomationPointIndex = -1;
};
