#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class TimelineComponent;

//==============================================================================
/** Renders a single clip with waveform or colored rectangle. */
class ClipComponent : public juce::Component
{
public:
    ClipComponent (te::Clip& clip, TimelineComponent& timeline);
    ~ClipComponent() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

    void updatePosition();

    te::Clip& getClip()  { return clip; }

private:
    bool isLeftTrimZone (int x) const;
    bool isRightTrimZone (int x) const;
    void showContextMenu();

    te::Clip& clip;
    TimelineComponent& timeline;

    std::unique_ptr<te::SmartThumbnail> thumbnail;

    enum DragMode { None, Move, TrimLeft, TrimRight };
    DragMode dragMode = None;
    double dragStartTime = 0.0;
    double dragOriginalStart = 0.0;
    double dragOriginalEnd = 0.0;

    static constexpr int trimZoneWidth = 8;
};
