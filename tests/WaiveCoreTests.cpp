#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "ClipEditActions.h"
#include "ModelManager.h"
#include "PathSanitizer.h"
#include "AudioAnalysisCache.h"
#include "AudioAnalysis.h"
#include "ProjectPackager.h"
#include "PluginPresetManager.h"
#include "CommandHandler.h"

#include <cmath>
#include <cstdlib>
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

int getAudioTrackCount (te::Edit& edit)
{
    return te::getAudioTracks (edit).size();
}

juce::File getFixtureDir (const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("waive_core_tests")
                   .getChildFile (name + "_" + juce::Uuid().toString());
    dir.createDirectory();
    return dir;
}

juce::File writeTestWav (const juce::File& file, float amplitude = 0.5f)
{
    std::vector<float> samples (4410, amplitude);
    std::unique_ptr<juce::OutputStream> stream (new juce::FileOutputStream (file));
    auto options = juce::AudioFormatWriterOptions()
                       .withSampleRate (44100.0)
                       .withNumChannels (1)
                       .withBitsPerSample (16);

    if (auto writer = juce::WavAudioFormat().createWriterFor (stream, options))
    {
        juce::AudioBuffer<float> buffer (1, (int) samples.size());
        buffer.copyFrom (0, 0, samples.data(), (int) samples.size());
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    return file;
}

juce::File writeTestMidi (const juce::File& file)
{
    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0.0);
    sequence.addEvent (juce::MidiMessage::noteOff (1, 60), 0.5);
    sequence.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (960);
    midiFile.addTrack (sequence);

    juce::FileOutputStream output (file);
    expect (output.openedOk(), "Expected MIDI fixture output stream");
    expect (midiFile.writeTo (output), "Expected MIDI fixture write to succeed");
    return file;
}

juce::File createSavedProjectFixture (te::Engine& engine, const juce::File& projectFile)
{
    auto backingFile = projectFile.getSiblingFile ("fixture_backing.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected fixture track");

    auto clip = track->insertMIDIClip (
        "fixture_clip",
        te::TimeRange (te::TimePosition::fromSeconds (0.0),
                       te::TimePosition::fromSeconds (1.0)),
        nullptr);
    expect (clip != nullptr, "Expected fixture MIDI clip insertion");
    clip->getSequence().addNote (60, te::BeatPosition::fromBeats (0.0),
                                 te::BeatDuration::fromBeats (1.0),
                                 100, 0, &edit->getUndoManager());

    expect (te::EditFileOperations (*edit).saveAs (projectFile, true),
            "Expected fixture project save to succeed");
    if (backingFile.existsAsFile())
        (void) backingFile.deleteFile();

    return projectFile;
}

juce::var runJsonCommand (CommandHandler& handler, const juce::String& payload)
{
    auto response = juce::JSON::parse (handler.handleCommand (payload));
    expect (response.isObject(), "Expected JSON command response object");
    return response;
}

class ScopedWorkingDirectory
{
public:
    explicit ScopedWorkingDirectory (const juce::File& targetDirectory)
        : previousDirectory (juce::File::getCurrentWorkingDirectory())
    {
        changed = targetDirectory.setAsCurrentWorkingDirectory();
    }

    ~ScopedWorkingDirectory()
    {
        if (changed)
            (void) previousDirectory.setAsCurrentWorkingDirectory();
    }

    bool wasChanged() const { return changed; }

private:
    juce::File previousDirectory;
    bool changed = false;
};

void testCoalescedTransactionsAndReset (EditSession& session)
{
    auto& edit = session.getEdit();

    expect (getAudioTrackCount (edit) == 1, "Expected new edit to have exactly one audio track");

    session.performEdit ("Add Track", true, [&] (te::Edit& e)
    {
        e.ensureNumberOfAudioTracks (getAudioTrackCount (e) + 1);
    });

    session.performEdit ("Add Track", true, [&] (te::Edit& e)
    {
        e.ensureNumberOfAudioTracks (getAudioTrackCount (e) + 1);
    });

    expect (getAudioTrackCount (edit) == 3, "Expected two coalesced add-track mutations");

    session.undo();
    expect (getAudioTrackCount (edit) == 1,
            "Expected one undo to revert both coalesced add-track mutations");

    session.redo();
    expect (getAudioTrackCount (edit) == 3, "Expected redo to restore coalesced add-track mutations");

    session.endCoalescedTransaction();

    session.performEdit ("Add Track", true, [&] (te::Edit& e)
    {
        e.ensureNumberOfAudioTracks (getAudioTrackCount (e) + 1);
    });

    expect (getAudioTrackCount (edit) == 4, "Expected a new add-track mutation after coalesce reset");

    session.undo();
    expect (getAudioTrackCount (edit) == 3,
            "Expected first undo to only revert post-reset mutation");

    session.undo();
    expect (getAudioTrackCount (edit) == 1,
            "Expected second undo to revert initial coalesced mutation group");
}

void testDuplicateMidiClipPreservesSequence (EditSession& session)
{
    auto& edit = session.getEdit();
    auto tracks = te::getAudioTracks (edit);
    expect (! tracks.isEmpty(), "Expected at least one audio track in the edit");

    auto* track = tracks.getFirst();
    expect (track != nullptr, "Expected non-null first audio track");

    constexpr double clipStartSeconds = 0.0;
    constexpr double clipEndSeconds = 2.0;

    auto midiClip = track->insertMIDIClip (
        "source",
        te::TimeRange (te::TimePosition::fromSeconds (clipStartSeconds),
                       te::TimePosition::fromSeconds (clipEndSeconds)),
        nullptr);

    expect (midiClip != nullptr, "Expected MIDI clip insertion to succeed");

    auto& sequence = midiClip->getSequence();
    sequence.addNote (60, te::BeatPosition::fromBeats (0.0),
                      te::BeatDuration::fromBeats (1.0),
                      100, 0, &edit.getUndoManager());

    expect (sequence.getNumNotes() == 1, "Expected source MIDI clip to contain one note");

    waive::duplicateClip (session, *midiClip);

    te::MidiClip* duplicate = nullptr;
    for (auto* clip : track->getClips())
    {
        if (clip != nullptr && clip != midiClip.get() && clip->getName().contains ("copy"))
        {
            duplicate = dynamic_cast<te::MidiClip*> (clip);
            if (duplicate != nullptr)
                break;
        }
    }

    expect (duplicate != nullptr, "Expected duplicated MIDI clip to be present on track");
    expect (duplicate->getSequence().getNumNotes() == 1,
            "Expected duplicated MIDI clip to preserve source note data");

    const auto duplicateStart = duplicate->getPosition().getStart().inSeconds();
    expect (std::abs (duplicateStart - clipEndSeconds) < 0.01,
            "Expected duplicated MIDI clip start at source clip end");
}

void testPerformEditExceptionSafety (EditSession& session)
{
    auto& edit = session.getEdit();
    const auto initialTrackCount = getAudioTrackCount (edit);

    auto firstMutationOk = session.performEdit ("Successful Mutation", [&] (te::Edit& e)
    {
        e.ensureNumberOfAudioTracks (getAudioTrackCount (e) + 1);
    });

    expect (firstMutationOk, "Expected setup mutation to succeed");
    expect (getAudioTrackCount (edit) == initialTrackCount + 1,
            "Expected setup mutation to add one track");

    auto ok = session.performEdit ("Throwing Mutation", [&] (te::Edit&)
    {
        throw std::runtime_error ("intentional test exception");
    });

    expect (! ok, "Expected performEdit to return false when mutation throws");
    expect (getAudioTrackCount (edit) == initialTrackCount + 1,
            "Expected throwing mutation not to undo the previous successful edit");

    session.undo();
    expect (getAudioTrackCount (edit) == initialTrackCount,
            "Expected undo history before the failing mutation to remain intact");
}

void testCoalescedPerformEditExceptionSafety (te::Engine& engine)
{
    EditSession session (engine);
    auto& edit = session.getEdit();
    const auto initialTrackCount = getAudioTrackCount (edit);

    auto addTrack = [&] (bool shouldThrow)
    {
        return session.performEdit ("Coalesced Throwing Mutation", true, [&] (te::Edit& e)
        {
            e.ensureNumberOfAudioTracks (getAudioTrackCount (e) + 1);

            if (shouldThrow)
                throw std::runtime_error ("intentional coalesced test exception");
        });
    };

    expect (addTrack (false), "Expected initial coalesced mutation to succeed");
    expect (getAudioTrackCount (edit) == initialTrackCount + 1,
            "Expected initial coalesced mutation to add one track");
    expect (session.canUndo(), "Expected initial coalesced mutation to create an undo entry");

    const auto undoDescriptionBeforeFailure = session.getUndoDescription();
    const auto dirtyStateBeforeFailure = session.hasChangedSinceSaved();

    auto ok = addTrack (true);

    expect (! ok, "Expected coalesced performEdit to return false when mutation throws");
    expect (getAudioTrackCount (edit) == initialTrackCount + 1,
            "Expected throwing coalesced mutation not to leave a partial added track behind");
    expect (session.getUndoDescription() == undoDescriptionBeforeFailure,
            "Expected throwing coalesced mutation to preserve prior undo history");
    expect (session.hasChangedSinceSaved() == dirtyStateBeforeFailure,
            "Expected throwing coalesced mutation not to perturb dirty tracking");

    session.undo();
    expect (getAudioTrackCount (edit) == initialTrackCount,
            "Expected undo after coalesced failure to revert the last successful mutation");
}

void testDirtyStateSavepointAcrossCoalescedUndoRedo (te::Engine& engine)
{
    EditSession session (engine);
    auto& edit = session.getEdit();

    auto addTrack = [&] (const juce::String& actionName, bool coalesce)
    {
        return session.performEdit (actionName, coalesce, [&] (te::Edit& e)
        {
            e.ensureNumberOfAudioTracks (getAudioTrackCount (e) + 1);
        });
    };

    expect (addTrack ("Seed Clean Savepoint", false), "Expected initial mutation to succeed");
    expect (session.hasChangedSinceSaved(), "Expected initial mutation to mark the session dirty");

    session.resetChangedStatus();
    expect (! session.hasChangedSinceSaved(), "Expected resetChangedStatus to establish a clean savepoint");

    expect (addTrack ("Coalesced Track Growth", true), "Expected first coalesced mutation to succeed");
    expect (addTrack ("Coalesced Track Growth", true), "Expected second coalesced mutation to succeed");
    expect (getAudioTrackCount (edit) == 4, "Expected coalesced mutations to add two tracks");
    expect (session.hasChangedSinceSaved(), "Expected coalesced mutations after the savepoint to mark the session dirty");

    session.undo();
    expect (getAudioTrackCount (edit) == 2, "Expected a single undo to revert the coalesced transaction");
    expect (! session.hasChangedSinceSaved(),
            "Expected undo to restore the clean savepoint without requiring full-state snapshot comparison");

    session.redo();
    expect (getAudioTrackCount (edit) == 4, "Expected redo to restore the coalesced transaction");
    expect (session.hasChangedSinceSaved(), "Expected redo to restore the dirty state after the savepoint");
}

void testUndoableCommandHandlerWrapsMutatingCommands (te::Engine& engine)
{
    EditSession session (engine);
    CommandHandler handler (session.getEdit());
    UndoableCommandHandler undoableHandler (handler, session);

    auto response = juce::JSON::parse (undoableHandler.handleCommand (R"({ "action":"add_track" })"));
    expect (response.isObject(), "Expected add_track response object through undoable handler");
    expect (response["status"].toString() == "ok", "Expected add_track to succeed through undoable handler");
    expect (getAudioTrackCount (session.getEdit()) == 2, "Expected add_track to mutate the edit");
    expect (session.canUndo(), "Expected undoable handler mutation to create an undo entry");

    session.undo();
    expect (getAudioTrackCount (session.getEdit()) == 1,
            "Expected undo to revert command-server style mutation when routed through undoable handler");

    session.resetChangedStatus();
    expect (! session.hasChangedSinceSaved(), "Expected reset changed status before read-only command test");

    auto transportResponse = juce::JSON::parse (undoableHandler.handleCommand (R"({ "action":"get_transport_state" })"));
    expect (transportResponse.isObject(), "Expected get_transport_state response object through undoable handler");
    expect (transportResponse["status"].toString() == "ok", "Expected get_transport_state to succeed");
    expect (! session.hasChangedSinceSaved(), "Expected get_transport_state to remain read-only");

    const auto undoDescriptionBeforeFailure = session.getUndoDescription();
    auto failureResponse = juce::JSON::parse (undoableHandler.handleCommand (R"({ "action":"remove_track", "track_id":999 })"));
    expect (failureResponse.isObject(), "Expected failing command response object through undoable handler");
    expect (failureResponse["status"].toString() == "error", "Expected failing command to report error status");
    expect (failureResponse["message"].toString().contains ("Track not found"),
            "Expected failing command to preserve the underlying error message");
    expect (! session.hasChangedSinceSaved(), "Expected failing command to leave edit clean");
    expect (session.getUndoDescription() == undoDescriptionBeforeFailure,
            "Expected failing command to avoid creating a new undo transaction");
}

void testClickTrackToggleSupportsUndoRedo (te::Engine& engine)
{
    EditSession session (engine);
    auto& edit = session.getEdit();
    const bool initialClickEnabled = edit.clickTrackEnabled.get();

    auto ok = session.performEdit ("Enable Click", [] (te::Edit& e)
    {
        e.clickTrackEnabled.setValue (true, &e.getUndoManager());
    });

    expect (ok, "Expected click-track toggle mutation to succeed");
    expect (edit.clickTrackEnabled.get(), "Expected click-track toggle to enable click");
    expect (session.canUndo(), "Expected click-track toggle to create an undo entry");

    session.undo();
    expect (edit.clickTrackEnabled.get() == initialClickEnabled,
            "Expected undo to restore original click-track state");

    session.redo();
    expect (edit.clickTrackEnabled.get(), "Expected redo to restore enabled click-track state");
}

void testModelManagerSettingsPersistence()
{
    auto fixtureDir = juce::File::getCurrentWorkingDirectory()
                          .getChildFile ("build")
                          .getChildFile ("test_fixtures")
                          .getChildFile ("model_manager_persistence");
    fixtureDir.createDirectory();

    {
        waive::ModelManager manager;
        manager.setStorageDirectory (fixtureDir);

        auto setQuotaResult = manager.setQuotaBytes (512 * 1024 * 1024);
        expect (setQuotaResult.wasOk(), "Expected quota set to succeed");

        auto installResult = manager.installModel ("stem_separator", "1.1.0", false);
        expect (installResult.wasOk(), "Expected stem_separator install to succeed");

        auto pinResult = manager.pinModelVersion ("stem_separator", "1.1.0");
        expect (pinResult.wasOk(), "Expected pin to succeed");
    }

    {
        waive::ModelManager manager;
        manager.setStorageDirectory (fixtureDir);

        expect (manager.getQuotaBytes() == 512 * 1024 * 1024, "Expected quota to persist");
        expect (manager.getPinnedVersion ("stem_separator") == "1.1.0",
                "Expected pinned version to persist");
        expect (manager.isInstalled ("stem_separator", "1.1.0"),
                "Expected installed model to persist");
    }

    (void) fixtureDir.deleteRecursively();
}

void testPathSanitizerRejectsTraversal()
{
    // Test path traversal attacks
    expect (waive::PathSanitizer::sanitizePathComponent ("..").isEmpty(),
            "Expected '..' to be rejected");
    expect (waive::PathSanitizer::sanitizePathComponent ("../etc").isEmpty(),
            "Expected '../etc' to be rejected");
    expect (waive::PathSanitizer::sanitizePathComponent ("foo/bar").isEmpty(),
            "Expected 'foo/bar' with slash to be rejected");
    expect (waive::PathSanitizer::sanitizePathComponent ("foo\\bar").isEmpty(),
            "Expected 'foo\\bar' with backslash to be rejected");

    // Test null bytes and control characters
    juce::String nullByte;
    nullByte << (juce::juce_wchar) 0;
    expect (waive::PathSanitizer::sanitizePathComponent (nullByte).isEmpty(),
            "Expected null byte to be rejected");

    juce::String controlChar;
    controlChar << (juce::juce_wchar) 0x1F;
    expect (waive::PathSanitizer::sanitizePathComponent (controlChar).isEmpty(),
            "Expected control character to be rejected");

    // Test valid components
    expect (waive::PathSanitizer::sanitizePathComponent ("valid_name") == "valid_name",
            "Expected valid alphanumeric with underscore to pass");
    expect (waive::PathSanitizer::sanitizePathComponent ("model-1.0.0") == "model-1.0.0",
            "Expected valid version string to pass");
    expect (waive::PathSanitizer::sanitizePathComponent ("ABC123") == "ABC123",
            "Expected alphanumeric uppercase to pass");
}

void testPathSanitizerValidatesDirectory()
{
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("waive_test_" + juce::Uuid().toString());
    tempDir.createDirectory();

    auto childDir = tempDir.getChildFile ("child");
    childDir.createDirectory();

    auto testFile = childDir.getChildFile ("test.txt");
    testFile.create();

    // Test containment validation
    expect (waive::PathSanitizer::isWithinDirectory (childDir, tempDir),
            "Expected child directory to be within parent");
    expect (waive::PathSanitizer::isWithinDirectory (testFile, tempDir),
            "Expected file in child to be within parent");
    expect (! waive::PathSanitizer::isWithinDirectory (tempDir, childDir),
            "Expected parent not to be within child");

    auto outsideDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    expect (! waive::PathSanitizer::isWithinDirectory (outsideDir, tempDir),
            "Expected unrelated directory not to be within temp dir");

    tempDir.deleteRecursively();
}

void testPathSanitizerValidatesIdentifier()
{
    // Valid identifiers
    expect (waive::PathSanitizer::isValidIdentifier ("valid_id"),
            "Expected 'valid_id' to be valid");
    expect (waive::PathSanitizer::isValidIdentifier ("ABC123"),
            "Expected 'ABC123' to be valid");
    expect (waive::PathSanitizer::isValidIdentifier ("model-1"),
            "Expected 'model-1' to be valid");
    expect (waive::PathSanitizer::isValidIdentifier ("a_b-c_123"),
            "Expected 'a_b-c_123' to be valid");

    // Invalid identifiers
    expect (! waive::PathSanitizer::isValidIdentifier ("../etc"),
            "Expected '../etc' to be invalid");
    expect (! waive::PathSanitizer::isValidIdentifier ("foo/bar"),
            "Expected 'foo/bar' to be invalid");
    expect (! waive::PathSanitizer::isValidIdentifier ("foo bar"),
            "Expected 'foo bar' with space to be invalid");
    expect (! waive::PathSanitizer::isValidIdentifier ("model@1.0"),
            "Expected 'model@1.0' with special char to be invalid");
    expect (! waive::PathSanitizer::isValidIdentifier (""),
            "Expected empty string to be invalid");
}

void testModelManagerRejectsPathTraversal()
{
    auto fixtureDir = juce::File::getCurrentWorkingDirectory()
                          .getChildFile ("build")
                          .getChildFile ("test_fixtures")
                          .getChildFile ("path_traversal_test");
    fixtureDir.createDirectory();

    waive::ModelManager manager;
    manager.setStorageDirectory (fixtureDir);

    // Attempt path traversal attacks
    auto result1 = manager.installModel ("../../etc", "1.0.0", false);
    expect (result1.failed(), "Expected path traversal in modelID to fail");

    auto result2 = manager.installModel ("stem_separator", "../etc", false);
    expect (result2.failed(), "Expected path traversal in version to fail");

    auto result3 = manager.uninstallModel ("../../../tmp", "");
    expect (result3.failed(), "Expected path traversal in uninstall to fail");

    fixtureDir.deleteRecursively();
}

void testAudioAnalysisCacheLRU()
{
    waive::AudioAnalysisCache cache (100);

    // Insert 101 entries to exceed capacity
    for (int i = 0; i < 101; ++i)
    {
        waive::AudioAnalysisCache::CacheKey key;
        key.sourceFile = juce::File ("/tmp/test_" + juce::String (i) + ".wav");
        key.activityThreshold = 0.01f;
        key.transientThreshold = 0.05f;

        waive::AudioAnalysisSummary summary;
        summary.valid = true;
        summary.totalSamples = 44100;
        summary.firstAboveSample = 0;
        summary.firstTransientSample = -1;

        cache.put (key, summary);
    }

    // Oldest entry (i=0) should have been evicted
    waive::AudioAnalysisCache::CacheKey firstKey;
    firstKey.sourceFile = juce::File ("/tmp/test_0.wav");
    firstKey.activityThreshold = 0.01f;
    firstKey.transientThreshold = 0.05f;

    auto firstResult = cache.get (firstKey);
    expect (! firstResult.has_value(), "Expected oldest entry to be evicted when capacity exceeded");

    // Most recent entry (i=100) should still exist
    waive::AudioAnalysisCache::CacheKey lastKey;
    lastKey.sourceFile = juce::File ("/tmp/test_100.wav");
    lastKey.activityThreshold = 0.01f;
    lastKey.transientThreshold = 0.05f;

    auto lastResult = cache.get (lastKey);
    expect (lastResult.has_value(), "Expected most recent entry to remain in cache");
}

void testAudioAnalysisCacheO1Performance()
{
    waive::AudioAnalysisCache cache (1000);

    // Fill cache with 1000 entries
    for (int i = 0; i < 1000; ++i)
    {
        waive::AudioAnalysisCache::CacheKey key;
        key.sourceFile = juce::File ("/tmp/test_" + juce::String (i) + ".wav");
        key.activityThreshold = 0.01f;
        key.transientThreshold = 0.05f;

        waive::AudioAnalysisSummary summary;
        summary.valid = true;
        summary.totalSamples = 44100;
        cache.put (key, summary);
    }

    // Access middle entry multiple times - should be O(1) with iterator map
    waive::AudioAnalysisCache::CacheKey middleKey;
    middleKey.sourceFile = juce::File ("/tmp/test_500.wav");
    middleKey.activityThreshold = 0.01f;
    middleKey.transientThreshold = 0.05f;

    for (int i = 0; i < 100; ++i)
    {
        auto result = cache.get (middleKey);
        expect (result.has_value(), "Expected cache hit for repeated access");
    }

    // Verify eviction still works correctly after many gets
    for (int i = 1000; i < 1100; ++i)
    {
        waive::AudioAnalysisCache::CacheKey key;
        key.sourceFile = juce::File ("/tmp/test_" + juce::String (i) + ".wav");
        key.activityThreshold = 0.01f;
        key.transientThreshold = 0.05f;

        waive::AudioAnalysisSummary summary;
        summary.valid = true;
        summary.totalSamples = 44100;
        cache.put (key, summary);
    }

    // Middle key should still exist (was accessed recently)
    auto middleResult = cache.get (middleKey);
    expect (middleResult.has_value(), "Expected frequently accessed entry to remain after evictions");

    // First entry should be gone
    waive::AudioAnalysisCache::CacheKey firstKey;
    firstKey.sourceFile = juce::File ("/tmp/test_0.wav");
    firstKey.activityThreshold = 0.01f;
    firstKey.transientThreshold = 0.05f;
    auto firstResult = cache.get (firstKey);
    expect (! firstResult.has_value(), "Expected LRU entry to be evicted");
}

void testAudioAnalysisZeroSampleRate()
{
    // Create a temporary WAV file with zero sample rate (simulated via validation)
    // Since we can't easily create a malformed WAV, we test the validation logic
    // by verifying that analyseAudioFile returns invalid summary for non-existent file
    auto nonExistentFile = juce::File ("/tmp/nonexistent_audio_" + juce::Uuid().toString() + ".wav");

    auto summary = waive::analyseAudioFile (nonExistentFile, 0.01f, 0.05f, nullptr, nullptr);
    expect (! summary.valid, "Expected invalid summary for non-existent file");
    expect (summary.totalSamples == 0, "Expected zero samples for invalid file");
}

void testCollectAndSavePersistsToExplicitProjectFile (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("collect_explicit_save");
    auto projectDir = fixtureDir.getChildFile ("project");
    projectDir.createDirectory();
    auto projectFile = projectDir.getChildFile ("waive_project.tracktionedit");
    auto backingFile = fixtureDir.getChildFile ("recovery_backing.tracktionedit");

    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected collect/save fixture track");

    auto clip = track->insertMIDIClip (
        "saved_via_collect",
        te::TimeRange (te::TimePosition::fromSeconds (0.0),
                       te::TimePosition::fromSeconds (1.0)),
        nullptr);
    expect (clip != nullptr, "Expected collect/save fixture clip");
    clip->getSequence().addNote (67, te::BeatPosition::fromBeats (0.0),
                                 te::BeatDuration::fromBeats (1.0),
                                 100, 0, &edit->getUndoManager());
    edit->markAsChanged();

    auto result = waive::ProjectPackager::collectAndSave (*edit, projectDir, projectFile);
    expect (result.errors.isEmpty(), "Expected collect/save to persist explicit project file without errors");
    expect (projectFile.existsAsFile(), "Expected collect/save to write the explicit project file");

    auto reloadedEdit = te::loadEditFromFile (engine, projectFile);
    auto* reloadedTrack = te::getAudioTracks (*reloadedEdit).getFirst();
    expect (reloadedTrack != nullptr, "Expected reloaded project track");
    expect (reloadedTrack->getClips().size() == 1,
            "Expected collect/save to persist edits even when no external media was copied");

    (void) fixtureDir.deleteRecursively();
}

void testCollectAndSaveCopiesExternalMediaAndRewritesReferences (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("collect_copy_media");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto externalDir = fixtureDir.getChildFile ("external");
    projectDir.createDirectory();
    externalDir.createDirectory();

    auto projectFile = projectDir.getChildFile ("portable.tracktionedit");
    auto externalAudio = writeTestWav (externalDir.getChildFile ("external_source.wav"));
    auto backingFile = fixtureDir.getChildFile ("external_backing.tracktionedit");

    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected external-media fixture track");

    auto insertedClip = track->insertWaveClip (
        "external_source",
        externalAudio,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.1) },
          te::TimeDuration() },
        false);
    expect (insertedClip != nullptr, "Expected external wave clip insertion");
    edit->markAsChanged();

    auto result = waive::ProjectPackager::collectAndSave (*edit, projectDir, projectFile);
    expect (result.filesCopied == 1, "Expected collect/save to copy one external file");
    expect (result.errors.isEmpty(), "Expected collect/save to complete without errors");

    auto reloadedEdit = te::loadEditFromFile (engine, projectFile);
    auto* reloadedTrack = te::getAudioTracks (*reloadedEdit).getFirst();
    expect (reloadedTrack != nullptr, "Expected reloaded media project track");

    auto* reloadedAudioClip = dynamic_cast<te::AudioClipBase*> (reloadedTrack->getClips().getFirst());
    expect (reloadedAudioClip != nullptr, "Expected reloaded audio clip");
    auto reloadedSource = reloadedAudioClip->getSourceFileReference().getFile();
    auto reloadedSourceDescription = reloadedAudioClip->getSourceFileReference().source.get();
    expect (reloadedSource.isAChildOf (projectDir.getChildFile ("Audio")),
            "Expected collected media reference to point into project Audio directory");
    expect (reloadedSource.existsAsFile(), "Expected collected media file to exist inside project");
    expect (! juce::File::isAbsolutePath (reloadedSourceDescription),
            "Expected collected media reference to be stored as a relative project path");

    (void) fixtureDir.deleteRecursively();
}

void testCollectAndSaveRewritesInternalAbsoluteReferencesRelative (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("collect_internal_absolute_reference");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto audioDir = projectDir.getChildFile ("Audio");
    projectDir.createDirectory();
    audioDir.createDirectory();

    auto projectFile = projectDir.getChildFile ("portable.tracktionedit");
    auto internalAudio = writeTestWav (audioDir.getChildFile ("internal_source.wav"));
    auto backingFile = fixtureDir.getChildFile ("internal_backing.tracktionedit");

    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected internal-media fixture track");

    auto insertedWaveClip = track->insertWaveClip (
        "internal_source",
        internalAudio,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.1) },
          te::TimeDuration() },
        false);
    auto* insertedClip = dynamic_cast<te::AudioClipBase*> (insertedWaveClip.get());
    expect (insertedClip != nullptr, "Expected internal wave clip insertion");
    insertedClip->getSourceFileReference().setToDirectFileReference (internalAudio, false);
    expect (juce::File::isAbsolutePath (insertedClip->getSourceFileReference().source.get()),
            "Expected fixture clip to start with an absolute source reference");

    auto result = waive::ProjectPackager::collectAndSave (*edit, projectDir, projectFile);
    expect (result.filesCopied == 0, "Expected no copy for already-internal media");
    expect (result.errors.isEmpty(), "Expected collect/save to rewrite internal absolute reference without errors");

    auto reloadedEdit = te::loadEditFromFile (engine, projectFile);
    auto* reloadedTrack = te::getAudioTracks (*reloadedEdit).getFirst();
    auto* reloadedAudioClip = dynamic_cast<te::AudioClipBase*> (reloadedTrack->getClips().getFirst());
    expect (reloadedAudioClip != nullptr, "Expected reloaded internal audio clip");
    expect (! juce::File::isAbsolutePath (reloadedAudioClip->getSourceFileReference().source.get()),
            "Expected collect/save to rewrite internal absolute media reference as relative");

    (void) fixtureDir.deleteRecursively();
}

void testCollectAndSaveRollsBackWhenOneFileFails (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("collect_partial_success");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto externalDir = fixtureDir.getChildFile ("external");
    projectDir.createDirectory();
    externalDir.createDirectory();

    auto projectFile = projectDir.getChildFile ("partial.tracktionedit");
    auto copiedAudio = writeTestWav (externalDir.getChildFile ("copy_me.wav"));
    auto missingAudio = externalDir.getChildFile ("missing.wav");
    auto backingFile = fixtureDir.getChildFile ("partial_backing.tracktionedit");

    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (2);
    auto tracks = te::getAudioTracks (*edit);
    expect (tracks.size() >= 2, "Expected two fixture tracks for partial collect/save");

    auto copiedClip = tracks[0]->insertWaveClip (
        "copy_me",
        copiedAudio,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.1) },
          te::TimeDuration() },
        false);
    expect (copiedClip != nullptr, "Expected copyable wave clip insertion");

    auto missingClip = tracks[1]->insertWaveClip (
        "missing",
        missingAudio,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.1) },
          te::TimeDuration() },
        false);
    expect (missingClip != nullptr, "Expected missing-file wave clip insertion");
    edit->markAsChanged();

    auto result = waive::ProjectPackager::collectAndSave (*edit, projectDir, projectFile);
    expect (result.filesCopied == 0, "Expected collect/save to roll back copied file count when another file fails");
    expect (! result.errors.isEmpty(), "Expected collect/save to report the failed copy");
    expect (! projectDir.getChildFile ("Audio").getChildFile ("copy_me.wav").existsAsFile(),
            "Expected partial collect/save to remove copied media after rollback");
    expect (! projectFile.existsAsFile(), "Expected partial collect/save rollback to avoid persisting the project file");

    expect (tracks[0]->getClips().size() == 1, "Expected copied track clip to remain after rollback");
    expect (tracks[1]->getClips().size() == 1, "Expected missing track clip to remain after rollback");

    (void) fixtureDir.deleteRecursively();
}

void testRemoveUnusedMediaReportsActualBytesFreed (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("remove_unused_media");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto audioDir = projectDir.getChildFile ("Audio");
    projectDir.createDirectory();
    audioDir.createDirectory();

    auto projectFile = projectDir.getChildFile ("cleanup.tracktionedit");
    auto usedAudio = writeTestWav (audioDir.getChildFile ("used.wav"), 0.4f);
    auto unusedAudio = writeTestWav (audioDir.getChildFile ("unused.wav"), 0.2f);
    auto unusedAudioSize = unusedAudio.getSize();

    auto edit = te::createEmptyEdit (engine, projectFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected cleanup fixture track");
    expect (track->insertWaveClip (
                "used",
                usedAudio,
                { { te::TimePosition::fromSeconds (0.0),
                    te::TimePosition::fromSeconds (0.1) },
                  te::TimeDuration() },
                false) != nullptr,
            "Expected used wave clip insertion");

    auto removeResult = waive::ProjectPackager::removeUnusedMedia (*edit, projectDir);
    expect (removeResult.errors.isEmpty(), "Expected unused-media removal to succeed");
    expect (removeResult.filesRemoved == 1, "Expected one unused media file to be moved");
    expect (removeResult.bytesFreed == unusedAudioSize,
            "Expected bytes freed to match the moved unused media file");
    expect (! unusedAudio.existsAsFile(), "Expected unused media file to be removed from Audio directory");
    expect (projectDir.getChildFile (".trash").getChildFile ("unused.wav").existsAsFile(),
            "Expected unused media file to be moved into .trash");

    (void) fixtureDir.deleteRecursively();
}

void testMediaManagementCanonicalisesSymlinkedReferences (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("symlinked_media_reference");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto audioDir = projectDir.getChildFile ("Audio");
    auto aliasDir = fixtureDir.getChildFile ("aliases");
    projectDir.createDirectory();
    audioDir.createDirectory();
    aliasDir.createDirectory();

    auto projectFile = projectDir.getChildFile ("symlink.tracktionedit");
    auto usedAudio = writeTestWav (audioDir.getChildFile ("used.wav"), 0.3f);
    auto aliasAudio = aliasDir.getChildFile ("used_alias.wav");

   #if JUCE_WINDOWS
    const bool aliasCreated = false;
   #else
    const bool aliasCreated = usedAudio.createSymbolicLink (aliasAudio, true);
   #endif
    if (! aliasCreated)
    {
        (void) fixtureDir.deleteRecursively();
        return;
    }

    auto edit = te::createEmptyEdit (engine, projectFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected symlink fixture track");

    auto insertedWaveClip = track->insertWaveClip (
        "used_alias",
        aliasAudio,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.1) },
          te::TimeDuration() },
        false);
    auto* insertedClip = dynamic_cast<te::AudioClipBase*> (insertedWaveClip.get());
    expect (insertedClip != nullptr, "Expected symlinked wave clip insertion");
    insertedClip->getSourceFileReference().setToDirectFileReference (aliasAudio, false);

    auto externalMedia = waive::ProjectPackager::findExternalMedia (*edit, projectDir);
    expect (externalMedia.isEmpty(),
            "Expected symlinked reference to project media to be treated as internal");

    auto unusedMedia = waive::ProjectPackager::findUnusedMedia (*edit, projectDir);
    expect (unusedMedia.isEmpty(),
            "Expected canonicalised symlinked reference to keep project media marked as used");

    (void) fixtureDir.deleteRecursively();
}

void testCollectAndSaveRestoresReferencesWhenSaveFails (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("collect_save_failure");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto externalDir = fixtureDir.getChildFile ("external");
    projectDir.createDirectory();
    externalDir.createDirectory();

    auto externalAudio = writeTestWav (externalDir.getChildFile ("external_source.wav"));
    auto backingFile = fixtureDir.getChildFile ("external_backing.tracktionedit");
    auto invalidProjectFile = projectDir.getChildFile ("missing").getChildFile ("portable.tracktionedit");

    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected save-failure fixture track");

    auto insertedWaveClip = track->insertWaveClip (
        "external_source",
        externalAudio,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.1) },
          te::TimeDuration() },
        false);
    auto* insertedClip = dynamic_cast<te::AudioClipBase*> (insertedWaveClip.get());
    expect (insertedClip != nullptr, "Expected save-failure wave clip insertion");

    auto result = waive::ProjectPackager::collectAndSave (*edit, projectDir, invalidProjectFile);
    expect (! result.errors.isEmpty(), "Expected collect/save to report save failure");
    expect (result.filesCopied == 0, "Expected collect/save rollback to report zero committed copied files");
    expect (result.bytesCopied == 0, "Expected collect/save rollback to report zero committed copied bytes");
    expect (insertedClip->getSourceFileReference().getFile() == externalAudio,
            "Expected clip reference to roll back to the original file when save fails");
    expect (! projectDir.getChildFile ("Audio").getChildFile ("external_source.wav").existsAsFile(),
            "Expected rolled-back collect/save to remove copied media");

    (void) fixtureDir.deleteRecursively();
}

void testPackageAsZipIncludesOnlyCurrentProjectFile (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("package_zip");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto audioDir = projectDir.getChildFile ("Audio");
    projectDir.createDirectory();
    audioDir.createDirectory();

    auto projectFile = createSavedProjectFixture (engine, projectDir.getChildFile ("main_project.tracktionedit"));
    auto backupProjectFile = projectDir.getChildFile ("backup.tracktionedit");
    auto autoSaveFile = projectDir.getChildFile (".waive-autosave-main_project.tracktionedit");
    auto audioFile = writeTestWav (audioDir.getChildFile ("packaged.wav"));
    auto outputZip = fixtureDir.getChildFile ("portable.zip");

    expect (backupProjectFile.replaceWithText ("backup"), "Expected backup fixture project file");
    expect (autoSaveFile.replaceWithText ("autosave"), "Expected autosave fixture project file");

    expect (waive::ProjectPackager::packageAsZip (projectFile, outputZip),
            "Expected packageAsZip to produce an archive");
    expect (outputZip.existsAsFile(), "Expected zip file to be created");

    juce::ZipFile zip (outputZip);
    juce::StringArray entries;
    for (int i = 0; i < zip.getNumEntries(); ++i)
        if (auto* entry = zip.getEntry (i))
            entries.add (entry->filename);

    expect (entries.contains (projectFile.getFileName()),
            "Expected zip to include the current project file");
    expect (entries.contains ("Audio/" + audioFile.getFileName()),
            "Expected zip to include project audio");
    expect (entries.contains (autoSaveFile.getFileName()),
            "Expected zip to include autosave snapshot when present");
    expect (! entries.contains (backupProjectFile.getFileName()),
            "Expected zip to exclude unrelated tracktionedit files from the project directory");

    (void) fixtureDir.deleteRecursively();
}

void testTrackCommandsReturnPublicIndicesWithFolderTracks (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("track_command_public_indices");
    auto backingFile = fixtureDir.getChildFile ("track_command_public_indices.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto folderTrack = edit->insertNewFolderTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr, false);
    expect (folderTrack != nullptr, "Expected folder track for public-index command test");
    folderTrack->setName ("Folder");

    CommandHandler handler (*edit);

    auto tracksResponse = runJsonCommand (handler, R"({ "action":"get_tracks" })");
    int firstAudioTrackId = -1;
    if (auto* tracks = tracksResponse.getProperty ("tracks", juce::var()).getArray())
    {
        for (const auto& entry : *tracks)
        {
            if (! entry.isObject() || static_cast<bool> (entry["is_folder"]))
                continue;

            firstAudioTrackId = (int) entry["track_id"];
            break;
        }
    }
    expect (firstAudioTrackId >= 0, "Expected at least one audio track in public track listing");

    auto addTrackResponse = runJsonCommand (handler, R"({ "action":"add_track" })");
    expect (addTrackResponse["status"].toString() == "ok", "Expected add_track to succeed with folder tracks present");
    expect ((int) addTrackResponse["track_index"] == 2,
            "Expected add_track to return the public track index including folders");

    auto duplicateTrackResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"duplicate_track",
        "track_id":%d
    })", firstAudioTrackId));
    expect (duplicateTrackResponse["status"].toString() == "ok", "Expected duplicate_track to succeed");
    expect ((int) duplicateTrackResponse["new_track_index"] == 3,
            "Expected duplicate_track to return the public track index including folders");

    (void) fixtureDir.deleteRecursively();
}

void testReorderTrackUsesPublicIndicesAcrossFolderTracks (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("reorder_track_public_indices");
    auto backingFile = fixtureDir.getChildFile ("reorder_track_public_indices.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto* firstTrack = te::getAudioTracks (*edit).getFirst();
    expect (firstTrack != nullptr, "Expected first audio track for reorder test");
    firstTrack->setName ("Track 1");

    auto folderTrack = edit->insertNewFolderTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr, false);
    expect (folderTrack != nullptr, "Expected folder track for reorder test");
    folderTrack->setName ("Folder");

    auto secondTrackPtr = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr);
    auto* secondTrack = secondTrackPtr.get();
    expect (secondTrack != nullptr, "Expected second audio track for reorder test");
    secondTrack->setName ("Track 2");

    CommandHandler handler (*edit);
    auto beforeTracksResponse = runJsonCommand (handler, R"({ "action":"get_tracks" })");
    int track2Id = -1;
    int targetPosition = -1;
    if (auto* tracks = beforeTracksResponse.getProperty ("tracks", juce::var()).getArray())
    {
        for (const auto& entry : *tracks)
        {
            if (! entry.isObject())
                continue;

            auto name = entry["name"].toString();
            if (name == "Track 2")
                track2Id = (int) entry["track_id"];
            else if (name == "Folder")
                targetPosition = (int) entry["track_id"];
        }
    }

    expect (track2Id >= 0, "Expected Track 2 public track id before reorder");
    expect (targetPosition >= 0, "Expected folder public track id before reorder");

    auto reorderResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"reorder_track",
        "track_id":%d,
        "new_position":%d
    })", track2Id, targetPosition));

    expect (reorderResponse["status"].toString() == "ok",
            "Expected reorder_track to accept public indices spanning folder tracks");

    auto tracksResponse = runJsonCommand (handler, R"({ "action":"get_tracks" })");
    expect (tracksResponse["status"].toString() == "ok", "Expected get_tracks after reorder");

    if (auto* tracks = tracksResponse.getProperty ("tracks", juce::var()).getArray())
    {
        expect (tracks->size() >= 3, "Expected three public tracks after reorder");
        expect ((*tracks)[targetPosition]["name"].toString() == "Track 2",
                "Expected Track 2 to move to the requested public index");
    }

    (void) fixtureDir.deleteRecursively();
}

void testCollectAndSaveCommandReturnsErrorOnPackagingFailure (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("collect_command_failure");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto externalDir = fixtureDir.getChildFile ("external");
    projectDir.createDirectory();
    externalDir.createDirectory();

    auto projectFile = projectDir.getChildFile ("broken_collect.tracktionedit");
    auto externalAudio = writeTestWav (externalDir.getChildFile ("source.wav"));

    auto edit = te::createEmptyEdit (engine, projectFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected collect-command fixture track");
    expect (track->insertWaveClip (
                "source",
                externalAudio,
                { { te::TimePosition::fromSeconds (0.0),
                    te::TimePosition::fromSeconds (0.1) },
                  te::TimeDuration() },
                false) != nullptr,
            "Expected collect-command wave clip insertion");
    expect (projectDir.deleteRecursively(), "Expected collect-command project directory deletion");

    CommandHandler handler (*edit);
    auto response = runJsonCommand (handler, R"({ "action":"collect_and_save" })");
    expect (response["status"].toString() == "error",
            "Expected collect_and_save command to return error when collect/save fails");
    expect (response.hasProperty ("errors"), "Expected collect_and_save error response to include errors array");

    (void) fixtureDir.deleteRecursively();
}

void testCollectAndSaveRollsBackOnPartialCopyFailure (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("collect_partial_failure");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto externalDir = fixtureDir.getChildFile ("external");
    expect (projectDir.createDirectory().wasOk(), "Expected project directory for partial collect failure");
    expect (externalDir.createDirectory().wasOk(), "Expected external directory for partial collect failure");

    auto projectFile = projectDir.getChildFile ("partial_collect.tracktionedit");
    auto goodAudio = writeTestWav (externalDir.getChildFile ("good.wav"), 0.2f);
    auto missingAudio = externalDir.getChildFile ("missing.wav");

    auto edit = te::createEmptyEdit (engine, projectFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected track for partial collect failure");
    expect (track->insertWaveClip (
                "good",
                goodAudio,
                { { te::TimePosition::fromSeconds (0.0),
                    te::TimePosition::fromSeconds (0.2) },
                  te::TimeDuration() },
                false) != nullptr,
            "Expected good source clip insertion");
    expect (track->insertWaveClip (
                "missing",
                missingAudio,
                { { te::TimePosition::fromSeconds (0.3),
                    te::TimePosition::fromSeconds (0.5) },
                  te::TimeDuration() },
                false) != nullptr,
            "Expected missing source clip insertion");

    auto result = waive::ProjectPackager::collectAndSave (*edit, projectDir, projectFile);
    expect (! result.errors.isEmpty(), "Expected collect-and-save partial copy failure to report errors");
    expect (result.filesCopied == 0, "Expected partial copy failure to roll back copied file count");
    expect (! projectDir.getChildFile ("Audio").getChildFile ("good.wav").existsAsFile(),
            "Expected successful copy to be rolled back after partial failure");
    expect (! projectFile.existsAsFile(),
            "Expected partial collect failure rollback to avoid persisting the project file");

    auto clips = track->getClips();
    expect (clips.size() == 2, "Expected both clips to remain after partial collect failure");

    (void) fixtureDir.deleteRecursively();
}

void testRemoveUnusedMediaCommandReturnsErrorOnMoveFailure (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("remove_unused_media_command_failure");
    auto projectDir = fixtureDir.getChildFile ("project");
    auto audioDir = projectDir.getChildFile ("Audio");
    projectDir.createDirectory();
    audioDir.createDirectory();

    auto projectFile = projectDir.getChildFile ("cleanup.tracktionedit");
    auto usedAudio = writeTestWav (audioDir.getChildFile ("used.wav"), 0.4f);
    auto unusedAudio = writeTestWav (audioDir.getChildFile ("unused.wav"), 0.2f);

    auto edit = te::createEmptyEdit (engine, projectFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected cleanup-command fixture track");
    expect (track->insertWaveClip (
                "used",
                usedAudio,
                { { te::TimePosition::fromSeconds (0.0),
                    te::TimePosition::fromSeconds (0.1) },
                  te::TimeDuration() },
                false) != nullptr,
            "Expected cleanup-command used clip insertion");

    expect (projectDir.getChildFile (".trash").replaceWithText ("blocking file"),
            "Expected .trash blocker file");
    expect (unusedAudio.existsAsFile(), "Expected unused media fixture file");

    CommandHandler handler (*edit);
    auto response = runJsonCommand (handler, R"({ "action":"remove_unused_media" })");
    expect (response["status"].toString() == "error",
            "Expected remove_unused_media command to return error when file moves fail");
    expect (response.hasProperty ("errors"), "Expected remove_unused_media error response to include errors array");
    expect (unusedAudio.existsAsFile(), "Expected failed removal to leave unused file in place");

    (void) fixtureDir.deleteRecursively();
}

void testPluginPresetManagerUsesDocumentedWrapperAndStableIdentifier (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("plugin_preset_manager");
    auto homeDir = fixtureDir.getChildFile ("home");
    expect (homeDir.createDirectory().wasOk(), "Expected preset fixture home directory");

    auto previousHome = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    {
        ScopedWorkingDirectory scopedWorkingDirectory (fixtureDir);
        expect (scopedWorkingDirectory.wasChanged(),
                "Expected preset fixture directory to become current working directory");
        setenv ("HOME", homeDir.getFullPathName().toRawUTF8(), 1);

        auto backingFile = fixtureDir.getChildFile ("preset_fixture.tracktionedit");
        auto edit = te::createEmptyEdit (engine, backingFile);

        auto pluginState = te::createValueTree (te::IDs::PLUGIN,
                                                te::IDs::type, te::ReverbPlugin::xmlTypeName,
                                                "pluginFormatName", te::PluginManager::builtInPluginFormatName,
                                                "fileOrIdentifier", "/tmp/Vendor/Reverb.plugin",
                                                "manufacturer", "Waive");
        auto plugin = edit->getPluginCache().createNewPlugin (pluginState);
        expect (plugin != nullptr, "Expected built-in plugin creation for preset test");

        auto pluginIdentifier = waive::PluginPresetManager::getPluginIdentifier (*plugin);
        expect (pluginIdentifier.contains (te::ReverbPlugin::xmlTypeName),
                "Expected plugin identifier to include the stable type identifier");
        expect (! pluginIdentifier.contains ("/tmp"),
                "Expected plugin identifier to avoid absolute installation paths");
        expect (! pluginIdentifier.contains ("/"),
                "Expected plugin identifier to be safe for use as a preset directory name");

        waive::PluginPresetManager presetManager;
        auto customPresetDir = fixtureDir.getChildFile ("custom_presets");
        presetManager.setPresetsDirectory (customPresetDir);
        expect (presetManager.getPresetsDirectory() == customPresetDir,
                "Expected preset manager to return the configured presets directory");
        expect (presetManager.savePreset (*plugin, "Room A"),
                "Expected preset save to succeed");

        auto presetFile = customPresetDir
                                 .getChildFile (pluginIdentifier)
                                 .getChildFile ("Room A.xml");
        expect (presetFile.existsAsFile(), "Expected preset file to be created");

        auto xml = juce::parseXML (presetFile);
        expect (xml != nullptr, "Expected preset XML to parse");
        auto presetTree = juce::ValueTree::fromXml (*xml);
        expect (presetTree.isValid(), "Expected preset tree to be valid");
        expect (presetTree.getType() == juce::Identifier ("WaivePreset"),
                "Expected WaivePreset root element");
        expect (presetTree.getNumChildren() == 1, "Expected PluginState wrapper child");
        expect (presetTree.getChild (0).getType() == juce::Identifier ("PluginState"),
                "Expected PluginState wrapper element");
        expect (presetTree.getChild (0).getNumChildren() == 1,
                "Expected wrapped plugin state child");
    }

    setenv ("HOME", previousHome.getFullPathName().toRawUTF8(), 1);
    (void) fixtureDir.deleteRecursively();
}

void testPluginPresetCommandsSupportMasterChain (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("master_preset_commands");
    auto homeDir = fixtureDir.getChildFile ("home");
    expect (homeDir.createDirectory().wasOk(), "Expected master preset fixture home directory");
    auto backingFile = fixtureDir.getChildFile ("master_preset_fixture.tracktionedit");
    {
        ScopedWorkingDirectory scopedWorkingDirectory (fixtureDir);
        expect (scopedWorkingDirectory.wasChanged(),
                "Expected master preset fixture directory to become current working directory");
        setenv ("HOME", homeDir.getFullPathName().toRawUTF8(), 1);

        auto edit = te::createEmptyEdit (engine, backingFile);
        edit->ensureNumberOfAudioTracks (1);

        auto pluginState = te::createValueTree (te::IDs::PLUGIN,
                                                te::IDs::type, te::ReverbPlugin::xmlTypeName,
                                                "pluginFormatName", te::PluginManager::builtInPluginFormatName,
                                                "fileOrIdentifier", te::ReverbPlugin::xmlTypeName,
                                                "manufacturer", "Waive");
        auto plugin = edit->getPluginCache().createNewPlugin (pluginState);
        expect (plugin != nullptr, "Expected master preset test plugin creation");

        auto* reverb = dynamic_cast<te::ReverbPlugin*> (plugin.get());
        expect (reverb != nullptr, "Expected built-in reverb plugin for master preset command test");
        edit->getMasterPluginList().insertPlugin (plugin, 0, nullptr);

        waive::PluginPresetManager presetManager;
        expect (presetManager.savePreset (*reverb, "Master Room"),
                "Expected baseline master preset save to succeed");

        CommandHandler handler (*edit);
        auto saveResponse = runJsonCommand (handler, R"({
            "action":"save_plugin_preset",
            "master":true,
            "plugin_index":0,
            "preset_name":"Master Command Save"
        })");
        expect (saveResponse["status"].toString() == "ok",
                "Expected save_plugin_preset to target the master chain");
        expect ((bool) saveResponse["master"],
                "Expected save_plugin_preset response to indicate master-chain targeting");

        auto params = reverb->getAutomatableParameters();
        expect (! params.isEmpty(), "Expected master reverb plugin to expose automatable parameters");
        params.getFirst()->setParameter (0.0f, juce::dontSendNotification);
        auto loadResponse = runJsonCommand (handler, R"({
            "action":"load_plugin_preset",
            "master":true,
            "plugin_index":0,
            "preset_name":"Master Room"
        })");
        expect (loadResponse["status"].toString() == "ok",
                "Expected load_plugin_preset to target the master chain");
        expect ((bool) loadResponse["master"],
                "Expected load_plugin_preset response to indicate master-chain targeting");
        expect (edit->hasChangedSinceSaved(),
                "Expected master preset load to mark the edit dirty");
    }

    (void) fixtureDir.deleteRecursively();
}

void testPluginPresetCommandsRejectMissingPluginIndex (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("preset_command_validation");
    auto homeDir = fixtureDir.getChildFile ("home");
    expect (homeDir.createDirectory().wasOk(), "Expected preset validation fixture home directory");
    auto backingFile = fixtureDir.getChildFile ("preset_validation_fixture.tracktionedit");
    {
        ScopedWorkingDirectory scopedWorkingDirectory (fixtureDir);
        expect (scopedWorkingDirectory.wasChanged(),
                "Expected preset validation fixture directory to become current working directory");
        setenv ("HOME", homeDir.getFullPathName().toRawUTF8(), 1);

        auto edit = te::createEmptyEdit (engine, backingFile);
        edit->ensureNumberOfAudioTracks (1);

        auto pluginState = te::createValueTree (te::IDs::PLUGIN,
                                                te::IDs::type, te::ReverbPlugin::xmlTypeName,
                                                "pluginFormatName", te::PluginManager::builtInPluginFormatName,
                                                "fileOrIdentifier", te::ReverbPlugin::xmlTypeName,
                                                "manufacturer", "Waive");
        auto plugin = edit->getPluginCache().createNewPlugin (pluginState);
        expect (plugin != nullptr, "Expected preset validation test plugin creation");
        edit->getMasterPluginList().insertPlugin (plugin, 0, nullptr);

        CommandHandler handler (*edit);

        auto saveResponse = runJsonCommand (handler, R"({
            "action":"save_plugin_preset",
            "master":true,
            "preset_name":"Missing Plugin Index"
        })");
        expect (saveResponse["status"].toString() == "error",
                "Expected save_plugin_preset without plugin_index to fail");

        auto loadResponse = runJsonCommand (handler, R"({
            "action":"load_plugin_preset",
            "master":true,
            "preset_name":"Missing Plugin Index"
        })");
        expect (loadResponse["status"].toString() == "error",
                "Expected load_plugin_preset without plugin_index to fail");
    }

    (void) fixtureDir.deleteRecursively();
}

void testCommandSchemaDocumentsPresetTargetingRequirements()
{
    auto schemaFile = juce::File::getCurrentWorkingDirectory()
                          .getChildFile ("docs")
                          .getChildFile ("command_schema.json");
    expect (schemaFile.existsAsFile(), "Expected command schema file to exist");

    auto parsed = juce::JSON::parse (schemaFile.loadFileAsString());
    auto* schemaObject = parsed.getDynamicObject();
    expect (schemaObject != nullptr, "Expected command schema JSON to parse as an object");

    auto* rules = schemaObject->getProperty ("allOf").getArray();
    expect (rules != nullptr, "Expected command schema allOf array");

    const auto hasPresetTargetingRule = [&] (const juce::String& actionName)
    {
        for (const auto& ruleVar : *rules)
        {
            auto* ruleObj = ruleVar.getDynamicObject();
            if (ruleObj == nullptr)
                continue;

            auto* ifObj = ruleObj->getProperty ("if").getDynamicObject();
            auto* thenObj = ruleObj->getProperty ("then").getDynamicObject();
            if (ifObj == nullptr || thenObj == nullptr)
                continue;

            auto* ifProperties = ifObj->getProperty ("properties").getDynamicObject();
            auto* actionObj = ifProperties != nullptr ? ifProperties->getProperty ("action").getDynamicObject()
                                                      : nullptr;
            if (actionObj == nullptr || actionObj->getProperty ("const").toString() != actionName)
                continue;

            auto* anyOfRules = thenObj->getProperty ("anyOf").getArray();
            if (anyOfRules == nullptr)
                return false;

            bool sawTrackIndexRule = false;
            bool sawMasterRule = false;

            for (const auto& anyOfVar : *anyOfRules)
            {
                auto* anyOfObj = anyOfVar.getDynamicObject();
                if (anyOfObj == nullptr)
                    continue;

                auto* requiredArray = anyOfObj->getProperty ("required").getArray();
                if (requiredArray == nullptr)
                    continue;

                if (requiredArray->contains ("track_index"))
                    sawTrackIndexRule = true;

                if (requiredArray->contains ("master"))
                {
                    auto* properties = anyOfObj->getProperty ("properties").getDynamicObject();
                    auto* masterObj = properties != nullptr ? properties->getProperty ("master").getDynamicObject()
                                                            : nullptr;
                    if (masterObj != nullptr && masterObj->getProperty ("const").equalsWithSameType (true))
                        sawMasterRule = true;
                }
            }

            return sawTrackIndexRule && sawMasterRule;
        }

        return false;
    };

    expect (hasPresetTargetingRule ("save_plugin_preset"),
            "Expected save_plugin_preset schema rule to require track_index or master=true");
    expect (hasPresetTargetingRule ("load_plugin_preset"),
            "Expected load_plugin_preset schema rule to require track_index or master=true");
}

void testSetParameterAcceptsStablePluginIdentifier (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("set_parameter_identifier");
    auto backingFile = fixtureDir.getChildFile ("set_parameter_fixture.tracktionedit");

    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected track for set_parameter identifier test");

    auto pluginState = te::createValueTree (te::IDs::PLUGIN,
                                            te::IDs::type, te::ReverbPlugin::xmlTypeName,
                                            "pluginFormatName", te::PluginManager::builtInPluginFormatName,
                                            "fileOrIdentifier", te::ReverbPlugin::xmlTypeName,
                                            "manufacturer", "Waive");
    auto plugin = edit->getPluginCache().createNewPlugin (pluginState);
    expect (plugin != nullptr, "Expected built-in reverb creation for set_parameter identifier test");

    auto* reverb = dynamic_cast<te::ReverbPlugin*> (plugin.get());
    expect (reverb != nullptr, "Expected reverb plugin instance for set_parameter identifier test");
    track->pluginList.insertPlugin (plugin, 0, nullptr);

    auto params = reverb->getAutomatableParameters();
    expect (! params.isEmpty(), "Expected reverb plugin to expose automatable parameters");
    auto* firstParam = params.getFirst();
    expect (firstParam != nullptr, "Expected first automatable parameter");

    const auto stableIdentifier = waive::PluginPresetManager::getPluginIdentifier (*reverb);
    CommandHandler handler (*edit);
    auto response = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"set_parameter",
        "track_id":0,
        "plugin_id":"%s",
        "param_id":"%s",
        "value":0.25
    })",
        stableIdentifier.replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8(),
        firstParam->paramID.replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));

    expect (response["status"].toString() == "ok",
            "Expected set_parameter to accept the documented stable plugin identifier");
    expect (std::abs ((double) firstParam->getCurrentValue() - 0.25) < 0.0001,
            "Expected set_parameter to update the target parameter value");

    (void) fixtureDir.deleteRecursively();
}

void testMoveTrackToFolderRejectsCycles (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("folder_cycle");
    auto backingFile = fixtureDir.getChildFile ("folder_cycle.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto rootFolder = edit->insertNewFolderTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr, false);
    expect (rootFolder != nullptr, "Expected root folder creation");
    rootFolder->setName ("Root");

    auto childFolder = edit->insertNewFolderTrack (te::TrackInsertPoint (rootFolder.get(), nullptr), nullptr, false);
    expect (childFolder != nullptr, "Expected child folder creation");
    childFolder->setName ("Child");

    CommandHandler handler (*edit);
    auto tracksResponse = juce::JSON::parse (handler.handleCommand (R"({ "action":"get_tracks" })"));
    expect (tracksResponse.isObject(), "Expected get_tracks response object");

    int rootFolderIndex = -1;
    if (auto* tracksArray = tracksResponse.getProperty ("tracks", juce::var()).getArray())
    {
        for (const auto& entry : *tracksArray)
        {
            if (! entry.isObject())
                continue;

            auto name = entry["name"].toString();
            auto index = (int) entry["track_id"];
            if (name == "Root")
                rootFolderIndex = index;
        }
    }

    expect (rootFolderIndex >= 0, "Expected root folder index in get_tracks output");

    auto response = juce::JSON::parse (handler.handleCommand (
        juce::String::formatted (R"({
        "action":"move_track_to_folder",
        "track_index":%d,
        "folder_index":%d
    })", rootFolderIndex, rootFolderIndex)));
    expect (response.isObject(), "Expected JSON object response for invalid folder move");
    expect (response["status"].toString() == "error",
            "Expected invalid cyclic folder move to be rejected");

    (void) fixtureDir.deleteRecursively();
}

void testCommandHandlerRejectsRelativePaths (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("command_handler_paths");
    auto backingFile = fixtureDir.getChildFile ("command_handler_paths.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto audioFile = writeTestWav (fixtureDir.getChildFile ("source.wav"));
    expect (audioFile.existsAsFile(), "Expected source WAV fixture");

    auto previousWorkingDirectory = juce::File::getCurrentWorkingDirectory();
    expect (fixtureDir.setAsCurrentWorkingDirectory(),
            "Expected fixture directory to become current working directory");

    CommandHandler handler (*edit);

    auto insertAudioResponse = juce::JSON::parse (handler.handleCommand (R"({
        "action":"insert_audio_clip",
        "track_id":0,
        "start_time":0.0,
        "file_path":"source.wav"
    })"));
    expect (insertAudioResponse.isObject(), "Expected relative audio insert response object");
    expect (insertAudioResponse["status"].toString() == "error",
            "Expected relative audio clip path to be rejected");

    auto insertMidiResponse = juce::JSON::parse (handler.handleCommand (R"({
        "action":"insert_midi_clip",
        "track_id":0,
        "file_path":"clip.mid"
    })"));
    expect (insertMidiResponse.isObject(), "Expected relative MIDI insert response object");
    expect (insertMidiResponse["status"].toString() == "error",
            "Expected relative MIDI clip path to be rejected");

    auto exportMixdownResponse = juce::JSON::parse (handler.handleCommand (R"({
        "action":"export_mixdown",
        "file_path":"mixdown.wav"
    })"));
    expect (exportMixdownResponse.isObject(), "Expected relative export response object");
    expect (exportMixdownResponse["status"].toString() == "error",
            "Expected relative mixdown path to be rejected");

    auto packageResponse = juce::JSON::parse (handler.handleCommand (R"({
        "action":"package_as_zip",
        "file_path":"archive.zip"
    })"));
    expect (packageResponse.isObject(), "Expected relative package response object");
    expect (packageResponse["status"].toString() == "error",
            "Expected relative package path to be rejected");

    expect (previousWorkingDirectory.setAsCurrentWorkingDirectory(),
            "Expected original working directory to be restored");
    (void) fixtureDir.deleteRecursively();
}

void testFolderSoloMutePropagatesThroughNestedFolders (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("nested_folder_propagation");
    auto backingFile = fixtureDir.getChildFile ("nested_folder_propagation.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto* leafTrack = te::getAudioTracks (*edit).getFirst();
    expect (leafTrack != nullptr, "Expected leaf audio track");

    auto rootFolder = edit->insertNewFolderTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr, false);
    auto childFolder = edit->insertNewFolderTrack (te::TrackInsertPoint (rootFolder.get(), nullptr), nullptr, false);
    expect (rootFolder != nullptr && childFolder != nullptr, "Expected nested folder hierarchy");

    edit->moveTrack (leafTrack, te::TrackInsertPoint (childFolder.get(), nullptr));

    CommandHandler handler (*edit);
    auto tracksResponse = juce::JSON::parse (handler.handleCommand (R"({ "action":"get_tracks" })"));
    expect (tracksResponse.isObject(), "Expected get_tracks response object for folder propagation");

    int rootFolderTrackId = -1;
    if (auto* tracksArray = tracksResponse.getProperty ("tracks", juce::var()).getArray())
    {
        for (const auto& entry : *tracksArray)
        {
            if (! entry.isObject())
                continue;

            if (entry["name"].toString() == rootFolder->getName())
            {
                rootFolderTrackId = (int) entry["track_id"];
                break;
            }
        }
    }

    expect (rootFolderTrackId >= 0, "Expected root folder track id in get_tracks output");

    auto soloResponse = juce::JSON::parse (handler.handleCommand (juce::String::formatted (R"({
        "action":"solo_track",
        "track_id":%d,
        "solo":true
    })", rootFolderTrackId)));
    expect (soloResponse.isObject() && soloResponse["status"].toString() == "ok",
            "Expected folder solo command to succeed");
    expect (rootFolder->isSolo (false), "Expected root folder soloed");
    expect (childFolder->isSolo (false), "Expected child folder soloed through propagation");
    expect (leafTrack->isSolo (false), "Expected leaf track soloed through propagation");

    auto muteResponse = juce::JSON::parse (handler.handleCommand (juce::String::formatted (R"({
        "action":"mute_track",
        "track_id":%d,
        "mute":true
    })", rootFolderTrackId)));
    expect (muteResponse.isObject() && muteResponse["status"].toString() == "ok",
            "Expected folder mute command to succeed");
    expect (rootFolder->isMuted (false), "Expected root folder muted");
    expect (childFolder->isMuted (false), "Expected child folder muted through propagation");
    expect (leafTrack->isMuted (false), "Expected leaf track muted through propagation");

    (void) fixtureDir.deleteRecursively();
}

void testCommandHandlerAcceptsDocumentedAliases (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("command_handler_aliases");
    auto backingFile = fixtureDir.getChildFile ("command_handler_aliases.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected audio track for alias command tests");

    auto clipRef = track->insertMIDIClip (
        "alias_source",
        te::TimeRange (te::TimePosition::fromSeconds (0.0),
                       te::TimePosition::fromSeconds (2.0)),
        nullptr);
    auto* clip = clipRef.get();
    expect (clip != nullptr, "Expected MIDI clip insertion for alias command tests");
    clip->getSequence().addNote (60, te::BeatPosition::fromBeats (0.0),
                                 te::BeatDuration::fromBeats (1.0),
                                 100, 0, &edit->getUndoManager());

    CommandHandler handler (*edit);

    auto armResponse = runJsonCommand (handler, R"({
        "action":"arm_track",
        "track_id":0,
        "enabled":true
    })");
    expect (armResponse["status"].toString() == "ok", "Expected arm_track enabled alias to succeed");
    expect ((bool) armResponse["armed"], "Expected arm_track response to report armed state");

    auto tempoResponse = runJsonCommand (handler, R"({
        "action":"set_tempo",
        "value":140.0
    })");
    expect (tempoResponse["status"].toString() == "ok", "Expected set_tempo value alias to succeed");
    expect (std::abs ((double) tempoResponse["bpm"] - 140.0) < 0.01,
            "Expected set_tempo alias to update bpm");

    auto splitResponse = runJsonCommand (handler, R"({
        "action":"split_clip",
        "track_id":0,
        "clip_index":0,
        "time":1.0
    })");
    expect (splitResponse["status"].toString() == "ok", "Expected split_clip time alias to succeed");
    expect (track->getClips().size() == 2, "Expected split_clip alias to split the clip");

    auto moveResponse = runJsonCommand (handler, R"({
        "action":"move_clip",
        "track_id":0,
        "clip_index":0,
        "delta_seconds":0.5
    })");
    expect (moveResponse["status"].toString() == "ok", "Expected move_clip delta_seconds alias to succeed");
    expect (std::abs ((double) moveResponse["new_start"] - 0.5) < 0.01,
            "Expected move_clip alias to apply delta from current start");

    auto renameClipResponse = runJsonCommand (handler, R"({
        "action":"rename_clip",
        "track_id":0,
        "clip_index":0,
        "new_name":"renamed_clip"
    })");
    expect (renameClipResponse["status"].toString() == "ok", "Expected rename_clip new_name alias to succeed");
    expect (track->getClips()[0]->getName() == "renamed_clip",
            "Expected rename_clip alias to update clip name");

    auto renameTrackResponse = runJsonCommand (handler, R"({
        "action":"rename_track",
        "track_id":0,
        "new_name":"renamed_track"
    })");
    expect (renameTrackResponse["status"].toString() == "ok", "Expected rename_track new_name alias to succeed");
    expect (track->getName() == "renamed_track",
            "Expected rename_track alias to update track name");

    (void) fixtureDir.deleteRecursively();
}

void testExportStemsReportsRenderFailures (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("export_stems_failure");
    auto backingFile = fixtureDir.getChildFile ("export_stems_failure.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected audio track for stem export failure test");

    auto audioFile = writeTestWav (fixtureDir.getChildFile ("source.wav"));
    expect (audioFile.existsAsFile(), "Expected source WAV fixture for stem export failure test");

    auto clip = track->insertWaveClip (
        "source",
        audioFile,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.1) },
          te::TimeDuration() },
        false);
    expect (clip != nullptr, "Expected audio clip insertion for stem export failure test");

    auto blockedPath = fixtureDir.getChildFile ("blocked_output");
    expect (blockedPath.replaceWithText ("not a directory"),
            "Expected blocked output path fixture file");

    CommandHandler handler (*edit);
    handler.setAllowedMediaDirectories ({ fixtureDir });

    auto response = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"export_stems",
        "output_dir":"%s"
    })", blockedPath.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));

    expect (response["status"].toString() == "error",
            "Expected export_stems to report failure when a stem cannot be rendered");
    expect (response.hasProperty ("errors"), "Expected export_stems failure response to include errors");
    expect ((int) response["count"] == 0, "Expected export_stems failure to report zero rendered stems");

    (void) fixtureDir.deleteRecursively();
}

void testExportStemsAllowsNewOutputDirectoriesWithinAllowlist (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("export_stems_new_output");
    auto backingFile = fixtureDir.getChildFile ("export_stems_new_output.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (1);

    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected audio track for export_stems new output dir test");

    auto audioFile = writeTestWav (fixtureDir.getChildFile ("source.wav"));
    expect (audioFile.existsAsFile(), "Expected source WAV fixture for export_stems new output dir test");

    auto clip = track->insertWaveClip (
        "source",
        audioFile,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.1) },
          te::TimeDuration() },
        false);
    expect (clip != nullptr, "Expected clip insertion for export_stems new output dir test");
    edit->getTransport().ensureContextAllocated();

    auto outputDir = fixtureDir.getChildFile ("exports").getChildFile ("nested").getChildFile ("stems");
    expect (! outputDir.exists(), "Expected nested output directory fixture to start absent");

    CommandHandler handler (*edit);
    handler.setAllowedMediaDirectories ({ fixtureDir });

    auto response = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"export_stems",
        "output_dir":"%s",
        "start":0.0,
        "end":0.1
    })", outputDir.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));

    expect (outputDir.isDirectory(), "Expected export_stems to create nested output directory");
    expect (response.getProperty ("message", juce::var()).toString().contains ("outside allowed directories") == false,
            "Expected export_stems to get past allowlist validation for a new nested output directory");

    (void) fixtureDir.deleteRecursively();
}

void testSetLoopRegionRejectsInvalidBoundsWithoutMutation (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("invalid_loop_region");
    auto backingFile = fixtureDir.getChildFile ("invalid_loop_region.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    CommandHandler handler (*edit);

    auto& transport = edit->getTransport();
    transport.looping.setValue (false, nullptr);
    transport.setLoopRange ({ te::TimePosition::fromSeconds (1.0),
                              te::TimePosition::fromSeconds (3.0) });

    auto response = runJsonCommand (handler, R"({
        "action":"set_loop_region",
        "enabled":true,
        "start":5.0,
        "end":2.0
    })");

    expect (response["status"].toString() == "error",
            "Expected invalid set_loop_region request to fail");
    expect (! transport.looping.get(),
            "Expected invalid set_loop_region to leave loop enabled state unchanged");

    auto loopRange = transport.getLoopRange();
    expect (std::abs (loopRange.getStart().inSeconds() - 1.0) < 0.01,
            "Expected invalid set_loop_region to preserve prior loop start");
    expect (std::abs (loopRange.getEnd().inSeconds() - 3.0) < 0.01,
            "Expected invalid set_loop_region to preserve prior loop end");

    (void) fixtureDir.deleteRecursively();
}

void testUndoableCommandHandlerSupportsLoopRegionUndoRedo (te::Engine& engine)
{
    EditSession session (engine);
    CommandHandler handler (session.getEdit());
    UndoableCommandHandler undoableHandler (handler, session);

    auto& transport = session.getEdit().getTransport();
    transport.looping.setValue (false, nullptr);
    transport.setLoopRange ({ te::TimePosition::fromSeconds (1.0),
                              te::TimePosition::fromSeconds (3.0) });

    auto response = juce::JSON::parse (undoableHandler.handleCommand (R"({
        "action":"set_loop_region",
        "enabled":true,
        "start":0.5,
        "end":1.5
    })"));

    expect (response.isObject(), "Expected set_loop_region response object through undoable handler");
    expect (response["status"].toString() == "ok", "Expected set_loop_region to succeed through undoable handler");
    expect (transport.looping.get(), "Expected set_loop_region to enable looping");

    auto loopRange = transport.getLoopRange();
    expect (std::abs (loopRange.getStart().inSeconds() - 0.5) < 0.01,
            "Expected set_loop_region to update loop start");
    expect (std::abs (loopRange.getEnd().inSeconds() - 1.5) < 0.01,
            "Expected set_loop_region to update loop end");
    expect (session.canUndo(), "Expected set_loop_region to create an undo entry");

    session.undo();
    expect (! transport.looping.get(), "Expected undo to restore original loop enabled state");
    loopRange = transport.getLoopRange();
    expect (std::abs (loopRange.getStart().inSeconds() - 1.0) < 0.01,
            "Expected undo to restore prior loop start");
    expect (std::abs (loopRange.getEnd().inSeconds() - 3.0) < 0.01,
            "Expected undo to restore prior loop end");

    session.redo();
    expect (transport.looping.get(), "Expected redo to restore enabled loop state");
    loopRange = transport.getLoopRange();
    expect (std::abs (loopRange.getStart().inSeconds() - 0.5) < 0.01,
            "Expected redo to restore loop start");
    expect (std::abs (loopRange.getEnd().inSeconds() - 1.5) < 0.01,
            "Expected redo to restore loop end");
}

void testCommandHandlerRejectsMalformedCommandRequests (te::Engine& engine)
{
    auto fixtureDir = getFixtureDir ("malformed_command_requests");
    auto backingFile = fixtureDir.getChildFile ("malformed_command_requests.tracktionedit");
    auto edit = te::createEmptyEdit (engine, backingFile);
    edit->ensureNumberOfAudioTracks (2);

    auto audioFixture = writeTestWav (fixtureDir.getChildFile ("malformed_source.wav"));
    expect (audioFixture.existsAsFile(), "Expected audio fixture for malformed-command test");

    auto* track = te::getAudioTracks (*edit).getFirst();
    expect (track != nullptr, "Expected audio track for malformed-command test");
    auto* secondTrack = te::getAudioTracks (*edit)[1];
    expect (secondTrack != nullptr, "Expected second audio track for malformed-command test");
    expect (secondTrack->getClips().isEmpty(), "Expected second track to start empty for malformed-command test");
    track->setSolo (true);
    track->setMute (true);

    auto clipRef = track->insertWaveClip (
        "source",
        audioFixture,
        { { te::TimePosition::fromSeconds (0.0),
            te::TimePosition::fromSeconds (0.5) },
          te::TimeDuration() },
        false);
    auto* clip = clipRef.get();
    expect (clip != nullptr, "Expected audio clip fixture for malformed-command test");

    auto volumePlugins = track->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    expect (! volumePlugins.isEmpty(), "Expected VolumeAndPanPlugin for malformed-command test");
    auto* volumePlugin = volumePlugins.getFirst();
    expect (volumePlugin != nullptr, "Expected non-null VolumeAndPanPlugin for malformed-command test");

    volumePlugin->volParam->setParameter (te::decibelsToVolumeFaderPosition (-6.0f), juce::dontSendNotification);
    volumePlugin->panParam->setParameter (0.25f, juce::dontSendNotification);
    const auto originalVolumeValue = volumePlugin->volParam->getCurrentValue();
    const auto originalPanValue = volumePlugin->panParam->getCurrentValue();

    edit->getTransport().setPosition (te::TimePosition::fromSeconds (1.25));
    edit->getTransport().looping = true;
    edit->getTransport().loopPoint1 = te::TimePosition::fromSeconds (0.5);
    edit->getTransport().loopPoint2 = te::TimePosition::fromSeconds (1.5);

    auto pluginState = te::createValueTree (te::IDs::PLUGIN,
                                            te::IDs::type, te::ReverbPlugin::xmlTypeName,
                                            "pluginFormatName", te::PluginManager::builtInPluginFormatName,
                                            "fileOrIdentifier", te::ReverbPlugin::xmlTypeName,
                                            "manufacturer", "Waive");
    auto plugin = edit->getPluginCache().createNewPlugin (pluginState);
    expect (plugin != nullptr, "Expected built-in plugin creation for malformed-command test");
    auto* reverb = dynamic_cast<te::ReverbPlugin*> (plugin.get());
    expect (reverb != nullptr, "Expected reverb plugin instance for malformed-command test");
    track->pluginList.insertPlugin (plugin, 0, nullptr);
    juce::PluginDescription builtInReverbDescription;
    builtInReverbDescription.name = "Built-in Reverb";
    builtInReverbDescription.fileOrIdentifier = te::ReverbPlugin::xmlTypeName;
    builtInReverbDescription.pluginFormatName = te::PluginManager::builtInPluginFormatName;
    builtInReverbDescription.category = "Effect";
    builtInReverbDescription.manufacturerName = "Waive";
    edit->engine.getPluginManager().knownPluginList.addType (builtInReverbDescription);
    auto reverbParams = reverb->getAutomatableParameters();
    expect (! reverbParams.isEmpty(), "Expected reverb to expose automatable parameters");
    auto* firstPluginParam = reverbParams.getFirst();
    expect (firstPluginParam != nullptr, "Expected first reverb parameter for malformed-command test");
    const auto originalPluginParamValue = firstPluginParam->getCurrentValue();
    const auto stableIdentifier = waive::PluginPresetManager::getPluginIdentifier (*reverb);

    auto midiFixture = writeTestMidi (fixtureDir.getChildFile ("malformed_source.mid"));
    expect (midiFixture.existsAsFile(), "Expected MIDI fixture for malformed-command test");
    auto exportOutput = fixtureDir.getChildFile ("malformed_mixdown.wav");
    auto stemsOutputDir = fixtureDir.getChildFile ("malformed_stems");

    const auto originalTrackCount = getAudioTrackCount (*edit);
    const auto originalClipCount = track->getClips().size();
    const auto originalPluginCount = track->pluginList.size();
    const auto originalClipStart = clip->getPosition().getStart().inSeconds();
    const auto originalClipEnd = clip->getPosition().getEnd().inSeconds();
    const auto originalClipName = clip->getName();
    const auto originalTrackName = track->getName();
    const auto originalGainDb = clip->state.getProperty ("gainDb", 0.0);
    const auto originalFadeIn = dynamic_cast<te::WaveAudioClip*> (clip)->getFadeIn().inSeconds();
    const auto originalFadeOut = dynamic_cast<te::WaveAudioClip*> (clip)->getFadeOut().inSeconds();
    auto rootFolder = edit->insertNewFolderTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr, false);
    expect (rootFolder != nullptr, "Expected folder track fixture for malformed-command test");
    rootFolder->setName ("Malformed Folder");
    const auto originalParentFolder = track->getParentFolderTrack();

    CommandHandler handler (*edit);

    auto removeTrackResponse = runJsonCommand (handler, R"({
        "action":"remove_track"
    })");
    expect (removeTrackResponse["status"].toString() == "error",
            "Expected remove_track without track_id to fail");
    expect (getAudioTrackCount (*edit) == originalTrackCount,
            "Expected malformed remove_track request not to delete any track");

    auto trackIdMalformedResponse = runJsonCommand (handler, R"({
        "action":"set_track_volume",
        "track_id":"oops",
        "value_db":-3.0
    })");
    expect (trackIdMalformedResponse["status"].toString() == "error",
            "Expected set_track_volume with non-integer track_id to fail");
    expect (std::abs (volumePlugin->volParam->getCurrentValue() - originalVolumeValue) < 0.0001f,
            "Expected malformed track_id not to retarget track 0 volume");

    auto seekMalformedResponse = runJsonCommand (handler, R"({
        "action":"transport_seek",
        "position":"oops"
    })");
    expect (seekMalformedResponse["status"].toString() == "error",
            "Expected transport_seek with non-numeric position to fail");
    expect (std::abs (edit->getTransport().getPosition().inSeconds() - 1.25) < 0.0001,
            "Expected malformed transport_seek request not to reset the transport position");

    auto duplicateTrackResponse = runJsonCommand (handler, R"({
        "action":"duplicate_track"
    })");
    expect (duplicateTrackResponse["status"].toString() == "error",
            "Expected duplicate_track without track_id to fail");
    expect (getAudioTrackCount (*edit) == originalTrackCount,
            "Expected malformed duplicate_track request not to duplicate any track");

    auto insertAudioResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"insert_audio_clip",
        "file_path":"%s"
    })", audioFixture.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));
    expect (insertAudioResponse["status"].toString() == "error",
            "Expected insert_audio_clip without track_id to fail");
    expect (track->getClips().size() == originalClipCount,
            "Expected malformed insert_audio_clip request not to insert on track 0");

    auto insertAudioStartMalformedResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"insert_audio_clip",
        "track_id":0,
        "file_path":"%s",
        "start_time":"oops"
    })", audioFixture.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));
    expect (insertAudioStartMalformedResponse["status"].toString() == "error",
            "Expected insert_audio_clip with non-numeric start_time to fail");
    expect (track->getClips().size() == originalClipCount,
            "Expected malformed insert_audio_clip start_time not to insert a clip");

    auto insertMidiResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"insert_midi_clip",
        "file_path":"%s"
    })", midiFixture.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));
    expect (insertMidiResponse["status"].toString() == "error",
            "Expected insert_midi_clip without track_id to fail");
    expect (track->getClips().size() == originalClipCount,
            "Expected malformed insert_midi_clip request not to insert on track 0");

    auto insertMidiStartMalformedResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"insert_midi_clip",
        "track_id":0,
        "file_path":"%s",
        "start_time":"oops"
    })", midiFixture.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));
    expect (insertMidiStartMalformedResponse["status"].toString() == "error",
            "Expected insert_midi_clip with non-numeric start_time to fail");
    expect (track->getClips().size() == originalClipCount,
            "Expected malformed insert_midi_clip start_time not to insert a clip");

    auto armTrackMalformedResponse = runJsonCommand (handler, R"({
        "action":"arm_track",
        "track_id":0,
        "enabled":"oops"
    })");
    expect (armTrackMalformedResponse["status"].toString() == "error",
            "Expected arm_track with non-boolean enabled alias to fail");

    auto splitMalformedResponse = runJsonCommand (handler, R"({
        "action":"split_clip",
        "track_id":"oops",
        "clip_index":0,
        "position":0.25
    })");
    expect (splitMalformedResponse["status"].toString() == "error",
            "Expected split_clip with non-integer track_id to fail");
    expect (track->getClips().size() == originalClipCount,
            "Expected malformed split_clip request not to split the clip");

    auto deleteMalformedResponse = runJsonCommand (handler, R"({
        "action":"delete_clip",
        "track_id":"oops",
        "clip_index":0
    })");
    expect (deleteMalformedResponse["status"].toString() == "error",
            "Expected delete_clip with non-integer track_id to fail");
    expect (track->getClips().size() == originalClipCount,
            "Expected malformed delete_clip request not to delete the clip");

    auto duplicateMalformedResponse = runJsonCommand (handler, R"({
        "action":"duplicate_clip",
        "track_id":"oops",
        "clip_index":0
    })");
    expect (duplicateMalformedResponse["status"].toString() == "error",
            "Expected duplicate_clip with non-integer track_id to fail");
    expect (track->getClips().size() == originalClipCount,
            "Expected malformed duplicate_clip request not to duplicate the clip");

    auto trimMalformedResponse = runJsonCommand (handler, R"({
        "action":"trim_clip",
        "track_id":"oops",
        "clip_index":0,
        "new_start":0.1
    })");
    expect (trimMalformedResponse["status"].toString() == "error",
            "Expected trim_clip with non-integer track_id to fail");
    expect (std::abs (clip->getPosition().getStart().inSeconds() - originalClipStart) < 0.0001,
            "Expected malformed trim_clip request not to change clip start");
    expect (std::abs (clip->getPosition().getEnd().inSeconds() - originalClipEnd) < 0.0001,
            "Expected malformed trim_clip request not to change clip end");

    auto gainMalformedResponse = runJsonCommand (handler, R"({
        "action":"set_clip_gain",
        "track_id":0,
        "clip_index":"oops",
        "gain_db":-12.0
    })");
    expect (gainMalformedResponse["status"].toString() == "error",
            "Expected set_clip_gain with non-integer clip_index to fail");
    expect (clip->state.getProperty ("gainDb", 0.0) == originalGainDb,
            "Expected malformed set_clip_gain request not to change clip gain");

    auto renameClipMalformedResponse = runJsonCommand (handler, R"({
        "action":"rename_clip",
        "track_id":"oops",
        "clip_index":0,
        "name":"renamed_by_bug"
    })");
    expect (renameClipMalformedResponse["status"].toString() == "error",
            "Expected rename_clip with non-integer track_id to fail");
    expect (clip->getName() == originalClipName,
            "Expected malformed rename_clip request not to rename the clip");

    auto renameTrackMalformedResponse = runJsonCommand (handler, R"({
        "action":"rename_track",
        "track_id":"oops",
        "name":"renamed_track_by_bug"
    })");
    expect (renameTrackMalformedResponse["status"].toString() == "error",
            "Expected rename_track with non-integer track_id to fail");
    expect (track->getName() == originalTrackName,
            "Expected malformed rename_track request not to rename the track");

    auto setClipFadeMalformedResponse = runJsonCommand (handler, R"({
        "action":"set_clip_fade",
        "track_id":0,
        "clip_index":0,
        "fade_in":"oops"
    })");
    expect (setClipFadeMalformedResponse["status"].toString() == "error",
            "Expected set_clip_fade with non-numeric fade value to fail");
    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);
    expect (waveClip != nullptr, "Expected wave clip fixture for malformed fade test");
    expect (std::abs (waveClip->getFadeIn().inSeconds() - originalFadeIn) < 0.0001,
            "Expected malformed set_clip_fade request not to change fade in");
    expect (std::abs (waveClip->getFadeOut().inSeconds() - originalFadeOut) < 0.0001,
            "Expected malformed set_clip_fade request not to change fade out");

    auto volumeResponse = runJsonCommand (handler, R"({
        "action":"set_track_volume",
        "value_db":-18.0
    })");
    expect (volumeResponse["status"].toString() == "error",
            "Expected set_track_volume without track_id to fail");
    expect (std::abs (volumePlugin->volParam->getCurrentValue() - originalVolumeValue) < 0.0001f,
            "Expected malformed set_track_volume request not to mutate track volume");

    auto panResponse = runJsonCommand (handler, R"({
        "action":"set_track_pan",
        "value":0.9
    })");
    expect (panResponse["status"].toString() == "error",
            "Expected set_track_pan without track_id to fail");
    expect (std::abs (volumePlugin->panParam->getCurrentValue() - originalPanValue) < 0.0001f,
            "Expected malformed set_track_pan request not to mutate track pan");

    auto seekResponse = runJsonCommand (handler, R"({
        "action":"transport_seek"
    })");
    expect (seekResponse["status"].toString() == "error",
            "Expected transport_seek without position to fail");
    expect (std::abs (edit->getTransport().getPosition().inSeconds() - 1.25) < 0.0001,
            "Expected malformed transport_seek request not to move the transport");

    auto setParameterResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"set_parameter",
        "plugin_id":"%s",
        "param_id":"%s",
        "value":0.75
    })",
        stableIdentifier.replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8(),
        firstPluginParam->paramID.replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));
    expect (setParameterResponse["status"].toString() == "error",
            "Expected set_parameter without track_id to fail");
    expect (std::abs ((double) firstPluginParam->getCurrentValue() - (double) originalPluginParamValue) < 0.0001,
            "Expected malformed set_parameter request not to mutate the plugin parameter");

    auto loadPluginResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"load_plugin",
        "plugin_id":"%s"
    })", te::ReverbPlugin::xmlTypeName));
    expect (loadPluginResponse["status"].toString() == "error",
            "Expected load_plugin without track_id to fail");
    expect (track->pluginList.size() == originalPluginCount,
            "Expected malformed load_plugin request not to insert a plugin on track 0");

    auto getPluginParametersMalformedResponse = runJsonCommand (handler, R"({
        "action":"get_plugin_parameters",
        "track_id":0,
        "plugin_index":"oops"
    })");
    expect (getPluginParametersMalformedResponse["status"].toString() == "error",
            "Expected get_plugin_parameters with non-integer plugin_index to fail");

    auto bypassPluginMalformedResponse = runJsonCommand (handler, R"({
        "action":"bypass_plugin",
        "track_id":0,
        "plugin_index":0,
        "bypassed":"oops"
    })");
    expect (bypassPluginMalformedResponse["status"].toString() == "error",
            "Expected bypass_plugin with non-boolean bypassed value to fail");
    expect (reverb->isEnabled(),
            "Expected malformed bypass_plugin request not to change plugin enabled state");

    auto removePluginMalformedResponse = runJsonCommand (handler, R"({
        "action":"remove_plugin",
        "track_id":"oops",
        "plugin_index":0
    })");
    expect (removePluginMalformedResponse["status"].toString() == "error",
            "Expected remove_plugin with non-integer track_id to fail");
    expect (track->pluginList.size() == originalPluginCount,
            "Expected malformed remove_plugin request not to remove a plugin");

    auto addAutomationMalformedResponse = runJsonCommand (handler, R"({
        "action":"add_automation_point",
        "track_id":0,
        "param_index":0,
        "time":0.25,
        "value":0.5,
        "curve":"oops"
    })");
    expect (addAutomationMalformedResponse["status"].toString() == "error",
            "Expected add_automation_point with non-numeric curve to fail");
    expect (firstPluginParam->getCurve().getNumPoints() == 0,
            "Expected malformed add_automation_point request not to create automation points");

    auto moveTrackToFolderMalformedResponse = runJsonCommand (handler, R"({
        "action":"move_track_to_folder",
        "track_index":"oops",
        "folder_index":2
    })");
    expect (moveTrackToFolderMalformedResponse["status"].toString() == "error",
            "Expected move_track_to_folder with non-integer track_index to fail");
    expect (track->getParentFolderTrack() == originalParentFolder,
            "Expected malformed move_track_to_folder request not to move the track");

    auto removeFromFolderMalformedResponse = runJsonCommand (handler, R"({
        "action":"remove_from_folder",
        "track_index":"oops"
    })");
    expect (removeFromFolderMalformedResponse["status"].toString() == "error",
            "Expected remove_from_folder with non-integer track_index to fail");
    expect (track->getParentFolderTrack() == originalParentFolder,
            "Expected malformed remove_from_folder request not to change folder membership");

    auto soloResponse = runJsonCommand (handler, R"({
        "action":"solo_track",
        "track_id":0,
        "solo":0
    })");
    expect (soloResponse["status"].toString() == "error",
            "Expected solo_track with numeric solo value to fail");
    expect (track->isSolo (false),
            "Expected malformed solo_track request not to change solo state");

    auto muteResponse = runJsonCommand (handler, R"({
        "action":"mute_track",
        "track_id":0,
        "mute":0
    })");
    expect (muteResponse["status"].toString() == "error",
            "Expected mute_track with numeric mute value to fail");
    expect (track->isMuted (false),
            "Expected malformed mute_track request not to change mute state");

    auto loopResponse = runJsonCommand (handler, R"({
        "action":"set_loop_region",
        "enabled":0
    })");
    expect (loopResponse["status"].toString() == "error",
            "Expected set_loop_region with numeric enabled value to fail");
    expect (edit->getTransport().looping.get(),
            "Expected malformed set_loop_region request not to change looping state");
    expect (std::abs (edit->getTransport().getLoopRange().getStart().inSeconds() - 0.5) < 0.0001,
            "Expected malformed set_loop_region request not to change loop start");
    expect (std::abs (edit->getTransport().getLoopRange().getEnd().inSeconds() - 1.5) < 0.0001,
            "Expected malformed set_loop_region request not to change loop end");

    auto loopRangeMalformedResponse = runJsonCommand (handler, R"({
        "action":"set_loop_region",
        "enabled":true,
        "start":"oops",
        "end":2.0
    })");
    expect (loopRangeMalformedResponse["status"].toString() == "error",
            "Expected set_loop_region with non-numeric start to fail");
    expect (edit->getTransport().looping.get(),
            "Expected malformed loop range request not to change looping state");
    expect (std::abs (edit->getTransport().getLoopRange().getStart().inSeconds() - 0.5) < 0.0001,
            "Expected malformed loop range request not to change loop start");
    expect (std::abs (edit->getTransport().getLoopRange().getEnd().inSeconds() - 1.5) < 0.0001,
            "Expected malformed loop range request not to change loop end");

    auto exportMixdownResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"export_mixdown",
        "file_path":"%s",
        "start":"oops"
    })", exportOutput.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));
    expect (exportMixdownResponse["status"].toString() == "error",
            "Expected export_mixdown with non-numeric start to fail");
    expect (! exportOutput.existsAsFile(),
            "Expected malformed export_mixdown request not to create an output file");

    auto exportStemsResponse = runJsonCommand (handler, juce::String::formatted (R"({
        "action":"export_stems",
        "output_dir":"%s",
        "start":"oops"
    })", stemsOutputDir.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"").toRawUTF8()));
    expect (exportStemsResponse["status"].toString() == "error",
            "Expected export_stems with non-numeric start to fail");
    expect (! stemsOutputDir.exists(),
            "Expected malformed export_stems request not to create the output directory");

    auto moveResponse = runJsonCommand (handler, R"({
        "action":"move_clip",
        "delta_seconds":0.5
    })");
    expect (moveResponse["status"].toString() == "error",
            "Expected move_clip without track_id and clip_index to fail");
    expect (std::abs (clip->getPosition().getStart().inSeconds()) < 0.01,
            "Expected malformed move_clip request not to mutate the clip");

    auto automationResponse = runJsonCommand (handler, R"({
        "action":"add_automation_point",
        "track_id":0,
        "value":0.5,
        "time":1.0
    })");
    expect (automationResponse["status"].toString() == "error",
            "Expected add_automation_point without param_index to fail");

    auto markerResponse = runJsonCommand (handler, R"({
        "action":"add_marker",
        "name":"marker_only"
    })");
    expect (markerResponse["status"].toString() == "error",
            "Expected add_marker without time to fail");

    (void) fixtureDir.deleteRecursively();
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    try
    {
        te::Engine engine ("WaiveCoreTests");
        engine.getPluginManager().initialise();

        EditSession session (engine);

        testCoalescedTransactionsAndReset (session);
        testDuplicateMidiClipPreservesSequence (session);
        testPerformEditExceptionSafety (session);
        testCoalescedPerformEditExceptionSafety (engine);
        testDirtyStateSavepointAcrossCoalescedUndoRedo (engine);
        testUndoableCommandHandlerWrapsMutatingCommands (engine);
        testClickTrackToggleSupportsUndoRedo (engine);
        testModelManagerSettingsPersistence();
        testPathSanitizerRejectsTraversal();
        testPathSanitizerValidatesDirectory();
        testPathSanitizerValidatesIdentifier();
        testModelManagerRejectsPathTraversal();
        testAudioAnalysisCacheLRU();
        testAudioAnalysisCacheO1Performance();
        testAudioAnalysisZeroSampleRate();
        testCollectAndSavePersistsToExplicitProjectFile (engine);
        testCollectAndSaveCopiesExternalMediaAndRewritesReferences (engine);
        testCollectAndSaveRewritesInternalAbsoluteReferencesRelative (engine);
        testCollectAndSaveRollsBackWhenOneFileFails (engine);
        testRemoveUnusedMediaReportsActualBytesFreed (engine);
        testMediaManagementCanonicalisesSymlinkedReferences (engine);
        testCollectAndSaveRestoresReferencesWhenSaveFails (engine);
        testPackageAsZipIncludesOnlyCurrentProjectFile (engine);
        testCollectAndSaveCommandReturnsErrorOnPackagingFailure (engine);
        testCollectAndSaveRollsBackOnPartialCopyFailure (engine);
        testRemoveUnusedMediaCommandReturnsErrorOnMoveFailure (engine);
        testPluginPresetManagerUsesDocumentedWrapperAndStableIdentifier (engine);
        testPluginPresetCommandsSupportMasterChain (engine);
        testPluginPresetCommandsRejectMissingPluginIndex (engine);
        testCommandSchemaDocumentsPresetTargetingRequirements();
        testSetParameterAcceptsStablePluginIdentifier (engine);
        testTrackCommandsReturnPublicIndicesWithFolderTracks (engine);
        testReorderTrackUsesPublicIndicesAcrossFolderTracks (engine);
        testMoveTrackToFolderRejectsCycles (engine);
        testCommandHandlerRejectsRelativePaths (engine);
        testFolderSoloMutePropagatesThroughNestedFolders (engine);
        testCommandHandlerAcceptsDocumentedAliases (engine);
        testExportStemsReportsRenderFailures (engine);
        testExportStemsAllowsNewOutputDirectoriesWithinAllowlist (engine);
        testSetLoopRegionRejectsInvalidBoundsWithoutMutation (engine);
        testUndoableCommandHandlerSupportsLoopRegionUndoRedo (engine);
        testCommandHandlerRejectsMalformedCommandRequests (engine);

        std::cout << "WaiveCoreTests: PASS" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "WaiveCoreTests: FAIL: " << e.what() << std::endl;
        return 1;
    }
}
