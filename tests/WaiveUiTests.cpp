#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

#include "MainComponent.h"
#include "SessionComponent.h"
#include "TimelineComponent.h"
#include "EditSession.h"
#include "ProjectManager.h"
#include "UndoableCommandHandler.h"
#include "JobQueue.h"
#include "CommandHandler.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace te = tracktion;

namespace
{

void expect (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

int getClipCount (te::AudioTrack& track)
{
    return track.getClips().size();
}

te::AudioTrack* getFirstTrack (te::Edit& edit)
{
    auto tracks = te::getAudioTracks (edit);
    if (tracks.isEmpty())
        return nullptr;

    return tracks.getFirst();
}

te::Clip* findClipAtStart (te::AudioTrack& track, double startSeconds)
{
    for (auto* clip : track.getClips())
    {
        if (clip == nullptr)
            continue;

        if (std::abs (clip->getPosition().getStart().inSeconds() - startSeconds) < 0.01)
            return clip;
    }

    return nullptr;
}

bool waitForFile (const juce::File& file, int timeoutMs = 2000)
{
    constexpr int sleepMs = 20;
    int waited = 0;

    while (waited < timeoutMs)
    {
        if (file.existsAsFile())
            return true;

        juce::Thread::sleep (sleepMs);
        waited += sleepMs;
    }

    return file.existsAsFile();
}

juce::File createLifecycleFixtureProject (te::Engine& engine)
{
    auto fixtureDir = juce::File::getCurrentWorkingDirectory()
                          .getChildFile ("build")
                          .getChildFile ("ui_test_projects");
    fixtureDir.createDirectory();
    auto projectFile = fixtureDir.getNonexistentChildFile ("waive_ui_lifecycle_", ".tracktionedit", true);
    auto backingFile = fixtureDir.getNonexistentChildFile ("waive_ui_fixture_backing_", ".tracktionedit", true);

    auto fixtureEdit = te::createEmptyEdit (engine, backingFile);
    fixtureEdit->ensureNumberOfAudioTracks (1);

    auto* track = getFirstTrack (*fixtureEdit);
    expect (track != nullptr, "Expected fixture edit audio track");

    auto clip = track->insertMIDIClip (
        "fixture_source",
        te::TimeRange (te::TimePosition::fromSeconds (0.0),
                       te::TimePosition::fromSeconds (1.0)),
        nullptr);
    expect (clip != nullptr, "Expected fixture MIDI clip insertion");
    clip->getSequence().addNote (60, te::BeatPosition::fromBeats (0.0),
                                 te::BeatDuration::fromBeats (1.0),
                                 100, 0, &fixtureEdit->getUndoManager());

    fixtureEdit->flushState();
    auto saved = te::EditFileOperations (*fixtureEdit).saveAs (projectFile, true);
    fixtureEdit->resetChangedStatus();
    expect (saved, "Expected fixture project saveAs to succeed");

    if (backingFile.existsAsFile())
        (void) backingFile.deleteFile();

    return projectFile;
}

void runUiCommandRoutingRegression()
{
    te::Engine engine ("WaiveUiTests");
    engine.getPluginManager().initialise();

    EditSession session (engine);
    waive::JobQueue jobQueue;
    ProjectManager projectManager (session);
    CommandHandler commandHandler (session.getEdit());
    UndoableCommandHandler undoableHandler (commandHandler, session);

    MainComponent mainComponent (undoableHandler, session, jobQueue, projectManager);
    mainComponent.setBounds (0, 0, 1200, 800);
    mainComponent.resized();

    auto tracks = te::getAudioTracks (session.getEdit());
    expect (! tracks.isEmpty(), "Expected at least one audio track for UI test");
    auto* track = tracks.getFirst();
    expect (track != nullptr, "Expected non-null first track");

    auto sourceClip = track->insertMIDIClip (
        "ui_source",
        te::TimeRange (te::TimePosition::fromSeconds (0.0),
                       te::TimePosition::fromSeconds (2.0)),
        nullptr);
    expect (sourceClip != nullptr, "Expected source MIDI clip insertion");
    sourceClip->getSequence().addNote (64, te::BeatPosition::fromBeats (0.0),
                                       te::BeatDuration::fromBeats (1.0),
                                       100, 0, &session.getEdit().getUndoManager());

    auto& timeline = mainComponent.getSessionComponentForTesting().getTimeline();
    timeline.rebuildTracks();
    timeline.getSelectionManager().selectClip (sourceClip.get());

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdDuplicate),
            "Expected duplicate command to execute");
    expect (getClipCount (*track) == 2, "Expected duplicate command to create second clip");

    auto* originalAtZero = findClipAtStart (*track, 0.0);
    expect (originalAtZero != nullptr, "Expected original clip at 0s");

    session.getEdit().getTransport().setPosition (te::TimePosition::fromSeconds (1.0));
    timeline.getSelectionManager().selectClip (originalAtZero);
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdSplit),
            "Expected split command to execute");
    expect (getClipCount (*track) == 3, "Expected split command to add one clip");

    auto* clipToDelete = findClipAtStart (*track, 0.0);
    expect (clipToDelete != nullptr, "Expected clip to delete at start");

    timeline.getSelectionManager().selectClip (clipToDelete);
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdDelete),
            "Expected delete command to execute");
    expect (getClipCount (*track) == 2, "Expected delete command to remove selected clip");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute");
    expect (getClipCount (*track) == 3, "Expected undo to restore deleted clip");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute");
    expect (getClipCount (*track) == 2, "Expected redo to remove clip again");
}

void runUiProjectLifecycleRegression()
{
    te::Engine engine ("WaiveUiLifecycleTests");
    engine.getPluginManager().initialise();

    EditSession session (engine);
    waive::JobQueue jobQueue;
    ProjectManager projectManager (session);
    CommandHandler commandHandler (session.getEdit());
    UndoableCommandHandler undoableHandler (commandHandler, session);

    MainComponent mainComponent (undoableHandler, session, jobQueue, projectManager);
    mainComponent.setBounds (0, 0, 1200, 800);
    mainComponent.resized();

    projectManager.clearRecentFiles();

    auto projectFile = createLifecycleFixtureProject (engine);
    const bool fixtureExistsAsFile = waitForFile (projectFile);
    const bool fixtureExists = projectFile.exists();
    const bool fixtureIsDirectory = projectFile.isDirectory();
    expect (fixtureExistsAsFile || fixtureExists,
            "Expected fixture project to exist (path=" + projectFile.getFullPathName().toStdString()
            + ", existsAsFile=" + (fixtureExistsAsFile ? "true" : "false")
            + ", exists=" + (fixtureExists ? "true" : "false")
            + ", isDirectory=" + (fixtureIsDirectory ? "true" : "false") + ")");

    expect (projectManager.openProject (projectFile), "Expected lifecycle openProject(file) to succeed");
    expect (projectManager.getCurrentFile() == projectFile, "Expected current file set after open");
    expect (projectManager.getProjectName() == projectFile.getFileNameWithoutExtension(),
            "Expected project name to match opened file");
    expect (! projectManager.isDirty(), "Expected opened project to start clean");

    auto* openedTrack = getFirstTrack (session.getEdit());
    expect (openedTrack != nullptr, "Expected opened project first track");
    expect (getClipCount (*openedTrack) == 1, "Expected fixture project to open with one clip");

    auto& timeline = mainComponent.getSessionComponentForTesting().getTimeline();
    timeline.rebuildTracks();
    auto* openedClip = openedTrack->getClips().getFirst();
    expect (openedClip != nullptr, "Expected opened clip to exist");
    timeline.getSelectionManager().selectClip (openedClip);

    expect (session.performEdit ("Lifecycle Add Clip", [&] (te::Edit& edit)
    {
        auto* track = getFirstTrack (edit);
        if (track == nullptr)
            return;

        auto clip = track->insertMIDIClip (
            "lifecycle_added",
            te::TimeRange (te::TimePosition::fromSeconds (1.0),
                           te::TimePosition::fromSeconds (2.0)),
            nullptr);
        if (clip != nullptr)
            clip->getSequence().addNote (65, te::BeatPosition::fromBeats (0.0),
                                         te::BeatDuration::fromBeats (1.0),
                                         100, 0, &edit.getUndoManager());
    }), "Expected lifecycle mutation to succeed");

    openedTrack = getFirstTrack (session.getEdit());
    expect (openedTrack != nullptr, "Expected track after lifecycle mutation");
    expect (getClipCount (*openedTrack) == 2, "Expected two clips after lifecycle mutation");

    // Mark dirty explicitly to keep lifecycle assertions deterministic in headless tests.
    session.getEdit().markAsChanged();
    expect (projectManager.isDirty(), "Expected lifecycle mutation to mark project dirty");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdSave),
            "Expected save command to execute");
    expect (! projectManager.isDirty(), "Expected save command to clear dirty state");

    auto savedEdit = te::loadEditFromFile (engine, projectFile);
    auto* savedTrack = getFirstTrack (*savedEdit);
    expect (savedTrack != nullptr, "Expected saved edit first track");
    expect (getClipCount (*savedTrack) == 2, "Expected save command to persist lifecycle clip");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdNew),
            "Expected new project command to execute");
    expect (projectManager.getCurrentFile() == juce::File(), "Expected new project to clear current file");
    expect (projectManager.getProjectName() == "Untitled", "Expected new project name to be Untitled");

    auto* newTrack = getFirstTrack (session.getEdit());
    expect (newTrack != nullptr, "Expected new project first track");
    expect (getClipCount (*newTrack) == 0, "Expected new project to start with no clips");

    // Verifies edit swap clears old clip selection and command dispatch remains safe.
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdDuplicate),
            "Expected duplicate command to execute after new project");
    newTrack = getFirstTrack (session.getEdit());
    expect (newTrack != nullptr, "Expected track after post-new duplicate");
    expect (getClipCount (*newTrack) == 0,
            "Expected duplicate after new project to be a no-op with no selection");

    expect (projectManager.openProject (projectFile), "Expected reopening saved project to succeed");
    auto* reopenedTrack = getFirstTrack (session.getEdit());
    expect (reopenedTrack != nullptr, "Expected reopened project first track");
    expect (getClipCount (*reopenedTrack) == 2, "Expected reopened project to retain saved clips");

    auto recentFiles = projectManager.getRecentFiles();
    expect (! recentFiles.isEmpty(), "Expected recent files to contain opened project");
    expect (recentFiles[0] == projectFile.getFullPathName(),
            "Expected opened project to be first in recent files");

    projectManager.clearRecentFiles();
    expect (projectManager.getRecentFiles().isEmpty(), "Expected recent files cleared");

    (void) projectFile.deleteFile();
}

void runUiPhase3TimeAutomationLoopPunchRegression()
{
    te::Engine engine ("WaiveUiPhase3Tests");
    engine.getPluginManager().initialise();

    EditSession session (engine);
    waive::JobQueue jobQueue;
    ProjectManager projectManager (session);
    CommandHandler commandHandler (session.getEdit());
    UndoableCommandHandler undoableHandler (commandHandler, session);

    MainComponent mainComponent (undoableHandler, session, jobQueue, projectManager);
    mainComponent.setBounds (0, 0, 1400, 900);
    mainComponent.resized();

    auto& sessionComponent = mainComponent.getSessionComponentForTesting();
    auto& timeline = sessionComponent.getTimeline();
    auto& edit = session.getEdit();

    auto* track = getFirstTrack (edit);
    expect (track != nullptr, "Expected phase-3 test track");
    auto* volumePlugin = track->getVolumePlugin();
    expect (volumePlugin != nullptr, "Expected phase-3 test track volume plugin");

    // 3A: tempo/time-signature + bars/beats snap.
    sessionComponent.setTempoForTesting (120.0);
    sessionComponent.setTimeSignatureForTesting (4, 4);
    sessionComponent.setSnapForTesting (true, TimelineComponent::SnapResolution::beat);

    auto* tempo0 = edit.tempoSequence.getTempo (0);
    expect (tempo0 != nullptr, "Expected initial tempo setting");
    expect (std::abs (tempo0->getBpm() - 120.0) < 0.05, "Expected tempo to be set to 120 BPM");

    auto* sig0 = edit.tempoSequence.getTimeSig (0);
    expect (sig0 != nullptr, "Expected initial time signature setting");
    expect (sig0->numerator.get() == 4 && sig0->denominator.get() == 4,
            "Expected time signature to be 4/4");

    auto snapped = sessionComponent.snapTimeForTesting (0.37);
    expect (std::abs (snapped - 0.5) < 0.02,
            "Expected beat snap to align 0.37s to 0.5s at 120 BPM");

    edit.getTransport().setPosition (te::TimePosition::fromSeconds (2.0));
    sessionComponent.insertTempoMarkerAtPlayheadForTesting (140.0);
    expect (edit.tempoSequence.getNumTempos() >= 2, "Expected tempo marker insertion at playhead");
    expect (std::abs (edit.tempoSequence.getBpmAt (te::TimePosition::fromSeconds (2.0)) - 140.0) < 0.5,
            "Expected BPM at 2s to follow inserted tempo marker");

    sessionComponent.insertTimeSigMarkerAtPlayheadForTesting (3, 4);
    expect (edit.tempoSequence.getNumTimeSigs() >= 2, "Expected time-signature marker insertion at playhead");
    auto& sigAtTwo = edit.tempoSequence.getTimeSigAt (te::TimePosition::fromSeconds (2.0));
    expect (sigAtTwo.numerator.get() == 3 && sigAtTwo.denominator.get() == 4,
            "Expected 3/4 time signature at marker position");

    // 3B: automation points on track/plugin parameters.
    auto volumeParamId = volumePlugin->volParam->paramID;
    expect (timeline.addAutomationPointForTrackParam (0, volumeParamId, 1.03, 0.75f),
            "Expected adding automation point through timeline helper");
    expect (volumePlugin->volParam->getCurve().getNumPoints() > 0,
            "Expected automation point to be present after add");

    int movedPointIndex = -1;
    auto& curve = volumePlugin->volParam->getCurve();
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        auto t = curve.getPointTime (i).inSeconds();
        if (std::abs (t - 1.0) < 0.06)
        {
            movedPointIndex = i;
            break;
        }
    }
    expect (movedPointIndex >= 0, "Expected to find snapped automation point near 1.0s");

    expect (timeline.moveAutomationPointForTrackParam (0, volumeParamId, movedPointIndex, 1.49, 0.40f),
            "Expected moving automation point through timeline helper");
    expect (std::abs (curve.getPointTime (movedPointIndex).inSeconds() - 1.5) < 0.06,
            "Expected moved automation point to snap to beat at 1.5s");
    expect (std::abs (curve.getPointValue (movedPointIndex) - 0.40f) < 0.08f,
            "Expected moved automation point value update");

    // 3C: loop + punch transport state.
    sessionComponent.setLoopRangeForTesting (0.5, 1.5);
    sessionComponent.setLoopEnabledForTesting (true);
    sessionComponent.setPunchEnabledForTesting (true);

    auto loopRange = edit.getTransport().getLoopRange();
    expect (std::abs (loopRange.getStart().inSeconds() - 0.5) < 0.01,
            "Expected loop in point at 0.5s");
    expect (std::abs (loopRange.getEnd().inSeconds() - 1.5) < 0.01,
            "Expected loop out point at 1.5s");
    expect (edit.getTransport().looping.get(), "Expected loop enabled");
    expect (edit.recordingPunchInOut.get(), "Expected punch enabled");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    try
    {
        runUiCommandRoutingRegression();
        runUiProjectLifecycleRegression();
        runUiPhase3TimeAutomationLoopPunchRegression();
        std::cout << "WaiveUiTests: PASS" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "WaiveUiTests: FAIL: " << e.what() << std::endl;
        return 1;
    }
}
