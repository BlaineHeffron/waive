#include "SessionComponent.h"
#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "CommandHelpers.h"
#include "TimelineComponent.h"
#include "MixerComponent.h"
#include "ToolSidebarComponent.h"
#include "ChatPanelComponent.h"
#include "PianoRollComponent.h"
#include "ToolDiff.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"
#include "ToolRegistry.h"
#include "ModelManager.h"
#include "JobQueue.h"
#include "ProjectManager.h"
#include "AiAgent.h"
#include "AiSettings.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

using waive::runCommand;
using waive::makeAction;

//==============================================================================
SessionComponent::SessionComponent (EditSession& session, UndoableCommandHandler& handler,
                                    waive::ToolRegistry* toolReg,
                                    waive::ModelManager* modelMgr,
                                    waive::JobQueue* jq,
                                    ProjectManager* projectMgr,
                                    waive::AiAgent* aiAgent,
                                    waive::AiSettings* aiSettings)
    : editSession (session), commandHandler (handler)
{
    playButton.setButtonText ("Play");
    stopButton.setButtonText ("Stop");
    recordButton.setButtonText ("Start Rec");
    stopRecordButton.setButtonText ("Stop Rec");
    recordFromMicButton.setButtonText ("Mic Rec");
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

    recordStatusLabel.setText ("Idle", juce::dontSendNotification);
    recordStatusLabel.setJustificationType (juce::Justification::centredLeft);
    recordStatusLabel.setFont (waive::Fonts::caption().boldened());

    selectionStatusLabel.setText ("Ready", juce::dontSendNotification);
    selectionStatusLabel.setJustificationType (juce::Justification::centredLeft);
    selectionStatusLabel.setFont (waive::Fonts::caption());
    if (auto* pal = waive::getWaivePalette (*this))
        selectionStatusLabel.setColour (juce::Label::textColourId, pal->textMuted);

    playButton.setTooltip ("Play (Space)");
    stopButton.setTooltip ("Stop (Space)");
    recordButton.setTooltip ("Start recording (Ctrl+R)");
    stopRecordButton.setTooltip ("Stop recording");
    recordFromMicButton.setTooltip ("Record from selected microphone (auto-creates armed track)");
    recordStatusLabel.setTooltip ("Current recording status");
    micInputCombo.setTooltip ("Microphone input device used by Mic Rec");
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
    recordButton.setDescription ("Start recording audio (Ctrl+R)");
    stopRecordButton.setTitle ("Stop Record");
    stopRecordButton.setDescription ("Stop recording audio");
    recordFromMicButton.setTitle ("Mic Rec");
    recordFromMicButton.setDescription ("Quick-record from selected microphone");
    recordStatusLabel.setTitle ("Record Status");
    recordStatusLabel.setDescription ("Shows whether transport is recording");
    micInputCombo.setTitle ("Mic Input");
    micInputCombo.setDescription ("Select microphone input device");
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
    stopRecordButton.setWantsKeyboardFocus (true);
    recordFromMicButton.setWantsKeyboardFocus (true);
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
    addAndMakeVisible (stopRecordButton);
    addAndMakeVisible (recordFromMicButton);
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
    addAndMakeVisible (recordStatusLabel);
    addAndMakeVisible (selectionStatusLabel);
    addAndMakeVisible (micInputLabel);
    addAndMakeVisible (micInputCombo);

    playButton.onClick  = [this] { runTransportAction ("transport_play"); };
    stopButton.onClick  = [this] { stop(); };
    recordButton.onClick = [this] { record(); };
    stopRecordButton.onClick = [this] { stopRecording(); };

    recordFromMicButton.onClick = [this] { recordFromMic(); };

    // Style recordFromMicButton with danger color
    if (auto* pal = waive::getWaivePalette (*this))
    {
        stopRecordButton.setColour (juce::TextButton::buttonColourId, pal->record);
        recordFromMicButton.setColour (juce::TextButton::buttonColourId, pal->danger);
        recordStatusLabel.setColour (juce::Label::textColourId, pal->textMuted);
    }

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

    refreshMicInputDevices();

    // Mixer
    mixer = std::make_unique<MixerComponent> (editSession);
    addAndMakeVisible (mixer.get());

    // Resizer bar between timeline and mixer
    resizerBar = std::make_unique<juce::StretchableLayoutResizerBar> (&layoutManager, 1, false);
    addAndMakeVisible (resizerBar.get());

    // Horizontal split host: content | resizer | sidebar
    horizontalLayout.setItemLayout (0, 160, -1.0, -0.75);
    horizontalLayout.setItemLayout (1,
                                    waive::Spacing::resizerBarThickness,
                                    waive::Spacing::resizerBarThickness,
                                    waive::Spacing::resizerBarThickness);
    horizontalLayout.setItemLayout (2, 180, 520, defaultSidebarWidth);

    contentArea.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (contentArea);
    contentArea.toBack();

    // Tool sidebar (optional — only created if all deps are provided)
    if (toolReg != nullptr && modelMgr != nullptr && jq != nullptr && projectMgr != nullptr)
    {
        toolSidebar = std::make_unique<ToolSidebarComponent> (*toolReg, editSession, *projectMgr,
                                                               *this, *modelMgr, *jq);
        addAndMakeVisible (toolSidebar.get());

        sidebarResizer = std::make_unique<juce::StretchableLayoutResizerBar> (&horizontalLayout, 1, true);
        addAndMakeVisible (sidebarResizer.get());
    }

    // Chat panel (optional — only created if AI agent is provided)
    if (aiAgent != nullptr && aiSettings != nullptr)
    {
        chatPanel = std::make_unique<waive::ChatPanelComponent> (*aiAgent, *aiSettings);
        addChildComponent (chatPanel.get());  // hidden by default (popout)

        chatClipButton.setTooltip ("Clip/Unclip AI chat panel to side");
        chatClipButton.onClick = [this]
        {
            chatPanelClippedToSide = ! chatPanelClippedToSide;
            resized();
        };
        addChildComponent (chatClipButton);

        chatCloseButton.setTooltip ("Close AI chat panel");
        chatCloseButton.onClick = [this]
        {
            chatPanelVisible = false;
            resized();
        };
        addChildComponent (chatCloseButton);
    }

    // Piano roll panel (created on demand)
    closePianoRollButton.onClick = [this] { closePianoRoll(); };
    closePianoRollButton.setTooltip ("Close Piano Roll");
    addChildComponent (closePianoRollButton);

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
    const bool showSelectionStatus = viewportWidth >= 980;
    const bool showTempoControls = viewportWidth >= 1120;
    const bool showMarkerControls = viewportWidth >= 1320;
    const bool showMicInputControls = viewportWidth >= 1200;

    const int toolbarH = showSecondaryControls
                            ? (waive::Spacing::controlHeightDefault * 2 + waive::Spacing::md)
                            : waive::Spacing::toolbarRowHeight;
    auto toolbar = bounds.removeFromTop (toolbarH);
    toolbar = toolbar.reduced (waive::Spacing::sm, waive::Spacing::xs);

    // Primary row (always visible)
    auto topRow = toolbar.removeFromTop (waive::Spacing::controlHeightDefault);
    juce::FlexBox primaryFlex;
    primaryFlex.flexDirection = juce::FlexBox::Direction::row;
    primaryFlex.justifyContent = juce::FlexBox::JustifyContent::flexStart;
    primaryFlex.alignItems = juce::FlexBox::AlignItems::center;

    // Playback control group
    primaryFlex.items.add (juce::FlexItem (playButton).withWidth (56));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
    primaryFlex.items.add (juce::FlexItem (stopButton).withWidth (56));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
    primaryFlex.items.add (juce::FlexItem (recordButton).withWidth (76));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
    primaryFlex.items.add (juce::FlexItem (stopRecordButton).withWidth (74));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
    primaryFlex.items.add (juce::FlexItem (recordFromMicButton).withWidth (72));
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::md)); // Group separator
    primaryFlex.items.add (juce::FlexItem (addTrackButton).withWidth (78));

    // Transport status group
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::sm));
    primaryFlex.items.add (juce::FlexItem (recordStatusLabel).withWidth (90));

    // Position display group
    primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::lg)); // Group separator
    primaryFlex.items.add (juce::FlexItem (positionLabel).withWidth (92));
    if (showSelectionStatus)
    {
        primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::md));
        primaryFlex.items.add (juce::FlexItem (selectionStatusLabel).withWidth (150).withFlex (1.0f));
    }

    // Tempo/timesig group (hidden on narrower widths so record controls stay visible)
    if (showTempoControls)
    {
        primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::lg)); // Group separator
        primaryFlex.items.add (juce::FlexItem (tempoLabel).withWidth (40));
        primaryFlex.items.add (juce::FlexItem (tempoSlider).withWidth (170));
        primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::sm));
        primaryFlex.items.add (juce::FlexItem (timeSigLabel).withWidth (26));
        primaryFlex.items.add (juce::FlexItem (timeSigNumeratorBox).withWidth (52));
        primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xxs));
        primaryFlex.items.add (juce::FlexItem (timeSigDenominatorBox).withWidth (52));
    }

    // Marker group
    if (showMarkerControls)
    {
        primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::lg)); // Group separator
        primaryFlex.items.add (juce::FlexItem (addTempoMarkerButton).withWidth (72));
        primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        primaryFlex.items.add (juce::FlexItem (addTimeSigMarkerButton).withWidth (60));
    }

    primaryFlex.performLayout (topRow);

    selectionStatusLabel.setVisible (showSelectionStatus);
    tempoLabel.setVisible (showTempoControls);
    tempoSlider.setVisible (showTempoControls);
    timeSigLabel.setVisible (showTempoControls);
    timeSigNumeratorBox.setVisible (showTempoControls);
    timeSigDenominatorBox.setVisible (showTempoControls);
    addTempoMarkerButton.setVisible (showMarkerControls);
    addTimeSigMarkerButton.setVisible (showMarkerControls);

    // Secondary row (visible when width >= 900px)
    if (showSecondaryControls)
    {
        auto bottomRow = toolbar.removeFromTop (waive::Spacing::controlHeightDefault);
        juce::FlexBox secondaryFlex;
        secondaryFlex.flexDirection = juce::FlexBox::Direction::row;
        secondaryFlex.justifyContent = juce::FlexBox::JustifyContent::flexStart;
        secondaryFlex.alignItems = juce::FlexBox::AlignItems::center;

        // Loop/punch group
        secondaryFlex.items.add (juce::FlexItem (loopButton).withWidth (60));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (punchButton).withWidth (68));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (setLoopInButton).withWidth (62));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (setLoopOutButton).withWidth (62));

        // Click/snap group
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::lg)); // Group separator
        secondaryFlex.items.add (juce::FlexItem (clickToggle).withWidth (60));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::sm));
        secondaryFlex.items.add (juce::FlexItem (snapToggle).withWidth (64));
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::xs));
        secondaryFlex.items.add (juce::FlexItem (snapResolutionBox).withWidth (84));

        // Display mode group
        secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::lg)); // Group separator
        secondaryFlex.items.add (juce::FlexItem (barsBeatsToggle).withWidth (58));

        if (showMicInputControls)
        {
            secondaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::lg)); // Group separator
            secondaryFlex.items.add (juce::FlexItem (micInputLabel).withWidth (36));
            secondaryFlex.items.add (juce::FlexItem (micInputCombo).withWidth (220));
        }

        secondaryFlex.performLayout (bottomRow);

        loopButton.setVisible (true);
        punchButton.setVisible (true);
        setLoopInButton.setVisible (true);
        setLoopOutButton.setVisible (true);
        clickToggle.setVisible (true);
        snapToggle.setVisible (true);
        snapResolutionBox.setVisible (true);
        barsBeatsToggle.setVisible (true);
        micInputLabel.setVisible (showMicInputControls);
        micInputCombo.setVisible (showMicInputControls);
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
        micInputLabel.setVisible (false);
        micInputCombo.setVisible (false);
    }

    // Horizontal split: content area | resizer | sidebar
    auto contentBounds = bounds;

    if (toolSidebar != nullptr && sidebarVisible && sidebarResizer != nullptr)
    {
        juce::Component* horizontalComps[] = { &contentArea, sidebarResizer.get(), toolSidebar.get() };
        horizontalLayout.layOutComponents (horizontalComps, 3,
                                           bounds.getX(), bounds.getY(),
                                           bounds.getWidth(), bounds.getHeight(),
                                           true, true);

        contentBounds = contentArea.getBounds();
        contentArea.setVisible (true);
        toolSidebar->setVisible (true);
        sidebarResizer->setVisible (true);
    }
    else
    {
        contentArea.setBounds (contentBounds);
        contentArea.setVisible (false);
        if (toolSidebar)
            toolSidebar->setVisible (false);
        if (sidebarResizer)
            sidebarResizer->setVisible (false);
    }

    juce::Rectangle<int> clippedChatBounds;
    const bool showChatPanel = (chatPanel != nullptr && chatPanelVisible);
    const bool clipChatToSide = showChatPanel && chatPanelClippedToSide;
    if (clipChatToSide)
    {
        const int minDockWidth = 280;
        const int maxDockWidth = juce::jmax (minDockWidth, contentBounds.getWidth() - 240);
        chatPanelDockedWidth = juce::jlimit (minDockWidth, maxDockWidth, chatPanelDockedWidth);

        clippedChatBounds = contentBounds.removeFromRight (chatPanelDockedWidth);
        clippedChatBounds.reduce (0, waive::Spacing::xxs);
    }

    PanelLayoutMode desiredMode = PanelLayoutMode::timelineMixer;
    if (pianoRollPanel != nullptr && pianoRollVisible)
        desiredMode = PanelLayoutMode::timelinePianoMixer;
    applyPanelLayoutMode (desiredMode);

    // Layout: timeline + resizer + [piano roll + resizer +] mixer (vertical)
    if (pianoRollPanel != nullptr && pianoRollVisible)
    {
        pianoRollPanel->setVisible (true);
        if (pianoRollResizerBar)
            pianoRollResizerBar->setVisible (true);

        juce::Component* comps[] = { timeline.get(), resizerBar.get(),
                                     pianoRollPanel.get(), pianoRollResizerBar.get(), mixer.get() };
        layoutManager.layOutComponents (comps, 5, contentBounds.getX(), contentBounds.getY(),
                                        contentBounds.getWidth(), contentBounds.getHeight(), true, true);

        // Position close button at top-right of piano roll
        closePianoRollButton.setBounds (pianoRollPanel->getRight() - 26, pianoRollPanel->getY() + 4, 22, 22);
    }
    else
    {
        if (pianoRollPanel)
            pianoRollPanel->setVisible (false);
        if (pianoRollResizerBar)
            pianoRollResizerBar->setVisible (false);

        juce::Component* comps[] = { timeline.get(), resizerBar.get(), mixer.get() };
        layoutManager.layOutComponents (comps, 3, contentBounds.getX(), contentBounds.getY(),
                                        contentBounds.getWidth(), contentBounds.getHeight(), true, true);
    }

    if (showChatPanel)
    {
        chatPanel->setVisible (true);
        chatClipButton.setVisible (true);
        chatCloseButton.setVisible (true);
        chatClipButton.setButtonText (clipChatToSide ? "Unclip" : "Clip Side");
        chatPanel->toFront (false);
        chatClipButton.toFront (false);
        chatCloseButton.toFront (false);

        if (clipChatToSide)
        {
            chatPanel->setBounds (clippedChatBounds);
            chatFloatingBounds = {};
        }
        else
        {
            const int minW = 360;
            const int minH = 260;
            const int desiredW = juce::jmax (minW, contentBounds.getWidth() / 3);
            const int desiredH = juce::jmax (minH, contentBounds.getHeight() / 2);

            if (chatFloatingBounds.isEmpty())
            {
                chatFloatingBounds = { contentBounds.getRight() - desiredW - waive::Spacing::md,
                                       contentBounds.getY() + waive::Spacing::md,
                                       desiredW,
                                       desiredH };
            }

            chatFloatingBounds.setWidth (juce::jmax (minW, juce::jmin (chatFloatingBounds.getWidth(), contentBounds.getWidth() - 24)));
            chatFloatingBounds.setHeight (juce::jmax (minH, juce::jmin (chatFloatingBounds.getHeight(), contentBounds.getHeight() - 24)));
            chatFloatingBounds.setPosition (juce::jlimit (contentBounds.getX(), contentBounds.getRight() - chatFloatingBounds.getWidth(),
                                                          chatFloatingBounds.getX()),
                                            juce::jlimit (contentBounds.getY(), contentBounds.getBottom() - chatFloatingBounds.getHeight(),
                                                          chatFloatingBounds.getY()));

            chatPanel->setBounds (chatFloatingBounds);
        }

        auto controlsRow = chatPanel->getBounds().removeFromTop (waive::Spacing::controlHeightSmall);
        chatCloseButton.setBounds (controlsRow.removeFromRight (waive::Spacing::xl));
        controlsRow.removeFromRight (waive::Spacing::xs);
        chatClipButton.setBounds (controlsRow.removeFromRight (96));
    }
    else if (chatPanel != nullptr)
    {
        chatPanel->setVisible (false);
        chatClipButton.setVisible (false);
        chatCloseButton.setVisible (false);
    }
}

void SessionComponent::applyPanelLayoutMode (PanelLayoutMode mode)
{
    if (panelLayoutInitialised && panelLayoutMode == mode)
        return;

    if (mode == PanelLayoutMode::timelinePianoMixer)
    {
        layoutManager.setItemLayout (0, 100, -1.0, -0.50);    // timeline
        layoutManager.setItemLayout (1, waive::Spacing::resizerBarThickness, waive::Spacing::resizerBarThickness, waive::Spacing::resizerBarThickness);
        layoutManager.setItemLayout (2, 200, 600, 300);        // piano roll
        layoutManager.setItemLayout (3, waive::Spacing::resizerBarThickness, waive::Spacing::resizerBarThickness, waive::Spacing::resizerBarThickness);
        layoutManager.setItemLayout (4, 80, 300, 160);         // mixer
    }
    else
    {
        layoutManager.setItemLayout (0, 100, -1.0, -0.75);    // timeline
        layoutManager.setItemLayout (1, waive::Spacing::resizerBarThickness, waive::Spacing::resizerBarThickness, waive::Spacing::resizerBarThickness);
        layoutManager.setItemLayout (2, 80, 300, 160);         // mixer
    }

    panelLayoutMode = mode;
    panelLayoutInitialised = true;
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

void SessionComponent::toggleChatPanel()
{
    chatPanelVisible = ! chatPanelVisible;
    if (chatPanelVisible)
        chatPanelClippedToSide = false;
    resized();
}

void SessionComponent::openPianoRoll (te::MidiClip& clip)
{
    pianoRollPanel = std::make_unique<PianoRollComponent> (clip, editSession);
    addAndMakeVisible (pianoRollPanel.get());

    pianoRollResizerBar = std::make_unique<juce::StretchableLayoutResizerBar> (&layoutManager, 3, false);
    addAndMakeVisible (pianoRollResizerBar.get());

    closePianoRollButton.setVisible (true);
    pianoRollVisible = true;
    resized();
}

void SessionComponent::closePianoRoll()
{
    pianoRollPanel.reset();
    pianoRollResizerBar.reset();
    closePianoRollButton.setVisible (false);
    pianoRollVisible = false;
    resized();
}

waive::ChatPanelComponent* SessionComponent::getChatPanelForTesting()
{
    return chatPanel.get();
}

MixerComponent& SessionComponent::getMixerForTesting()
{
    return *mixer;
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
    auto& transport = editSession.getEdit().getTransport();
    if (transport.isRecording())
        stopRecording();

    transport.stop (false, false);
}

void SessionComponent::record()
{
    auto& transport = editSession.getEdit().getTransport();
    if (transport.isRecording())
        return;

    requestMicrophoneAccess ([this] (bool granted)
    {
        if (! granted)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Microphone Access Required",
                                                     getMicAccessHelpText());
            return;
        }

        juce::String micError;
        if (! ensureMicInputReady (micError))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Microphone Not Ready",
                                                     micError);
            return;
        }

        auto& localTransport = editSession.getEdit().getTransport();
        localTransport.record (false, true);

        if (! localTransport.isRecording())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Recording Did Not Start",
                                                     "Recording could not start. Arm a track input or use Mic Rec.");
        }
    });
}

void SessionComponent::goToStart()
{
    editSession.getEdit().getTransport().setPosition (te::TimePosition::fromSeconds (0.0));
}

void SessionComponent::stopRecording()
{
    auto& transport = editSession.getEdit().getTransport();
    if (! transport.isRecording())
        return;

    editSession.performEdit ("Stop Recording", [&] (te::Edit&)
    {
        transport.stopRecording (false);
    });
}

void SessionComponent::requestMicrophoneAccess (std::function<void (bool)> callback)
{
    auto permission = juce::RuntimePermissions::recordAudio;
    if (! juce::RuntimePermissions::isRequired (permission))
    {
        callback (true);
        return;
    }

    if (juce::RuntimePermissions::isGranted (permission))
    {
        callback (true);
        return;
    }

    if (micPermissionRequestInFlight)
    {
        callback (false);
        return;
    }

    micPermissionRequestInFlight = true;
    recordStatusLabel.setText ("Requesting Mic", juce::dontSendNotification);

    auto safeThis = juce::Component::SafePointer<SessionComponent> (this);
    juce::RuntimePermissions::request (permission, [safeThis, callback = std::move (callback)] (bool granted) mutable
    {
        if (safeThis == nullptr)
            return;

        safeThis->micPermissionRequestInFlight = false;
        callback (granted);
    });
}

bool SessionComponent::ensureMicInputReady (juce::String& errorMessage)
{
    auto& dm = editSession.getEdit().engine.getDeviceManager();
    auto waveInputs = dm.getWaveInputDevices();
    if (waveInputs.empty())
    {
        errorMessage = "No wave input devices available. Connect a microphone or audio interface.";
        return false;
    }

    if (auto* current = dm.deviceManager.getCurrentAudioDevice())
    {
        if (current->getInputChannelNames().size() > 0
            && current->getActiveInputChannels().countNumberOfSetBits() == 0)
        {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            dm.deviceManager.getAudioDeviceSetup (setup);
            setup.useDefaultInputChannels = true;
            auto setupError = dm.deviceManager.setAudioDeviceSetup (setup, true);
            if (setupError.isNotEmpty())
            {
                errorMessage = "Could not enable microphone input channels: " + setupError;
                return false;
            }
        }
    }

    refreshMicInputDevices();
    if (getSelectedMicInputDevice() == nullptr)
    {
        errorMessage = "No microphone input device is selected.";
        return false;
    }

    return true;
}

juce::String SessionComponent::getMicAccessHelpText() const
{
#if JUCE_MAC
    return "Microphone access is blocked.\nOpen System Settings > Privacy & Security > Microphone and allow Waive.";
#elif JUCE_WINDOWS
    return "Microphone access is blocked.\nOpen Settings > Privacy & security > Microphone and enable access for desktop apps.";
#elif JUCE_LINUX
    return "Microphone access is blocked or unavailable.\nEnsure your audio stack (PipeWire/PulseAudio/ALSA) exposes an input and app permissions allow microphone access.";
#else
    return "Microphone access is blocked. Enable microphone permission for this app in your OS privacy settings.";
#endif
}

void SessionComponent::refreshMicInputDevices()
{
    auto& dm = editSession.getEdit().engine.getDeviceManager();
    auto waveInputs = dm.getWaveInputDevices();

    juce::String currentSelection = micInputCombo.getText();

    micInputCombo.clear (juce::dontSendNotification);
    int selectedId = 0;
    int nextId = 1;
    for (auto* input : waveInputs)
    {
        if (input == nullptr)
            continue;

        auto name = input->getName();
        micInputCombo.addItem (name, nextId);
        if (selectedId == 0 && (currentSelection.isEmpty() || currentSelection == name))
            selectedId = nextId;
        ++nextId;
    }

    if (selectedId > 0)
        micInputCombo.setSelectedId (selectedId, juce::dontSendNotification);
}

te::WaveInputDevice* SessionComponent::getSelectedMicInputDevice() const
{
    auto selectedName = micInputCombo.getText();
    if (selectedName.isEmpty())
        return nullptr;

    auto& dm = editSession.getEdit().engine.getDeviceManager();
    auto waveInputs = dm.getWaveInputDevices();
    for (auto* input : waveInputs)
    {
        if (input != nullptr && input->getName() == selectedName)
            return input;
    }

    return nullptr;
}

void SessionComponent::recordFromMic()
{
    auto& transport = editSession.getEdit().getTransport();
    if (transport.isRecording())
        return;

    requestMicrophoneAccess ([this] (bool granted)
    {
        if (! granted)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Microphone Access Required",
                                                     getMicAccessHelpText());
            return;
        }

        juce::String micError;
        if (! ensureMicInputReady (micError))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Microphone Not Ready",
                                                     micError);
            return;
        }

        auto& edit = editSession.getEdit();

        // Find first empty track or create new one
        te::AudioTrack* targetTrack = nullptr;
        for (auto* t : te::getAudioTracks (edit))
        {
            if (t->getClips().isEmpty())
            {
                targetTrack = t;
                break;
            }
        }

        if (targetTrack == nullptr)
        {
            auto trackCount = te::getAudioTracks (edit).size();
            edit.ensureNumberOfAudioTracks (trackCount + 1);
            targetTrack = te::getAudioTracks (edit).getLast();
        }

        if (targetTrack == nullptr)
            return;

        auto* waveIn = getSelectedMicInputDevice();
        if (waveIn == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "No Input Devices",
                                                     "No microphone input device is selected.");
            return;
        }

        // Assign and arm the input device, then start recording
        editSession.performEdit ("Record from Mic", [&edit, targetTrack, waveIn] (te::Edit&)
        {
            edit.getTransport().ensureContextAllocated();
            (void) edit.getEditInputDevices().getInstanceStateForInputDevice (*waveIn);

            if (auto* epc = edit.getCurrentPlaybackContext())
            {
                if (auto* idi = epc->getInputFor (waveIn))
                {
                    if (!idi->setTarget (targetTrack->itemID, false, &edit.getUndoManager()))
                    {
                        DBG ("Failed to assign input device to track");
                    }
                    idi->setRecordingEnabled (targetTrack->itemID, true);
                }
            }

            // Start recording after arming is complete
            edit.getTransport().record (false, true);
        });

        if (! edit.getTransport().isRecording())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Recording Did Not Start",
                                                     "Could not start microphone recording. Check input and audio device settings.");
        }
    });
}

void SessionComponent::timerCallback()
{
    auto& transport = editSession.getEdit().getTransport();
    auto pos = transport.getPosition().inSeconds();
    int mins = (int) pos / 60;
    double secs = pos - mins * 60;
    positionLabel.setText (juce::String::formatted ("%d:%.1f", mins, secs),
                           juce::dontSendNotification);

    const bool isRecording = transport.isRecording();

    {
        auto* pal = waive::getWaivePalette (*this);
        if (pal != nullptr)
        {
            recordStatusLabel.setColour (juce::Label::textColourId, isRecording ? pal->record : pal->textMuted);
            stopRecordButton.setColour (juce::TextButton::buttonColourId,
                                        isRecording ? pal->record.brighter (0.08f) : pal->record);
        }
    }

    const bool permissionRequired = juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio);
    const bool permissionGranted = (! permissionRequired) || juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio);
    const bool hasMicInput = micInputCombo.getNumItems() > 0;

    juce::String recordStatusText = "Mic Ready";
    if (micPermissionRequestInFlight)
        recordStatusText = "Requesting Mic";
    else if (! permissionGranted)
        recordStatusText = "Permission Required";
    else if (! hasMicInput)
        recordStatusText = "No Mic Input";
    else if (isRecording)
        recordStatusText = "Recording";

    recordStatusLabel.setText (recordStatusText, juce::dontSendNotification);
    recordButton.setEnabled (! isRecording);
    stopRecordButton.setEnabled (isRecording);
    recordFromMicButton.setEnabled (! isRecording);
    micInputCombo.setEnabled (! isRecording && hasMicInput);

    ++micInputRefreshTicks;
    if (micInputRefreshTicks >= 20)
    {
        micInputRefreshTicks = 0;
        refreshMicInputDevices();
    }

    juce::ScopedValueSetter<bool> sv (suppressControlCallbacks, true);

    auto& seq = editSession.getEdit().tempoSequence;
    if (auto* tempo0 = seq.getTempo (0))
    {
        auto newTempo = tempo0->getBpm();
        if (! juce::approximatelyEqual (newTempo, lastTempo))
        {
            tempoSlider.setValue (newTempo, juce::dontSendNotification);
            lastTempo = newTempo;
        }
    }

    if (auto* sig0 = seq.getTimeSig (0))
    {
        int newNumerator = sig0->numerator.get();
        int newDenominator = sig0->denominator.get();

        if (newNumerator != lastNumerator)
        {
            timeSigNumeratorBox.setSelectedId (newNumerator, juce::dontSendNotification);
            lastNumerator = newNumerator;
        }

        if (newDenominator != lastDenominator)
        {
            timeSigDenominatorBox.setSelectedId (newDenominator, juce::dontSendNotification);
            lastDenominator = newDenominator;
        }
    }

    bool newLoopState = transport.looping.get();
    if (newLoopState != lastLoopState)
    {
        loopButton.setToggleState (newLoopState, juce::dontSendNotification);
        lastLoopState = newLoopState;
    }

    bool newPunchState = editSession.getEdit().recordingPunchInOut.get();
    if (newPunchState != lastPunchState)
    {
        punchButton.setToggleState (newPunchState, juce::dontSendNotification);
        lastPunchState = newPunchState;
    }

    bool newClickState = editSession.getEdit().clickTrackEnabled.get();
    if (newClickState != lastClickState)
    {
        clickToggle.setToggleState (newClickState, juce::dontSendNotification);
        lastClickState = newClickState;
    }

    bool newSnapState = timeline->isSnapEnabled();
    if (newSnapState != lastSnapState)
    {
        snapToggle.setToggleState (newSnapState, juce::dontSendNotification);
        lastSnapState = newSnapState;
    }

    bool newBarsBeatsState = timeline->getShowBarsBeatsRuler();
    if (newBarsBeatsState != lastBarsBeatsState)
    {
        barsBeatsToggle.setToggleState (newBarsBeatsState, juce::dontSendNotification);
        lastBarsBeatsState = newBarsBeatsState;
    }

    int newSnapResolution = -1;
    switch (timeline->getSnapResolution())
    {
        case TimelineComponent::SnapResolution::bar:
            newSnapResolution = 1;
            break;
        case TimelineComponent::SnapResolution::beat:
            newSnapResolution = 2;
            break;
        case TimelineComponent::SnapResolution::halfBeat:
            newSnapResolution = 3;
            break;
        case TimelineComponent::SnapResolution::quarterBeat:
            newSnapResolution = 4;
            break;
    }

    if (newSnapResolution != lastSnapResolution)
    {
        snapResolutionBox.setSelectedId (newSnapResolution, juce::dontSendNotification);
        lastSnapResolution = newSnapResolution;
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
    if (controlName == "stopRecord")
        return stopRecordButton.getTooltip();
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
    if (controlName == "micInput")
        return micInputCombo.getTooltip();
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
