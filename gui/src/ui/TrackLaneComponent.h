#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class TimelineComponent;
class ClipComponent;

//==============================================================================
/** Grid line representation with minor/major flag. */
struct GridLine
{
    int x;
    bool isMinor;
};

//==============================================================================
/** Single track's horizontal clip lane. */
class TrackLaneComponent : public juce::Component
{
public:
    static constexpr int automationLaneHeight = 34;

    TrackLaneComponent (te::AudioTrack& track, TimelineComponent& timeline, int trackIndex = 0);
    ~TrackLaneComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    void updateClips();
    void pollState();

    te::AudioTrack& getTrack()  { return track; }

    juce::Colour getTrackColorForTesting() const;

private:
    void refreshAutomationParams();
    void layoutClipComponents();
    void showTrackContextMenu();
    juce::Rectangle<int> getClipLaneBounds() const;
    juce::Rectangle<int> getAutomationBounds() const;
    te::AutomatableParameter* getSelectedAutomationParameter() const;
    float normalisedFromAutomationY (int y) const;
    int automationYForNormalisedValue (float normalised) const;
    int findNearbyAutomationPoint (te::AutomationCurve& curve, te::AutomatableParameter& param, int mouseX, int mouseY) const;

    te::AudioTrack& track;
    TimelineComponent& timeline;
    int trackIndex = 0;

    juce::Label headerLabel;
    juce::ComboBox automationParamCombo;
    juce::ReferenceCountedArray<te::AutomatableParameter> automationParams;
    std::vector<std::unique_ptr<ClipComponent>> clipComponents;

    int lastClipCount = -1;
    int lastAutomatableParamCount = -1;
    int draggingAutomationPointIndex = -1;
    bool isHeaderHovered = false;

    bool layoutDirty = true;
    double lastScrollOffset = 0.0;
    double lastPixelsPerSecond = 0.0;

    // Grid line cache
    std::vector<GridLine> cachedGridLines;
    double cachedScrollOffsetForGrid = -1.0;
    double cachedPixelsPerSecondForGrid = -1.0;
};
