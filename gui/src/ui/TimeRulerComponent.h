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
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

    // Test helpers that drive the same loop-marker drag path as mouse interaction.
    bool beginLoopStartDragForTesting();
    bool beginLoopEndDragForTesting();
    void dragActiveLoopMarkerToTimeForTesting (double seconds);
    void endLoopMarkerDragForTesting();

private:
    enum LoopDragMode { None, DraggingStart, DraggingEnd };
    static constexpr int loopMarkerHitThreshold = 8;

    bool beginLoopMarkerDragAtX (int x);
    void dragActiveLoopMarkerToX (int x);
    void seekToX (int x);

    LoopDragMode loopDragMode = None;

    EditSession& editSession;
    TimelineComponent& timeline;
};
