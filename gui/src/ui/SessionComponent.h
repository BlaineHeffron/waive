#pragma once

#include <JuceHeader.h>

class EditSession;
class UndoableCommandHandler;
class TimelineComponent;
class MixerComponent;

//==============================================================================
/** Transport toolbar + timeline + mixer. */
class SessionComponent : public juce::Component,
                         private juce::Timer
{
public:
    SessionComponent (EditSession& session, UndoableCommandHandler& handler);
    ~SessionComponent() override;

    void resized() override;

    TimelineComponent& getTimeline();

private:
    void timerCallback() override;
    void runTransportAction (const juce::String& action);

    EditSession& editSession;
    UndoableCommandHandler& commandHandler;

    // Transport toolbar
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton recordButton;
    juce::TextButton addTrackButton;
    juce::Label positionLabel;

    // Main areas
    std::unique_ptr<TimelineComponent> timeline;
    std::unique_ptr<MixerComponent> mixer;

    juce::StretchableLayoutManager layoutManager;
    std::unique_ptr<juce::StretchableLayoutResizerBar> resizerBar;
};
