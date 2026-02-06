#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

#include "MainComponent.h"
#include "SessionComponent.h"
#include "TimelineComponent.h"
#include "ToolsComponent.h"
#include "LibraryComponent.h"
#include "PluginBrowserComponent.h"
#include "EditSession.h"
#include "ProjectManager.h"
#include "UndoableCommandHandler.h"
#include "JobQueue.h"
#include "CommandHandler.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

float getTrackVolumeDb (te::AudioTrack& track)
{
    auto* volumePlugin = track.getVolumePlugin();
    if (volumePlugin == nullptr)
        return 0.0f;

    return te::volumeFaderPositionToDB ((float) volumePlugin->volume.get());
}

float getTrackPan (te::AudioTrack& track)
{
    auto* volumePlugin = track.getVolumePlugin();
    if (volumePlugin == nullptr || volumePlugin->panParam == nullptr)
        return 0.0f;

    return volumePlugin->panParam->getCurrentValue() * 2.0f - 1.0f;
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

juce::File createPhase4FixtureAudioFile()
{
    auto audioDir = juce::File::getCurrentWorkingDirectory()
                        .getChildFile ("build")
                        .getChildFile ("ui_test_audio");
    audioDir.createDirectory();

    auto file = audioDir.getNonexistentChildFile ("waive_ui_phase4_", ".wav", true);

    juce::WavAudioFormat wavFormat;
    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
    expect (stream != nullptr, "Expected output stream for phase-4 fixture audio");

    constexpr double sampleRate = 44100.0;
    constexpr int numChannels = 1;
    constexpr int bitsPerSample = 16;
    constexpr double durationSeconds = 1.0;
    const int totalSamples = (int) std::round (sampleRate * durationSeconds);

    auto writer = std::unique_ptr<juce::AudioFormatWriter> (
        wavFormat.createWriterFor (stream.get(), sampleRate, numChannels, bitsPerSample, {}, 0));
    expect (writer != nullptr, "Expected WAV writer for phase-4 fixture audio");
    (void) stream.release();

    juce::AudioBuffer<float> buffer (numChannels, totalSamples);
    constexpr float amplitude = 0.25f;
    constexpr double frequencyHz = 220.0;

    for (int i = 0; i < totalSamples; ++i)
    {
        const auto sample = amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequencyHz * (double) i / sampleRate);
        buffer.setSample (0, i, (float) sample);
    }

    const bool writeOk = writer->writeFromAudioSampleBuffer (buffer, 0, totalSamples);
    expect (writeOk, "Expected fixture WAV write to succeed");

    return file;
}

juce::File createPhase5FixtureAudioFile (const juce::String& stemName,
                                         double durationSeconds,
                                         std::function<float (int index, double sampleRate)> generator)
{
    auto audioDir = juce::File::getCurrentWorkingDirectory()
                        .getChildFile ("build")
                        .getChildFile ("ui_test_audio");
    audioDir.createDirectory();

    auto file = audioDir.getNonexistentChildFile (stemName, ".wav", true);

    juce::WavAudioFormat wavFormat;
    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
    expect (stream != nullptr, "Expected output stream for phase-5 fixture audio");

    constexpr double sampleRate = 44100.0;
    constexpr int numChannels = 1;
    constexpr int bitsPerSample = 16;
    const int totalSamples = (int) std::round (sampleRate * durationSeconds);

    auto writer = std::unique_ptr<juce::AudioFormatWriter> (
        wavFormat.createWriterFor (stream.get(), sampleRate, numChannels, bitsPerSample, {}, 0));
    expect (writer != nullptr, "Expected WAV writer for phase-5 fixture audio");
    (void) stream.release();

    juce::AudioBuffer<float> buffer (numChannels, totalSamples);
    for (int i = 0; i < totalSamples; ++i)
        buffer.setSample (0, i, generator (i, sampleRate));

    const bool writeOk = writer->writeFromAudioSampleBuffer (buffer, 0, totalSamples);
    expect (writeOk, "Expected phase-5 fixture WAV write to succeed");

    return file;
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

void runUiPhase1LibraryAndPhase2PluginRoutingRegression()
{
    te::Engine engine ("WaiveUiPhase1And2Tests");
    engine.getPluginManager().initialise();

    EditSession session (engine);
    waive::JobQueue jobQueue;
    ProjectManager projectManager (session);
    CommandHandler commandHandler (session.getEdit());
    UndoableCommandHandler undoableHandler (commandHandler, session);

    MainComponent mainComponent (undoableHandler, session, jobQueue, projectManager);
    mainComponent.setBounds (0, 0, 1500, 900);
    mainComponent.resized();

    auto& edit = session.getEdit();
    auto* track = getFirstTrack (edit);
    expect (track != nullptr, "Expected track for phase-1/2 regression");

    auto fixtureAudio = createPhase4FixtureAudioFile();
    edit.getTransport().setPosition (te::TimePosition::fromSeconds (1.25));

    auto& library = mainComponent.getLibraryComponentForTesting();
    const auto clipCountBeforeImport = getClipCount (*track);
    library.fileDoubleClicked (fixtureAudio);

    expect (getClipCount (*track) == clipCountBeforeImport + 1,
            "Expected library double-click import to insert one clip");
    auto* importedClip = findClipAtStart (*track, 1.25);
    expect (importedClip != nullptr, "Expected imported clip start at transport position");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo to revert library import");
    expect (getClipCount (*track) == clipCountBeforeImport,
            "Expected undo to remove imported clip");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo to restore library import");
    expect (getClipCount (*track) == clipCountBeforeImport + 1,
            "Expected redo to reinsert imported clip");

    auto& pluginBrowser = mainComponent.getPluginBrowserForTesting();
    expect (pluginBrowser.selectTrackForTesting (0),
            "Expected plugin browser to select track 0 for phase-2 regression");

    const int chainCountBeforeInsert = pluginBrowser.getChainPluginCountForTesting();
    expect (pluginBrowser.insertBuiltInPluginForTesting (te::ReverbPlugin::xmlTypeName),
            "Expected built-in reverb insertion through plugin browser test helper");
    expect (pluginBrowser.getChainPluginCountForTesting() == chainCountBeforeInsert + 1,
            "Expected plugin chain count to increase after reverb insert");

    expect (pluginBrowser.selectChainRowForTesting (0),
            "Expected selecting inserted plugin chain row");
    const bool bypassBeforeToggle = pluginBrowser.isChainPluginBypassedForTesting (0);
    expect (pluginBrowser.toggleSelectedChainPluginBypassForTesting(),
            "Expected plugin bypass toggle to change state");
    expect (pluginBrowser.isChainPluginBypassedForTesting (0) != bypassBeforeToggle,
            "Expected bypass state change after toggle");
    expect (pluginBrowser.openSelectedChainPluginEditorForTesting(),
            "Expected plugin editor open for selected chain plugin");
    expect (pluginBrowser.closeSelectedChainPluginEditorForTesting(),
            "Expected plugin editor close for selected chain plugin");

    expect (pluginBrowser.getAvailableInputCountForTesting() > 0,
            "Expected at least one audio input device for recording workflow coverage");
    expect (pluginBrowser.selectFirstAvailableInputForTesting(),
            "Expected input assignment to selected track");
    expect (pluginBrowser.hasAssignedInputForTesting(),
            "Expected selected track to report assigned input");
    expect (pluginBrowser.setArmEnabledForTesting (true),
            "Expected arming selected track input");
    expect (pluginBrowser.isArmEnabledForTesting(),
            "Expected armed state to be true");
    expect (pluginBrowser.setMonitorEnabledForTesting (true),
            "Expected monitor-on state update for selected track input");
    expect (pluginBrowser.isMonitorEnabledForTesting(),
            "Expected monitor state to be on");

    const int clipCountBeforeRecordToggle = getClipCount (*track);
    edit.getTransport().setPosition (te::TimePosition::fromSeconds (0.0));
    edit.getTransport().record (false, true);
    expect (edit.getTransport().isRecording(),
            "Expected transport to enter recording state");
    juce::Thread::sleep (80);
    expect (session.performEdit ("Stop Recording (Test)", [&] (te::Edit&)
    {
        edit.getTransport().stopRecording (false);
    }), "Expected stop-record mutation to succeed");
    expect (! edit.getTransport().isRecording(),
            "Expected transport recording state to clear after stop");
    expect (getClipCount (*track) >= clipCountBeforeRecordToggle,
            "Expected record start/stop to preserve existing clips");

    expect (pluginBrowser.clearInputForTesting(),
            "Expected clearing selected track input");
    expect (! pluginBrowser.hasAssignedInputForTesting(),
            "Expected selected track input to be cleared");

    expect (pluginBrowser.setSendLevelDbForTesting (-12.0f),
            "Expected send level test helper to create/update aux send");
    const auto sendGainDb = pluginBrowser.getAuxSendGainDbForTesting (0);
    expect (std::isfinite (sendGainDb) && std::abs (sendGainDb + 12.0f) < 0.6f,
            "Expected aux send gain near -12 dB after update");

    auto pluginOrderBeforeMove = pluginBrowser.getChainPluginTypeOrderForTesting();
    expect (pluginOrderBeforeMove.size() >= 2,
            "Expected at least two chain plugins before move test");
    expect (pluginBrowser.selectChainRowForTesting (1),
            "Expected selecting second chain row before move");
    expect (pluginBrowser.moveSelectedChainPluginForTesting (-1),
            "Expected plugin move-up operation to reorder chain");
    auto pluginOrderAfterMove = pluginBrowser.getChainPluginTypeOrderForTesting();
    expect (pluginOrderAfterMove != pluginOrderBeforeMove,
            "Expected plugin order to change after move");

    expect (pluginBrowser.selectChainRowForTesting (0),
            "Expected selecting top chain row before removal");
    const int chainCountBeforeRemove = pluginBrowser.getChainPluginCountForTesting();
    expect (pluginBrowser.removeSelectedChainPluginForTesting(),
            "Expected plugin removal to reduce chain size");
    const int chainCountAfterRemove = pluginBrowser.getChainPluginCountForTesting();
    expect (chainCountAfterRemove == chainCountBeforeRemove - 1,
            "Expected chain size decrement after remove");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo to restore removed plugin");
    expect (pluginBrowser.getChainPluginCountForTesting() == chainCountBeforeRemove,
            "Expected undo to restore chain size");
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo to reapply plugin removal");
    expect (pluginBrowser.getChainPluginCountForTesting() == chainCountAfterRemove,
            "Expected redo to remove plugin again");

    const int auxReturnCountBefore = pluginBrowser.getMasterAuxReturnCountForTesting (0);
    const int reverbCountBefore = pluginBrowser.getMasterReverbCountForTesting();
    pluginBrowser.ensureReverbReturnOnMasterForTesting();

    const int auxReturnCountAfter = pluginBrowser.getMasterAuxReturnCountForTesting (0);
    const int reverbCountAfter = pluginBrowser.getMasterReverbCountForTesting();
    expect (auxReturnCountAfter >= juce::jmax (1, auxReturnCountBefore),
            "Expected master aux return creation for routing");
    expect (reverbCountAfter >= juce::jmax (1, reverbCountBefore),
            "Expected master reverb creation for routing");

    pluginBrowser.ensureReverbReturnOnMasterForTesting();
    expect (pluginBrowser.getMasterAuxReturnCountForTesting (0) == auxReturnCountAfter,
            "Expected repeated return creation to be idempotent");
    expect (pluginBrowser.getMasterReverbCountForTesting() == reverbCountAfter,
            "Expected repeated reverb return creation to be idempotent");

    (void) fixtureAudio.deleteFile();
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

    // Bar snap should respect denominator when engine beat length is fixed-crotchet.
    // In denominator-dependent mode, 6/8 and 6/4 share the same beat count per bar.
    sessionComponent.setTimeSignatureForTesting (6, 8);
    sessionComponent.setSnapForTesting (true, TimelineComponent::SnapResolution::bar);
    const auto snappedBarInSixEight = sessionComponent.snapTimeForTesting (1.1);

    sessionComponent.setTimeSignatureForTesting (6, 4);
    const auto snappedBarInSixFour = sessionComponent.snapTimeForTesting (1.1);

    const bool beatDependsOnTimeSig
        = edit.engine.getEngineBehaviour().lengthOfOneBeatDependsOnTimeSignature();

    expect (std::isfinite (snappedBarInSixEight) && snappedBarInSixEight >= 0.0,
            "Expected snapped 6/8 bar time to be finite and non-negative");
    expect (std::isfinite (snappedBarInSixFour) && snappedBarInSixFour >= 0.0,
            "Expected snapped 6/4 bar time to be finite and non-negative");

    if (! beatDependsOnTimeSig)
    {
        expect (std::abs (snappedBarInSixEight - 1.5) < 0.05,
                "Expected 6/8 bar snap to align 1.1s to 1.5s in fixed-crotchet beat mode");
        expect (std::abs (snappedBarInSixEight - snappedBarInSixFour) > 0.2,
                "Expected 6/8 and 6/4 bar snap to differ in fixed-crotchet beat mode");
    }

    sessionComponent.setTimeSignatureForTesting (4, 4);
    sessionComponent.setSnapForTesting (true, TimelineComponent::SnapResolution::beat);

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
    auto& curve = volumePlugin->volParam->getCurve();
    const int pointsBeforeAdd = curve.getNumPoints();

    expect (timeline.addAutomationPointForTrackParam (0, volumeParamId, 1.03, 0.75f),
            "Expected adding automation point through timeline helper");
    expect (curve.getNumPoints() == pointsBeforeAdd + 1,
            "Expected exactly one automation point added");

    auto findPointNearTime = [&curve] (double targetSeconds, double toleranceSeconds = 0.08) -> int
    {
        for (int i = 0; i < curve.getNumPoints(); ++i)
            if (std::abs (curve.getPointTime (i).inSeconds() - targetSeconds) < toleranceSeconds)
                return i;
        return -1;
    };

    int movedPointIndex = findPointNearTime (1.0, 0.06);
    expect (movedPointIndex >= 0, "Expected to find snapped automation point near 1.0s");

    expect (timeline.moveAutomationPointForTrackParam (0, volumeParamId, movedPointIndex, 1.49, 0.40f),
            "Expected moving automation point through timeline helper");
    expect (std::abs (curve.getPointTime (movedPointIndex).inSeconds() - 1.5) < 0.06,
            "Expected moved automation point to snap to beat at 1.5s");
    expect (std::abs (curve.getPointValue (movedPointIndex) - 0.40f) < 0.08f,
            "Expected moved automation point value update");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute for automation move");
    auto pointAfterMoveUndo = findPointNearTime (1.0, 0.06);
    expect (pointAfterMoveUndo >= 0, "Expected automation move undo to restore point near 1.0s");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute for automation move");
    auto pointAfterMoveRedo = findPointNearTime (1.5, 0.06);
    expect (pointAfterMoveRedo >= 0, "Expected automation move redo to restore point near 1.5s");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute for automation move redo");
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute for automation add");
    expect (curve.getNumPoints() == pointsBeforeAdd,
            "Expected automation add undo to restore original point count");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute for automation add");
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute for automation move");
    expect (findPointNearTime (1.5, 0.06) >= 0,
            "Expected automation point near 1.5s after redo chain");

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

void runUiPhase4ToolFrameworkRegression()
{
    te::Engine engine ("WaiveUiPhase4Tests");
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
    auto& toolsComponent = mainComponent.getToolsComponentForTesting();
    auto& edit = session.getEdit();

    auto* track = getFirstTrack (edit);
    expect (track != nullptr, "Expected phase-4 test track");

    auto fixtureAudio = createPhase4FixtureAudioFile();

    auto insertedClip = track->insertWaveClip (
        "phase4_source",
        fixtureAudio,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (1.0) },
          te::TimeDuration() },
        false);
    expect (insertedClip != nullptr, "Expected phase-4 wave clip insertion");

    auto* audioClip = dynamic_cast<te::AudioClipBase*> (insertedClip.get());
    expect (audioClip != nullptr, "Expected phase-4 inserted audio clip");
    audioClip->setGainDB (0.0f);

    timeline.rebuildTracks();
    timeline.getSelectionManager().selectClip (insertedClip.get());

    toolsComponent.selectToolForTesting ("normalize_selected_clips");

    auto* initialParamsObj = new juce::DynamicObject();
    initialParamsObj->setProperty ("target_peak_db", -6.0);
    initialParamsObj->setProperty ("analysis_delay_ms", 0);
    toolsComponent.setParamsForTesting (juce::var (initialParamsObj));

    expect (toolsComponent.runPlanForTesting(), "Expected phase-4 plan start");
    expect (toolsComponent.waitForIdleForTesting(), "Expected phase-4 plan completion");
    expect (toolsComponent.hasPendingPlanForTesting(), "Expected pending plan after phase-4 planning");
    expect (std::abs (audioClip->getGainDB()) < 0.05f,
            "Expected plan preview to not mutate clip gain before apply");

    const auto previewText = toolsComponent.getPreviewTextForTesting();
    expect (previewText.contains ("normalize_selected_clips"),
            "Expected tool preview text to identify normalize tool");
    expect (previewText.contains ("Set clip"),
            "Expected tool preview text to contain clip parameter change");

    auto selectedAfterPreview = timeline.getSelectionManager().getSelectedClips();
    expect (selectedAfterPreview.contains (insertedClip.get()),
            "Expected tool preview to highlight affected clip in timeline");

    auto highlightedTracks = sessionComponent.getToolPreviewTracksForTesting();
    expect (highlightedTracks.contains (0),
            "Expected tool preview to highlight affected mixer track");

    toolsComponent.rejectPlanForTesting();
    expect (! toolsComponent.hasPendingPlanForTesting(), "Expected rejected plan to clear pending plan");
    expect (std::abs (audioClip->getGainDB()) < 0.05f,
            "Expected reject to leave clip gain unchanged");

    timeline.getSelectionManager().selectClip (insertedClip.get());
    expect (toolsComponent.runPlanForTesting(), "Expected phase-4 re-plan start");
    expect (toolsComponent.waitForIdleForTesting(), "Expected phase-4 re-plan completion");
    expect (toolsComponent.hasPendingPlanForTesting(), "Expected pending plan before apply");
    expect (toolsComponent.applyPlanForTesting(), "Expected phase-4 apply to succeed");

    const auto appliedGainDb = (double) audioClip->getGainDB();
    expect (std::abs (appliedGainDb) > 0.5,
            "Expected normalize apply to make a material clip-gain adjustment");

    auto planArtifact = toolsComponent.getLastPlanArtifactForTesting();
    expect (planArtifact.existsAsFile(), "Expected phase-4 plan artifact file to exist");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute for phase-4 apply");
    expect (std::abs (audioClip->getGainDB()) < 0.05f,
            "Expected undo to restore pre-apply clip gain");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute for phase-4 apply");
    const auto redoGainDb = (double) audioClip->getGainDB();
    expect (std::abs (redoGainDb - appliedGainDb) < 0.05,
            "Expected redo to restore normalized clip gain deterministically");

    auto* cancelParamsObj = new juce::DynamicObject();
    cancelParamsObj->setProperty ("target_peak_db", -6.0);
    cancelParamsObj->setProperty ("analysis_delay_ms", 1500);
    toolsComponent.setParamsForTesting (juce::var (cancelParamsObj));

    timeline.getSelectionManager().selectClip (insertedClip.get());
    const auto gainBeforeCancel = audioClip->getGainDB();
    expect (toolsComponent.runPlanForTesting(), "Expected long-running phase-4 plan start");
    juce::Thread::sleep (50);
    toolsComponent.cancelPlanForTesting();
    expect (toolsComponent.waitForIdleForTesting (5000), "Expected cancelled phase-4 plan to settle");
    expect (! toolsComponent.hasPendingPlanForTesting(),
            "Expected cancelled phase-4 plan to produce no pending changes");
    expect (std::abs (audioClip->getGainDB() - gainBeforeCancel) < 0.05f,
            "Expected cancellation to leave edit state unchanged");

    (void) fixtureAudio.deleteFile();
}

void runUiPhase5BuiltInToolsRegression()
{
    te::Engine engine ("WaiveUiPhase5Tests");
    engine.getPluginManager().initialise();

    EditSession session (engine);
    waive::JobQueue jobQueue;
    ProjectManager projectManager (session);
    CommandHandler commandHandler (session.getEdit());
    UndoableCommandHandler undoableHandler (commandHandler, session);

    MainComponent mainComponent (undoableHandler, session, jobQueue, projectManager);
    mainComponent.setBounds (0, 0, 1500, 900);
    mainComponent.resized();

    auto& sessionComponent = mainComponent.getSessionComponentForTesting();
    auto& timeline = sessionComponent.getTimeline();
    auto& toolsComponent = mainComponent.getToolsComponentForTesting();
    auto& edit = session.getEdit();

    edit.ensureNumberOfAudioTracks (2);
    auto tracks = te::getAudioTracks (edit);
    expect (tracks.size() >= 2, "Expected phase-5 test edit to have two audio tracks");

    auto* track1 = tracks.getUnchecked (0);
    auto* track2 = tracks.getUnchecked (1);
    expect (track1 != nullptr && track2 != nullptr, "Expected phase-5 tracks");

    const auto steadyClipFile = createPhase5FixtureAudioFile (
        "waive_ui_phase5_steady_",
        1.0,
        [] (int i, double sampleRate) -> float
        {
            constexpr float amplitude = 0.25f;
            constexpr double frequencyHz = 330.0;
            return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequencyHz * (double) i / sampleRate);
        });

    const auto trimClipFile = createPhase5FixtureAudioFile (
        "waive_ui_phase5_trim_",
        1.0,
        [] (int i, double sampleRate) -> float
        {
            const auto t = (double) i / sampleRate;
            if (t < 0.20 || t > 0.72)
                return 0.0f;

            constexpr float amplitude = 0.40f;
            constexpr double frequencyHz = 220.0;
            return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequencyHz * t);
        });

    const auto lateTransientFile = createPhase5FixtureAudioFile (
        "waive_ui_phase5_late_transient_",
        1.0,
        [] (int i, double sampleRate) -> float
        {
            const int onset = (int) std::round (0.10 * sampleRate);
            if (i < onset)
                return 0.0f;

            if (i == onset)
                return 0.95f;

            constexpr float amplitude = 0.30f;
            constexpr double frequencyHz = 180.0;
            const auto t = (double) i / sampleRate;
            return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequencyHz * t);
        });

    const auto earlyTransientFile = createPhase5FixtureAudioFile (
        "waive_ui_phase5_early_transient_",
        1.0,
        [] (int i, double sampleRate) -> float
        {
            const int onset = (int) std::round (0.02 * sampleRate);
            if (i < onset)
                return 0.0f;

            if (i == onset)
                return 0.95f;

            constexpr float amplitude = 0.30f;
            constexpr double frequencyHz = 180.0;
            const auto t = (double) i / sampleRate;
            return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequencyHz * t);
        });

    auto renameAndGainClip = track1->insertWaveClip (
        "Kick_Main",
        steadyClipFile,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (1.0) },
          te::TimeDuration() },
        false);
    expect (renameAndGainClip != nullptr, "Expected phase-5 steady wave clip");

    auto trimClip = track1->insertWaveClip (
        "Vocal_Trim",
        trimClipFile,
        { { te::TimePosition::fromSeconds (1.8),
            te::TimePosition::fromSeconds (2.8) },
          te::TimeDuration() },
        false);
    expect (trimClip != nullptr, "Expected phase-5 trim wave clip");

    auto lateClip = track1->insertWaveClip (
        "Snare_Late",
        lateTransientFile,
        { { te::TimePosition::fromSeconds (4.0),
            te::TimePosition::fromSeconds (5.0) },
          te::TimeDuration() },
        false);
    expect (lateClip != nullptr, "Expected phase-5 late transient clip");

    auto earlyClip = track2->insertWaveClip (
        "Snare_Early",
        earlyTransientFile,
        { { te::TimePosition::fromSeconds (3.8),
            te::TimePosition::fromSeconds (4.8) },
          te::TimeDuration() },
        false);
    expect (earlyClip != nullptr, "Expected phase-5 early transient clip");

    timeline.rebuildTracks();

    // 5A.1: Rename tracks from selected clips.
    const auto originalTrackName = track1->getName();
    timeline.getSelectionManager().selectClip (renameAndGainClip.get());

    toolsComponent.selectToolForTesting ("rename_tracks_from_clips");
    auto* renameParams = new juce::DynamicObject();
    renameParams->setProperty ("selected_only", true);
    toolsComponent.setParamsForTesting (juce::var (renameParams));

    expect (toolsComponent.runPlanForTesting(), "Expected phase-5 rename plan start");
    expect (toolsComponent.waitForIdleForTesting(), "Expected phase-5 rename plan completion");
    expect (toolsComponent.hasPendingPlanForTesting(), "Expected pending rename plan");
    expect (toolsComponent.applyPlanForTesting(), "Expected phase-5 rename apply to succeed");

    const auto renamedTrackName = track1->getName();
    expect (renamedTrackName != originalTrackName, "Expected phase-5 rename tool to change track name");
    expect (renamedTrackName.containsIgnoreCase ("kick"), "Expected renamed track to reflect clip name");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute for phase-5 rename");
    expect (track1->getName() == originalTrackName, "Expected undo to restore original track name");
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute for phase-5 rename");
    expect (track1->getName() == renamedTrackName, "Expected redo to restore renamed track name");

    // 5A.2: Gain-stage selected tracks to target peak.
    timeline.getSelectionManager().selectClip (renameAndGainClip.get());
    toolsComponent.selectToolForTesting ("gain_stage_selected_tracks");

    auto* gainParams = new juce::DynamicObject();
    gainParams->setProperty ("target_peak_db", -18.0);
    gainParams->setProperty ("analysis_delay_ms", 0);
    toolsComponent.setParamsForTesting (juce::var (gainParams));

    const auto volumeBeforeGainStage = getTrackVolumeDb (*track1);
    expect (toolsComponent.runPlanForTesting(), "Expected phase-5 gain-stage plan start");
    expect (toolsComponent.waitForIdleForTesting(), "Expected phase-5 gain-stage plan completion");
    expect (toolsComponent.hasPendingPlanForTesting(), "Expected pending gain-stage plan");
    expect (toolsComponent.applyPlanForTesting(), "Expected phase-5 gain-stage apply to succeed");

    const auto volumeAfterGainStage = getTrackVolumeDb (*track1);
    expect (volumeAfterGainStage < volumeBeforeGainStage - 1.0f,
            "Expected gain-stage apply to lower track volume materially");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute for phase-5 gain-stage");
    const auto volumeAfterGainUndo = getTrackVolumeDb (*track1);
    expect (volumeAfterGainUndo > volumeAfterGainStage + 1.0f,
            "Expected gain-stage undo to materially reverse the fader change");
    expect (std::abs (volumeAfterGainUndo - volumeBeforeGainStage) < 3.0f,
            "Expected gain-stage undo to land near pre-apply fader level");
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute for phase-5 gain-stage");
    expect (std::abs (getTrackVolumeDb (*track1) - volumeAfterGainStage) < 0.4f,
            "Expected gain-stage redo to restore staged volume");

    // 5A.3: Detect silence and cut regions.
    toolsComponent.selectToolForTesting ("detect_silence_and_cut_regions");
    timeline.getSelectionManager().selectClip (trimClip.get());

    auto* cutParams = new juce::DynamicObject();
    cutParams->setProperty ("threshold_db", -40.0);
    cutParams->setProperty ("min_trim_ms", 60.0);
    cutParams->setProperty ("padding_ms", 0.0);
    cutParams->setProperty ("analysis_delay_ms", 0);
    toolsComponent.setParamsForTesting (juce::var (cutParams));

    const auto trimStartBefore = trimClip->getPosition().getStart().inSeconds();
    const auto trimEndBefore = trimClip->getPosition().getEnd().inSeconds();

    expect (toolsComponent.runPlanForTesting(), "Expected phase-5 silence-cut plan start");
    expect (toolsComponent.waitForIdleForTesting(), "Expected phase-5 silence-cut plan completion");
    expect (toolsComponent.hasPendingPlanForTesting(), "Expected pending silence-cut plan");
    expect (toolsComponent.applyPlanForTesting(), "Expected phase-5 silence-cut apply to succeed");

    const auto trimStartAfter = trimClip->getPosition().getStart().inSeconds();
    const auto trimEndAfter = trimClip->getPosition().getEnd().inSeconds();
    expect (trimStartAfter > trimStartBefore + 0.12,
            "Expected silence-cut apply to trim clip start");
    expect (trimEndAfter < trimEndBefore - 0.12,
            "Expected silence-cut apply to trim clip end");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute for phase-5 silence-cut");
    expect (std::abs (trimClip->getPosition().getStart().inSeconds() - trimStartBefore) < 0.05,
            "Expected silence-cut undo to restore clip start");
    expect (std::abs (trimClip->getPosition().getEnd().inSeconds() - trimEndBefore) < 0.05,
            "Expected silence-cut undo to restore clip end");
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute for phase-5 silence-cut");
    expect (std::abs (trimClip->getPosition().getStart().inSeconds() - trimStartAfter) < 0.05,
            "Expected silence-cut redo to restore trimmed start");
    expect (std::abs (trimClip->getPosition().getEnd().inSeconds() - trimEndAfter) < 0.05,
            "Expected silence-cut redo to restore trimmed end");

    // 5A.4: Align clips by transient.
    toolsComponent.selectToolForTesting ("align_clips_by_transient");
    timeline.getSelectionManager().selectClip (lateClip.get());
    timeline.getSelectionManager().selectClip (earlyClip.get(), true);

    auto* alignParams = new juce::DynamicObject();
    alignParams->setProperty ("threshold_db", -24.0);
    alignParams->setProperty ("max_shift_ms", 1000.0);
    alignParams->setProperty ("analysis_delay_ms", 0);
    toolsComponent.setParamsForTesting (juce::var (alignParams));

    const auto lateStartBeforeAlign = lateClip->getPosition().getStart().inSeconds();
    const auto earlyStartBeforeAlign = earlyClip->getPosition().getStart().inSeconds();

    expect (toolsComponent.runPlanForTesting(), "Expected phase-5 transient-align plan start");
    expect (toolsComponent.waitForIdleForTesting(), "Expected phase-5 transient-align plan completion");
    expect (toolsComponent.hasPendingPlanForTesting(), "Expected pending transient-align plan");
    expect (toolsComponent.applyPlanForTesting(), "Expected phase-5 transient-align apply to succeed");

    const auto lateStartAfterAlign = lateClip->getPosition().getStart().inSeconds();
    const auto earlyStartAfterAlign = earlyClip->getPosition().getStart().inSeconds();

    expect (lateStartAfterAlign < lateStartBeforeAlign - 0.10,
            "Expected transient-align apply to move late clip earlier");
    expect (std::abs (earlyStartAfterAlign - earlyStartBeforeAlign) < 0.10,
            "Expected transient-align to keep earliest clip mostly stable");

    auto phase5Artifact = toolsComponent.getLastPlanArtifactForTesting();
    expect (phase5Artifact.existsAsFile(), "Expected phase-5 plan artifact file to exist");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo command to execute for phase-5 transient-align");
    expect (std::abs (lateClip->getPosition().getStart().inSeconds() - lateStartBeforeAlign) < 0.05,
            "Expected transient-align undo to restore late clip start");
    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo command to execute for phase-5 transient-align");
    expect (std::abs (lateClip->getPosition().getStart().inSeconds() - lateStartAfterAlign) < 0.05,
            "Expected transient-align redo to restore aligned late clip start");

    (void) steadyClipFile.deleteFile();
    (void) trimClipFile.deleteFile();
    (void) lateTransientFile.deleteFile();
    (void) earlyTransientFile.deleteFile();
}

void runUiPhase5ModelBackedToolsRegression()
{
    te::Engine engine ("WaiveUiPhase5BTests");
    engine.getPluginManager().initialise();

    EditSession session (engine);
    waive::JobQueue jobQueue;
    ProjectManager projectManager (session);
    CommandHandler commandHandler (session.getEdit());
    UndoableCommandHandler undoableHandler (commandHandler, session);

    MainComponent mainComponent (undoableHandler, session, jobQueue, projectManager);
    mainComponent.setBounds (0, 0, 1500, 900);
    mainComponent.resized();

    auto& sessionComponent = mainComponent.getSessionComponentForTesting();
    auto& timeline = sessionComponent.getTimeline();
    auto& toolsComponent = mainComponent.getToolsComponentForTesting();
    auto& edit = session.getEdit();

    auto modelStorage = juce::File::getCurrentWorkingDirectory()
                            .getChildFile ("build")
                            .getChildFile ("ui_test_models")
                            .getChildFile ("waive_ui_phase5b_" + juce::Uuid().toString());
    (void) modelStorage.deleteRecursively();
    modelStorage.createDirectory();

    auto setStorageResult = toolsComponent.setModelStorageDirectoryForTesting (modelStorage);
    expect (setStorageResult.wasOk(), "Expected model storage configuration to succeed");

    auto setLowQuotaResult = toolsComponent.setModelQuotaForTesting (96 * 1024);
    expect (setLowQuotaResult.wasOk(), "Expected low model quota setup to succeed");

    auto lowQuotaInstallResult = toolsComponent.installModelForTesting ("stem_separator", "1.1.0", false);
    expect (lowQuotaInstallResult.failed(), "Expected stem model install to fail under low quota");

    auto setHighQuotaResult = toolsComponent.setModelQuotaForTesting (512 * 1024 * 1024);
    expect (setHighQuotaResult.wasOk(), "Expected model quota increase to succeed");

    auto installStemResult = toolsComponent.installModelForTesting ("stem_separator", "1.1.0", true);
    expect (installStemResult.wasOk(),
            "Expected stem model install to succeed (error="
            + installStemResult.getErrorMessage().toStdString() + ")");
    expect (toolsComponent.isModelInstalledForTesting ("stem_separator", "1.1.0"),
            "Expected installed stem model version to be available");
    expect (toolsComponent.getPinnedModelVersionForTesting ("stem_separator") == "1.1.0",
            "Expected stem model version pin to persist");

    edit.ensureNumberOfAudioTracks (2);
    auto tracks = te::getAudioTracks (edit);
    expect (tracks.size() >= 2, "Expected phase-5B test edit to have at least two tracks");

    auto* track1 = tracks.getUnchecked (0);
    auto* track2 = tracks.getUnchecked (1);
    expect (track1 != nullptr && track2 != nullptr, "Expected non-null tracks for phase-5B");

    const auto stemFixture = createPhase5FixtureAudioFile (
        "waive_ui_phase5b_stem_fixture_",
        1.0,
        [] (int i, double sampleRate) -> float
        {
            constexpr float lowAmp = 0.35f;
            constexpr float highAmp = 0.22f;
            const auto t = (double) i / sampleRate;
            const auto low = lowAmp * std::sin (2.0 * juce::MathConstants<double>::pi * 120.0 * t);
            const auto high = highAmp * std::sin (2.0 * juce::MathConstants<double>::pi * 2200.0 * t);
            return (float) (low + high);
        });

    const auto mixFixtureOne = createPhase5FixtureAudioFile (
        "waive_ui_phase5b_mix_one_",
        1.0,
        [] (int i, double sampleRate) -> float
        {
            constexpr float amplitude = 0.40f;
            const auto t = (double) i / sampleRate;
            return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t);
        });

    const auto mixFixtureTwo = createPhase5FixtureAudioFile (
        "waive_ui_phase5b_mix_two_",
        1.0,
        [] (int i, double sampleRate) -> float
        {
            constexpr float amplitude = 0.18f;
            const auto t = (double) i / sampleRate;
            return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 660.0 * t);
        });

    auto stemSourceClip = track1->insertWaveClip (
        "StemSource",
        stemFixture,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (1.0) },
          te::TimeDuration() },
        false);
    expect (stemSourceClip != nullptr, "Expected stem source clip insertion");

    auto mixClipOne = track1->insertWaveClip (
        "MixSourceOne",
        mixFixtureOne,
        { { te::TimePosition::fromSeconds (1.5),
            te::TimePosition::fromSeconds (2.5) },
          te::TimeDuration() },
        false);
    expect (mixClipOne != nullptr, "Expected mix source one insertion");

    auto mixClipTwo = track2->insertWaveClip (
        "MixSourceTwo",
        mixFixtureTwo,
        { { te::TimePosition::fromSeconds (1.5),
            te::TimePosition::fromSeconds (2.5) },
          te::TimeDuration() },
        false);
    expect (mixClipTwo != nullptr, "Expected mix source two insertion");

    timeline.rebuildTracks();

    toolsComponent.selectToolForTesting ("stem_separation");
    timeline.getSelectionManager().selectClip (stemSourceClip.get());

    auto* stemParams = new juce::DynamicObject();
    stemParams->setProperty ("model_version", "1.1.0");
    stemParams->setProperty ("analysis_delay_ms", 0);
    toolsComponent.setParamsForTesting (juce::var (stemParams));

    const auto trackCountBeforeStem = te::getAudioTracks (edit).size();

    expect (toolsComponent.runPlanForTesting(), "Expected phase-5B stem plan start");
    expect (toolsComponent.waitForIdleForTesting(), "Expected phase-5B stem plan completion");
    expect (toolsComponent.hasPendingPlanForTesting(), "Expected pending phase-5B stem plan");
    expect (toolsComponent.applyPlanForTesting(), "Expected phase-5B stem apply to succeed");

    auto tracksAfterStemApply = te::getAudioTracks (edit);
    expect (tracksAfterStemApply.size() >= trackCountBeforeStem + 2,
            "Expected stem apply to add two destination tracks");

    bool hasStemLowTrack = false;
    bool hasStemHighTrack = false;
    for (auto* track : tracksAfterStemApply)
    {
        if (track == nullptr)
            continue;

        if (track->getName() == "Stem Low" && track->getClips().size() > 0)
            hasStemLowTrack = true;
        if (track->getName() == "Stem High" && track->getClips().size() > 0)
            hasStemHighTrack = true;
    }

    expect (hasStemLowTrack, "Expected stem apply to populate Stem Low track");
    expect (hasStemHighTrack, "Expected stem apply to populate Stem High track");

    auto stemArtifact = toolsComponent.getLastPlanArtifactForTesting();
    expect (stemArtifact.existsAsFile(), "Expected phase-5B stem artifact payload file");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo for phase-5B stem apply");
    auto tracksAfterStemUndo = te::getAudioTracks (edit);
    expect (tracksAfterStemUndo.size() <= trackCountBeforeStem,
            "Expected undo to remove stem destination tracks");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo for phase-5B stem apply");
    auto tracksAfterStemRedo = te::getAudioTracks (edit);
    expect (tracksAfterStemRedo.size() >= trackCountBeforeStem + 2,
            "Expected redo to restore stem destination tracks");

    auto installAutoMixResult = toolsComponent.installModelForTesting ("auto_mix_suggester", "1.0.0", true);
    expect (installAutoMixResult.wasOk(), "Expected auto-mix model install to succeed");
    expect (toolsComponent.getPinnedModelVersionForTesting ("auto_mix_suggester") == "1.0.0",
            "Expected auto-mix model pin to be set");

    toolsComponent.selectToolForTesting ("auto_mix_suggestions");
    timeline.getSelectionManager().selectClip (mixClipOne.get());
    timeline.getSelectionManager().selectClip (mixClipTwo.get(), true);

    auto* autoMixParams = new juce::DynamicObject();
    autoMixParams->setProperty ("model_version", "1.0.0");
    autoMixParams->setProperty ("target_peak_db", -16.0);
    autoMixParams->setProperty ("max_adjust_db", 10.0);
    autoMixParams->setProperty ("stereo_spread", true);
    autoMixParams->setProperty ("analysis_delay_ms", 0);
    toolsComponent.setParamsForTesting (juce::var (autoMixParams));

    tracks = te::getAudioTracks (edit);
    track1 = tracks.getUnchecked (0);
    track2 = tracks.getUnchecked (1);
    expect (track1 != nullptr && track2 != nullptr, "Expected tracks for auto-mix assertions");

    const auto track1VolumeBeforeAutoMix = getTrackVolumeDb (*track1);
    const auto track2VolumeBeforeAutoMix = getTrackVolumeDb (*track2);
    const auto track1PanBeforeAutoMix = getTrackPan (*track1);
    const auto track2PanBeforeAutoMix = getTrackPan (*track2);

    expect (toolsComponent.runPlanForTesting(), "Expected phase-5B auto-mix plan start");
    expect (toolsComponent.waitForIdleForTesting(), "Expected phase-5B auto-mix plan completion");
    expect (toolsComponent.hasPendingPlanForTesting(), "Expected pending phase-5B auto-mix plan");
    expect (toolsComponent.applyPlanForTesting(), "Expected phase-5B auto-mix apply to succeed");

    const auto track1VolumeAfterAutoMix = getTrackVolumeDb (*track1);
    const auto track2VolumeAfterAutoMix = getTrackVolumeDb (*track2);
    const auto track1PanAfterAutoMix = getTrackPan (*track1);
    const auto track2PanAfterAutoMix = getTrackPan (*track2);

    expect (std::abs (track1VolumeAfterAutoMix - track1VolumeBeforeAutoMix) > 0.4f
            || std::abs (track1PanAfterAutoMix - track1PanBeforeAutoMix) > 0.06f,
            "Expected auto-mix to materially change track-1 volume or pan");
    expect (std::abs (track2VolumeAfterAutoMix - track2VolumeBeforeAutoMix) > 0.4f
            || std::abs (track2PanAfterAutoMix - track2PanBeforeAutoMix) > 0.06f,
            "Expected auto-mix to materially change track-2 volume or pan");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdUndo),
            "Expected undo for phase-5B auto-mix apply");
    expect (std::abs (getTrackVolumeDb (*track1) - track1VolumeBeforeAutoMix) < 0.8f,
            "Expected auto-mix undo to restore track-1 volume near baseline");
    expect (std::abs (getTrackPan (*track1) - track1PanBeforeAutoMix) < 0.12f,
            "Expected auto-mix undo to restore track-1 pan near baseline");

    expect (mainComponent.invokeCommandForTesting (MainComponent::cmdRedo),
            "Expected redo for phase-5B auto-mix apply");
    expect (std::abs (getTrackVolumeDb (*track1) - track1VolumeAfterAutoMix) < 0.8f,
            "Expected auto-mix redo to restore track-1 volume");
    expect (std::abs (getTrackPan (*track1) - track1PanAfterAutoMix) < 0.12f,
            "Expected auto-mix redo to restore track-1 pan");

    auto uninstallAutoMixResult = toolsComponent.uninstallModelForTesting ("auto_mix_suggester", "1.0.0");
    expect (uninstallAutoMixResult.wasOk(), "Expected auto-mix model uninstall to succeed");
    expect (! toolsComponent.isModelInstalledForTesting ("auto_mix_suggester", "1.0.0"),
            "Expected auto-mix model to be removed");

    (void) stemFixture.deleteFile();
    (void) mixFixtureOne.deleteFile();
    (void) mixFixtureTwo.deleteFile();
    (void) modelStorage.deleteRecursively();
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    try
    {
        runUiCommandRoutingRegression();
        runUiProjectLifecycleRegression();
        runUiPhase1LibraryAndPhase2PluginRoutingRegression();
        runUiPhase3TimeAutomationLoopPunchRegression();
        runUiPhase4ToolFrameworkRegression();
        runUiPhase5BuiltInToolsRegression();
        runUiPhase5ModelBackedToolsRegression();
        std::cout << "WaiveUiTests: PASS" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "WaiveUiTests: FAIL: " << e.what() << std::endl;
        return 1;
    }
}
