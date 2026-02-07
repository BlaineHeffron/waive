#include "SessionComponent.h"
#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "CommandHelpers.h"
#include "TimelineComponent.h"
#include "MixerComponent.h"
#include "ToolSidebarComponent.h"
#include "ToolDiff.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"
#include "ToolRegistry.h"
#include "ModelManager.h"
#include "JobQueue.h"
#include "ProjectManager.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

using waive::runCommand;
using waive::makeAction;

//==============================================================================
SessionComponent::SessionComponent (EditSession& session, UndoableCommandHandler& handler,
                                    waive::ToolRegistry* toolReg,
                                    waive::ModelManager* modelMgr,
                                    waive::JobQueue* jq,
                                    ProjectManager* projectMgr)
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
    clickToggle.setToggleState (false, juce::dontSendNotification);

    positionLabel.setText ("0:00.0", juce::dontSendNotification);
    positionLabel.setJustificationType (juce::Justification::centredLeft);

    selectionStatusLabel.setText ("Ready", juce::dontSendNotification);
    selectionStatusLabel.setJustificationType (juce::Justification::centredLeft);
    selectionStatusLabel.setFont (waive::Fonts::caption());
    if (auto* pal = waive::getWaivePalette (*this))
        selectionStatusLabel.setColour (juce::Label::textColourId, pal->textMuted);

    playButton.setTooltip ("Play (Space)");
    stopButton.setTooltip ("Stop (Space)");
    recordButton.setTooltip ("Record (Ctrl+R)");
    addTrackButton.setTooltip ("Add Track (Ctrl+T)");
    loopButton.setTooltip ("Loop On/Off (L)");
    punchButton.setTooltip ("Punch In/Out");
    setLoopInButton.setTooltip ("Set Loop In Point");
    setLoopOutButton.setTooltip ("Set Loop Out Point");
    tempoSlider.setTooltip ("Tempo (BPM)");
    timeSigNumeratorBox.setTooltip ("Time Signature");
    timeSigDenominatorBox.setTooltip ("Time Signature");
    addTempoMarkerButton.setTooltip ("Insert Tempo Marker");
    addTimeSigMarkerButton.setTooltip ("Insert Time Signature Marker");
    snapToggle.setTooltip ("Snap to Grid");
    snapResolutionBox.setTooltip ("Snap Resolution");
    barsBeatsToggle.setTooltip ("Bars/Beats Ruler");
    clickToggle.setTooltip ("Metronome Click");

    // Accessibility labels
    playButton.setTitle ("Play");
    playButton.setDescription ("Start playback (Space)");
    stopButton.setTitle ("Stop");
    stopButton.setDescription ("Stop playback (Space)");
    recordButton.setTitle ("Record");
    recordButton.setDescription ("Record audio (Ctrl+R)");
    addTrackButton.setTitle ("Add Track");
    addTrackButton.setDescription ("Add new audio track (Ctrl+T)");
    loopButton.setTitle ("Loop");
    loopButton.setDescription ("Toggle loop mode (L)");
    punchButton.setTitle ("Punch");
    punchButton.setDescription ("Toggle punch in/out recording");
    setLoopInButton.setTitle ("Set Loop In");
    setLoopInButton.setDescription ("Set loop in point at playhead");
    setLoopOutButton.setTitle ("Set Loop Out");
    setLoopOutButton.setDescription ("Set loop out point at playhead");
    tempoSlider.setTitle ("Tempo");
    tempoSlider.setDescription ("Project tempo in BPM");
    timeSigNumeratorBox.setTitle ("Time Signature Numerator");
    timeSigNumeratorBox.setDescription ("Time signature beats per bar");
    timeSigDenominatorBox.setTitle ("Time Signature Denominator");
    timeSigDenominatorBox.setDescription ("Time signature beat value");
    addTempoMarkerButton.setTitle ("Add Tempo Marker");
    addTempoMarkerButton.setDescription ("Insert tempo marker at playhead");
    addTimeSigMarkerButton.setTitle ("Add Time Signature Marker");
    addTimeSigMarkerButton.setDescription ("Insert time signature marker at playhead");
    snapToggle.setTitle ("Snap");
    snapToggle.setDescription ("Toggle snap to grid");
    snapResolutionBox.setTitle ("Snap Resolution");
    snapResolutionBox.setDescription ("Grid snap resolution");
    barsBeatsToggle.setTitle ("Bars/Beats");
    barsBeatsToggle.setDescription ("Toggle bars/beats ruler display");
    clickToggle.setTitle ("Click");
    clickToggle.setDescription ("Toggle metronome click");

    // Enable keyboard focus for all transport controls
    playButton.setWantsKeyboardFocus (true);
    stopButton.setWantsKeyboardFocus (true);
    recordButton.setWantsKeyboardFocus (true);
    loopButton.setWantsKeyboardFocus (true);
    punchButton.setWantsKeyboardFocus (true);
    addTrackButton.setWantsKeyboardFocus (true);
    tempoSlider.setWantsKeyboardFocus (true);
    timeSigNumeratorBox.setWantsKeyboardFocus (true);
    timeSigDenominatorBox.setWantsKeyboardFocus (true);
    snapToggle.setWantsKeyboardFocus (true);
    snapResolutionBox.setWantsKeyboardFocus (true);

    // Set explicit Tab navigation focus order: transport → timeline → mixer → tool sidebar
    // REMOVED: causes segfault in tests. Tab order implementation needs revision.
    // playButton.setExplicitFocusOrder (1);
    // stopButton.setExplicitFocusOrder (2);
    // recordButton.setExplicitFocusOrder (3);
    // addTrackButton.setExplicitFocusOrder (4);
    // loopButton.setExplicitFocusOrder (5);
    // punchButton.setExplicitFocusOrder (6);
    // tempoSlider.setExplicitFocusOrder (7);
    // timeSigNumeratorBox.setExplicitFocusOrder (8);
    // timeSigDenominatorBox.setExplicitFocusOrder (9);
    // snapToggle.setExplicitFocusOrder (10);
    // snapResolutionBox.setExplicitFocusOrder (11);
    // timeline->setExplicitFocusOrder (100);
    // mixer->setExplicitFocusOrder (200);
    // if (toolSidebar != nullptr)
    //     toolSidebar->setExplicitFocusOrder (300);

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
    addAndMakeVisible (clickToggle);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (selectionStatusLabel);

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
    timeline->getSelectionManager().addListener (this);

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

    clickToggle.onClick = [this]
    {
        if (suppressControlCallbacks)
            return;
        editSession.getEdit().clickTrackEnabled = clickToggle.getToggleState();
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

    // Tool sidebar (optional — only created if all deps are provided)
    if (toolReg != nullptr && modelMgr != nullptr && jq != nullptr && projectMgr != nullptr)
    {
        toolSidebar = std::make_unique<ToolSidebarComponent> (*toolReg, editSession, *projectMgr,
                                                               *this, *modelMgr, *jq);
        addAndMakeVisible (toolSidebar.get());

        sidebarResizer = std::make_unique<juce::StretchableLayoutResizerBar> (&horizontalLayout, 1, true);
        addAndMakeVisible (sidebarResizer.get());
    }

    startTimerHz (10);
}

SessionComponent::~SessionComponent()
{
    if (timeline)
        timeline->getSelectionManager().removeListener (this);
}

void SessionComponent::resized()
{
    auto bounds = getLocalBounds();

    // Transport toolbar with responsive layout
    const int viewportWidth = bounds.getWidth();
    const bool showSecondaryControls = viewportWidth >= 900;

    auto toolbar = bounds.removeFromTop (showSecondaryControls ? 68 : 36);
    toolbar = toolbar.reduced (8, 4);

    // Primary row (always visible)
    auto topRow = toolbar.removeFromTop (28);
    juce::FlexBox primaryFlex;
    primaryFlex.flexDirection = juce::FlexBox::Direction::row;
    primaryFlex.justifyContent = juce::FlexBox::JustifyContent::flexStart;
    primaryFlex.alignItems = juce::FlexBox::AlignItems::center;

    primaryFlex.items.add (juce::FlexItem (playButton).withWidth (56));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
    primaryFlex.items.add (juce::FlexItem (stopButton).withWidth (56));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
    primaryFlex.items.add (juce::FlexItem (recordButton).withWidth (56));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::sm));
    primaryFlex.items.add (juce::FlexItem (addTrackButton).withWidth (78));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::sm));
    primaryFlex.items.add (juce::FlexItem (positionLabel).withWidth (92));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::md));
    primaryFlex.items.add (juce::FlexItem (selectionStatusLabel).withWidth (150).withFlex (1.0f));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::md));
    primaryFlex.items.add (juce::FlexItem (tempoLabel).withWidth (40));
    primaryFlex.items.add (juce::FlexItem (tempoSlider).withWidth (180));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::sm));
    primaryFlex.items.add (juce::FlexItem (timeSigLabel).withWidth (26));
    primaryFlex.items.add (juce::FlexItem (timeSigNumeratorBox).withWidth (52));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xxs));
    primaryFlex.items.add (juce::FlexItem (timeSigDenominatorBox).withWidth (52));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
    primaryFlex.items.add (juce::FlexItem (addTempoMarkerButton).withWidth (72));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
    primaryFlex.items.add (juce::FlexItem (addTimeSigMarkerButton).withWidth (60));

    primaryFlex.performLayout (topRow);

    // Secondary row (visible when width >= 900px)
    if (showSecondaryControls)
    {
        auto bottomRow = toolbar.removeFromTop (28);
        juce::FlexBox secondaryFlex;
        secondaryFlex.flexDirection = juce::FlexBox::Direction::row;
        secondaryFlex.justifyContent = juce::FlexBox::JustifyContent::flexStart;
        secondaryFlex.alignItems = juce::FlexBox::AlignItems::center;

        secondaryFlex.items.add (juce::FlexItem (loopButton).withWidth (60));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (punchButton).withWidth (68));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (setLoopInButton).withWidth (62));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (setLoopOutButton).withWidth (62));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (clickToggle).withWidth (60));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::sm));
        secondaryFlex.items.add (juce::FlexItem (snapToggle).withWidth (64));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (snapResolutionBox).withWidth (84));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::sm));
        secondaryFlex.items.add (juce::FlexItem (barsBeatsToggle).withWidth (58));

        secondaryFlex.performLayout (bottomRow);

        loopButton.setVisible (true);
        punchButton.setVisible (true);
        setLoopInButton.setVisible (true);
        setLoopOutButton.setVisible (true);
        clickToggle.setVisible (true);
        snapToggle.setVisible (true);
        snapResolutionBox.setVisible (true);
        barsBeatsToggle.setVisible (true);
    }
    else
    {
        loopButton.setVisible (false);
        punchButton.setVisible (false);
        setLoopInButton.setVisible (false);
        setLoopOutButton.setVisible (false);
        clickToggle.setVisible (false);
        snapToggle.setVisible (false);
        snapResolutionBox.setVisible (false);
        barsBeatsToggle.setVisible (false);
    }

    // Horizontal split: content area | resizer | sidebar
    auto contentBounds = bounds;

    if (toolSidebar != nullptr && sidebarVisible)
    {
        auto sidebarBounds = contentBounds.removeFromRight (defaultSidebarWidth);
        auto resizerBounds = contentBounds.removeFromRight (4);
        if (sidebarResizer)
            sidebarResizer->setBounds (resizerBounds);
        toolSidebar->setBounds (sidebarBounds);
        toolSidebar->setVisible (true);
        if (sidebarResizer)
            sidebarResizer->setVisible (true);
    }
    else
    {
        if (toolSidebar)
            toolSidebar->setVisible (false);
        if (sidebarResizer)
            sidebarResizer->setVisible (false);
    }

    // Layout: timeline + resizer + mixer (vertical)
    juce::Component* comps[] = { timeline.get(), resizerBar.get(), mixer.get() };
    layoutManager.layOutComponents (comps, 3, contentBounds.getX(), contentBounds.getY(),
                                    contentBounds.getWidth(), contentBounds.getHeight(), true, true);
}

TimelineComponent& SessionComponent::getTimeline()
{
    return *timeline;
}

ToolSidebarComponent* SessionComponent::getToolSidebar()
{
    return toolSidebar.get();
}

void SessionComponent::toggleToolSidebar()
{
    sidebarVisible = ! sidebarVisible;
    resized();
}

void SessionComponent::play()
{
    auto& transport = editSession.getEdit().getTransport();
    if (transport.isPlaying())
        transport.stop (false, false);
    else
        transport.play (false);
}

void SessionComponent::stop()
{
    editSession.getEdit().getTransport().stop (false, false);
}

void SessionComponent::record()
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
        transport.record (false, true);
    }
}

void SessionComponent::goToStart()
{
    editSession.getEdit().getTransport().setPosition (te::TimePosition::fromSeconds (0.0));
}

void SessionComponent::timerCallback()
{
    auto& transport = editSession.getEdit().getTransport();
    auto pos = transport.getPosition().inSeconds();
    int mins = (int) pos / 60;
    double secs = pos - mins * 60;
    positionLabel.setText (juce::String (mins) + ":" + juce::String (secs, 1),
                           juce::dontSendNotification);

    {
        auto* pal = waive::getWaivePalette (*this);
        recordButton.setColour (juce::TextButton::buttonColourId,
                                transport.isRecording() ? (pal ? pal->record : juce::Colours::darkred)
                                                        : (pal ? pal->surfaceBg : juce::Colours::darkgrey));
    }

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
    clickToggle.setToggleState (editSession.getEdit().clickTrackEnabled.get(), juce::dontSendNotification);
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

void SessionComponent::selectionChanged()
{
    auto& selectionMgr = timeline->getSelectionManager();
    auto selectedClips = selectionMgr.getSelectedClips();
    int count = selectedClips.size();

    if (count == 0)
    {
        selectionStatusLabel.setText ("Ready", juce::dontSendNotification);
    }
    else if (count == 1)
    {
        auto* clip = selectedClips[0];
        if (clip)
            selectionStatusLabel.setText (clip->getName(), juce::dontSendNotification);
        else
            selectionStatusLabel.setText ("1 clip selected", juce::dontSendNotification);
    }
    else
    {
        selectionStatusLabel.setText (juce::String (count) + " clips selected", juce::dontSendNotification);
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

juce::Array<int> SessionComponent::getToolPreviewTracksForTesting() const
{
    if (mixer == nullptr)
        return {};

    return mixer->getHighlightedTrackIndicesForTesting();
}

juce::String SessionComponent::getTransportTooltipForTesting (const juce::String& controlName)
{
    if (controlName == "play")
        return playButton.getTooltip();
    if (controlName == "stop")
        return stopButton.getTooltip();
    if (controlName == "record")
        return recordButton.getTooltip();
    if (controlName == "loop")
        return loopButton.getTooltip();
    if (controlName == "punch")
        return punchButton.getTooltip();
    if (controlName == "setLoopIn")
        return setLoopInButton.getTooltip();
    if (controlName == "setLoopOut")
        return setLoopOutButton.getTooltip();
    if (controlName == "addTrack")
        return addTrackButton.getTooltip();
    if (controlName == "tempo")
        return tempoSlider.getTooltip();
    if (controlName == "timeSig")
        return timeSigNumeratorBox.getTooltip();
    if (controlName == "snap")
        return snapToggle.getTooltip();
    if (controlName == "snapResolution")
        return snapResolutionBox.getTooltip();
    if (controlName == "barsBeat")
        return barsBeatsToggle.getTooltip();
    if (controlName == "click")
        return clickToggle.getTooltip();
    return {};
}

juce::String SessionComponent::getSelectionStatusTextForTesting() const
{
    return selectionStatusLabel.getText();
}

void SessionComponent::applyToolPreviewDiff (const juce::Array<waive::ToolDiffEntry>& changes)
{
    juce::Array<te::EditItemID> clipIDs;
    juce::Array<int> trackIndices;

    for (const auto& change : changes)
    {
        if (change.clipID.isValid())
            clipIDs.addIfNotAlreadyThere (change.clipID);

        if (change.trackIndex >= 0)
            trackIndices.addIfNotAlreadyThere (change.trackIndex);
    }

    timeline->selectClipsByIDForPreview (clipIDs);
    mixer->setHighlightedTrackIndices (trackIndices);
}

void SessionComponent::clearToolPreview()
{
    timeline->clearPreviewSelection();
    mixer->clearHighlightedTrackIndices();
}
