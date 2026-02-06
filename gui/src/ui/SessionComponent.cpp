#include "SessionComponent.h"
#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "CommandHelpers.h"
#include "TimelineComponent.h"
#include "MixerComponent.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

using waive::runCommand;
using waive::makeAction;

//==============================================================================
SessionComponent::SessionComponent (EditSession& session, UndoableCommandHandler& handler)
    : editSession (session), commandHandler (handler)
{
    playButton.setButtonText ("Play");
    stopButton.setButtonText ("Stop");
    recordButton.setButtonText ("Rec");
    addTrackButton.setButtonText ("+ Track");

    positionLabel.setText ("0:00.0", juce::dontSendNotification);
    positionLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (addTrackButton);
    addAndMakeVisible (positionLabel);

    playButton.onClick  = [this] { runTransportAction ("transport_play"); };
    stopButton.onClick  = [this] { runTransportAction ("transport_stop"); };
    recordButton.onClick = [this]
    {
        auto& transport = editSession.getEdit().getTransport();

        if (transport.isRecording())
        {
            editSession.performEdit ("Stop Recording", [&] (te::Edit&)
            {
                transport.stopRecording (false);
            });
        }
        else
        {
            // Allow record start even if no inputs are armed; actual clips are created when stopping.
            transport.record (false, true);
        }
    };

    addTrackButton.onClick = [this]
    {
        editSession.performEdit ("Add Track", [&] (te::Edit& edit)
        {
            auto trackCount = te::getAudioTracks (edit).size();
            edit.ensureNumberOfAudioTracks (trackCount + 1);
        });
    };

    // Timeline
    timeline = std::make_unique<TimelineComponent> (editSession);
    addAndMakeVisible (timeline.get());

    // Mixer
    mixer = std::make_unique<MixerComponent> (editSession);
    addAndMakeVisible (mixer.get());

    // Resizer bar between timeline and mixer
    layoutManager.setItemLayout (0, 100, -1.0, -0.75);   // timeline: flexible
    layoutManager.setItemLayout (1, 4, 4, 4);             // resizer bar
    layoutManager.setItemLayout (2, 80, 300, 160);        // mixer: default 160px

    resizerBar = std::make_unique<juce::StretchableLayoutResizerBar> (&layoutManager, 1, false);
    addAndMakeVisible (resizerBar.get());

    startTimerHz (10);
}

SessionComponent::~SessionComponent() = default;

void SessionComponent::resized()
{
    auto bounds = getLocalBounds();

    // Transport toolbar
    auto toolbar = bounds.removeFromTop (36);
    toolbar = toolbar.reduced (8, 4);
    playButton.setBounds (toolbar.removeFromLeft (60));
    toolbar.removeFromLeft (4);
    stopButton.setBounds (toolbar.removeFromLeft (60));
    toolbar.removeFromLeft (4);
    recordButton.setBounds (toolbar.removeFromLeft (60));
    toolbar.removeFromLeft (12);
    addTrackButton.setBounds (toolbar.removeFromLeft (80));
    toolbar.removeFromLeft (12);
    positionLabel.setBounds (toolbar.removeFromLeft (120));

    // Layout: timeline + resizer + mixer
    juce::Component* comps[] = { timeline.get(), resizerBar.get(), mixer.get() };
    layoutManager.layOutComponents (comps, 3, bounds.getX(), bounds.getY(),
                                    bounds.getWidth(), bounds.getHeight(), true, true);
}

TimelineComponent& SessionComponent::getTimeline()
{
    return *timeline;
}

void SessionComponent::timerCallback()
{
    auto& transport = editSession.getEdit().getTransport();
    auto pos = transport.getPosition().inSeconds();
    int mins = (int) pos / 60;
    double secs = pos - mins * 60;
    positionLabel.setText (juce::String (mins) + ":" + juce::String (secs, 1),
                           juce::dontSendNotification);

    recordButton.setColour (juce::TextButton::buttonColourId,
                            transport.isRecording() ? juce::Colours::darkred
                                                    : juce::Colours::darkgrey);
}

void SessionComponent::runTransportAction (const juce::String& action)
{
    auto cmd = makeAction (action);
    (void) runCommand (commandHandler, cmd);
}
