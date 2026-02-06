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
    tempoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    tempoSlider.setRange (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, 0.1);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 18);
    tempoSlider.setValue (120.0, juce::dontSendNotification);

    timeSigNumeratorBox.addItem ("2", 2);
    timeSigNumeratorBox.addItem ("3", 3);
    timeSigNumeratorBox.addItem ("4", 4);
    timeSigNumeratorBox.addItem ("5", 5);
    timeSigNumeratorBox.addItem ("6", 6);
    timeSigNumeratorBox.addItem ("7", 7);
    timeSigNumeratorBox.addItem ("8", 8);
    timeSigNumeratorBox.addItem ("9", 9);
    timeSigNumeratorBox.addItem ("10", 10);
    timeSigNumeratorBox.addItem ("11", 11);
    timeSigNumeratorBox.addItem ("12", 12);
    timeSigNumeratorBox.setSelectedId (4, juce::dontSendNotification);

    timeSigDenominatorBox.addItem ("2", 2);
    timeSigDenominatorBox.addItem ("4", 4);
    timeSigDenominatorBox.addItem ("8", 8);
    timeSigDenominatorBox.addItem ("16", 16);
    timeSigDenominatorBox.setSelectedId (4, juce::dontSendNotification);

    snapResolutionBox.addItem ("Bar", 1);
    snapResolutionBox.addItem ("Beat", 2);
    snapResolutionBox.addItem ("1/2", 3);
    snapResolutionBox.addItem ("1/4", 4);
    snapResolutionBox.setSelectedId (2, juce::dontSendNotification);

    snapToggle.setToggleState (true, juce::dontSendNotification);
    barsBeatsToggle.setToggleState (true, juce::dontSendNotification);

    positionLabel.setText ("0:00.0", juce::dontSendNotification);
    positionLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (addTrackButton);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (punchButton);
    addAndMakeVisible (setLoopInButton);
    addAndMakeVisible (setLoopOutButton);
    addAndMakeVisible (tempoLabel);
    addAndMakeVisible (tempoSlider);
    addAndMakeVisible (timeSigLabel);
    addAndMakeVisible (timeSigNumeratorBox);
    addAndMakeVisible (timeSigDenominatorBox);
    addAndMakeVisible (addTempoMarkerButton);
    addAndMakeVisible (addTimeSigMarkerButton);
    addAndMakeVisible (snapToggle);
    addAndMakeVisible (snapResolutionBox);
    addAndMakeVisible (barsBeatsToggle);
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

    tempoSlider.onValueChange = [this]
    {
        if (suppressControlCallbacks)
            return;
        applyTempo (tempoSlider.getValue(), true);
    };
    tempoSlider.onDragEnd = [this] { editSession.endCoalescedTransaction(); };

    timeSigNumeratorBox.onChange = [this]
    {
        if (suppressControlCallbacks)
            return;
        applyTimeSignature (getSelectedTimeSigNumerator(), getSelectedTimeSigDenominator());
    };
    timeSigDenominatorBox.onChange = [this]
    {
        if (suppressControlCallbacks)
            return;
        applyTimeSignature (getSelectedTimeSigNumerator(), getSelectedTimeSigDenominator());
    };

    addTempoMarkerButton.onClick = [this]
    {
        if (suppressControlCallbacks)
            return;
        insertTempoMarkerAtPlayheadForTesting (tempoSlider.getValue());
    };

    addTimeSigMarkerButton.onClick = [this]
    {
        if (suppressControlCallbacks)
            return;
        insertTimeSigMarkerAtPlayheadForTesting (getSelectedTimeSigNumerator(), getSelectedTimeSigDenominator());
    };

    loopButton.onClick = [this]
    {
        if (suppressControlCallbacks)
            return;
        editSession.getEdit().getTransport().looping = loopButton.getToggleState();
    };

    punchButton.onClick = [this]
    {
        if (suppressControlCallbacks)
            return;
        editSession.getEdit().recordingPunchInOut = punchButton.getToggleState();
    };

    setLoopInButton.onClick = [this]
    {
        auto& transport = editSession.getEdit().getTransport();
        transport.setLoopIn (transport.getPosition());
        transport.looping = true;
    };

    setLoopOutButton.onClick = [this]
    {
        auto& transport = editSession.getEdit().getTransport();
        transport.setLoopOut (transport.getPosition());
        transport.looping = true;
    };

    // Timeline
    timeline = std::make_unique<TimelineComponent> (editSession);
    addAndMakeVisible (timeline.get());

    snapToggle.onClick = [this]
    {
        if (suppressControlCallbacks)
            return;
        timeline->setSnapEnabled (snapToggle.getToggleState());
    };

    snapResolutionBox.onChange = [this]
    {
        if (suppressControlCallbacks)
            return;

        auto resolution = TimelineComponent::SnapResolution::beat;
        switch (snapResolutionBox.getSelectedId())
        {
            case 1: resolution = TimelineComponent::SnapResolution::bar; break;
            case 2: resolution = TimelineComponent::SnapResolution::beat; break;
            case 3: resolution = TimelineComponent::SnapResolution::halfBeat; break;
            case 4: resolution = TimelineComponent::SnapResolution::quarterBeat; break;
            default: break;
        }
        timeline->setSnapResolution (resolution);
    };

    barsBeatsToggle.onClick = [this]
    {
        if (suppressControlCallbacks)
            return;
        timeline->setShowBarsBeatsRuler (barsBeatsToggle.getToggleState());
    };

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
    auto toolbar = bounds.removeFromTop (68);
    toolbar = toolbar.reduced (8, 4);
    auto topRow = toolbar.removeFromTop (28);
    auto bottomRow = toolbar.removeFromTop (28);

    playButton.setBounds (topRow.removeFromLeft (56));
    topRow.removeFromLeft (4);
    stopButton.setBounds (topRow.removeFromLeft (56));
    topRow.removeFromLeft (4);
    recordButton.setBounds (topRow.removeFromLeft (56));
    topRow.removeFromLeft (8);
    addTrackButton.setBounds (topRow.removeFromLeft (78));
    topRow.removeFromLeft (8);
    positionLabel.setBounds (topRow.removeFromLeft (92));
    topRow.removeFromLeft (10);
    tempoLabel.setBounds (topRow.removeFromLeft (40));
    tempoSlider.setBounds (topRow.removeFromLeft (180));
    topRow.removeFromLeft (8);
    timeSigLabel.setBounds (topRow.removeFromLeft (26));
    timeSigNumeratorBox.setBounds (topRow.removeFromLeft (52));
    topRow.removeFromLeft (2);
    timeSigDenominatorBox.setBounds (topRow.removeFromLeft (52));
    topRow.removeFromLeft (6);
    addTempoMarkerButton.setBounds (topRow.removeFromLeft (72));
    topRow.removeFromLeft (4);
    addTimeSigMarkerButton.setBounds (topRow.removeFromLeft (60));

    loopButton.setBounds (bottomRow.removeFromLeft (60));
    bottomRow.removeFromLeft (4);
    punchButton.setBounds (bottomRow.removeFromLeft (68));
    bottomRow.removeFromLeft (6);
    setLoopInButton.setBounds (bottomRow.removeFromLeft (62));
    bottomRow.removeFromLeft (4);
    setLoopOutButton.setBounds (bottomRow.removeFromLeft (62));
    bottomRow.removeFromLeft (14);
    snapToggle.setBounds (bottomRow.removeFromLeft (64));
    bottomRow.removeFromLeft (4);
    snapResolutionBox.setBounds (bottomRow.removeFromLeft (84));
    bottomRow.removeFromLeft (8);
    barsBeatsToggle.setBounds (bottomRow.removeFromLeft (58));

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

    juce::ScopedValueSetter<bool> sv (suppressControlCallbacks, true);

    auto& seq = editSession.getEdit().tempoSequence;
    if (auto* tempo0 = seq.getTempo (0))
        tempoSlider.setValue (tempo0->getBpm(), juce::dontSendNotification);

    if (auto* sig0 = seq.getTimeSig (0))
    {
        timeSigNumeratorBox.setSelectedId (sig0->numerator.get(), juce::dontSendNotification);
        timeSigDenominatorBox.setSelectedId (sig0->denominator.get(), juce::dontSendNotification);
    }

    loopButton.setToggleState (transport.looping.get(), juce::dontSendNotification);
    punchButton.setToggleState (editSession.getEdit().recordingPunchInOut.get(), juce::dontSendNotification);
    snapToggle.setToggleState (timeline->isSnapEnabled(), juce::dontSendNotification);
    barsBeatsToggle.setToggleState (timeline->getShowBarsBeatsRuler(), juce::dontSendNotification);

    switch (timeline->getSnapResolution())
    {
        case TimelineComponent::SnapResolution::bar:
            snapResolutionBox.setSelectedId (1, juce::dontSendNotification);
            break;
        case TimelineComponent::SnapResolution::beat:
            snapResolutionBox.setSelectedId (2, juce::dontSendNotification);
            break;
        case TimelineComponent::SnapResolution::halfBeat:
            snapResolutionBox.setSelectedId (3, juce::dontSendNotification);
            break;
        case TimelineComponent::SnapResolution::quarterBeat:
            snapResolutionBox.setSelectedId (4, juce::dontSendNotification);
            break;
    }
}

void SessionComponent::runTransportAction (const juce::String& action)
{
    auto cmd = makeAction (action);
    (void) runCommand (commandHandler, cmd);
}

void SessionComponent::applyTempo (double bpm, bool coalesce)
{
    const auto clampedBpm = juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm);
    editSession.performEdit ("Set Tempo", coalesce, [clampedBpm] (te::Edit& edit)
    {
        if (auto* tempo0 = edit.tempoSequence.getTempo (0))
            tempo0->setBpm (clampedBpm);
    });
}

void SessionComponent::applyTimeSignature (int numerator, int denominator)
{
    numerator = juce::jlimit (1, 32, numerator);
    denominator = juce::jmax (1, denominator);

    editSession.performEdit ("Set Time Signature", [numerator, denominator] (te::Edit& edit)
    {
        if (auto* sig0 = edit.tempoSequence.getTimeSig (0))
            sig0->setStringTimeSig (juce::String (numerator) + "/" + juce::String (denominator));
    });
}

int SessionComponent::getSelectedTimeSigNumerator() const
{
    auto n = timeSigNumeratorBox.getSelectedId();
    return n > 0 ? n : 4;
}

int SessionComponent::getSelectedTimeSigDenominator() const
{
    auto d = timeSigDenominatorBox.getSelectedId();
    return d > 0 ? d : 4;
}

void SessionComponent::setTempoForTesting (double bpm)
{
    applyTempo (bpm, false);
}

void SessionComponent::setTimeSignatureForTesting (int numerator, int denominator)
{
    applyTimeSignature (numerator, denominator);
}

void SessionComponent::insertTempoMarkerAtPlayheadForTesting (double bpm)
{
    const auto markerBpm = juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm);

    editSession.performEdit ("Insert Tempo Marker", [this, markerBpm] (te::Edit& edit)
    {
        const auto markerPos = edit.getTransport().getPosition();
        if (auto tempo = edit.tempoSequence.insertTempo (markerPos))
            tempo->setBpm (markerBpm);
    });
}

void SessionComponent::insertTimeSigMarkerAtPlayheadForTesting (int numerator, int denominator)
{
    numerator = juce::jlimit (1, 32, numerator);
    denominator = juce::jmax (1, denominator);

    editSession.performEdit ("Insert Time Signature Marker", [this, numerator, denominator] (te::Edit& edit)
    {
        const auto markerPos = edit.getTransport().getPosition();
        if (auto sig = edit.tempoSequence.insertTimeSig (markerPos))
            sig->setStringTimeSig (juce::String (numerator) + "/" + juce::String (denominator));
    });
}

void SessionComponent::setSnapForTesting (bool enabled, TimelineComponent::SnapResolution resolution)
{
    timeline->setSnapEnabled (enabled);
    timeline->setSnapResolution (resolution);
}

double SessionComponent::snapTimeForTesting (double seconds) const
{
    return timeline->snapTimeToGrid (seconds);
}

void SessionComponent::setLoopEnabledForTesting (bool enabled)
{
    editSession.getEdit().getTransport().looping = enabled;
}

void SessionComponent::setLoopRangeForTesting (double loopInSeconds, double loopOutSeconds)
{
    auto start = te::TimePosition::fromSeconds (juce::jmax (0.0, juce::jmin (loopInSeconds, loopOutSeconds)));
    auto end = te::TimePosition::fromSeconds (juce::jmax (loopInSeconds, loopOutSeconds));
    editSession.getEdit().getTransport().setLoopRange ({ start, end });
}

void SessionComponent::setPunchEnabledForTesting (bool enabled)
{
    editSession.getEdit().recordingPunchInOut = enabled;
}
