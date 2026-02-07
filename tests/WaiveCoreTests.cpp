#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

#include "EditSession.h"
#include "ClipEditActions.h"
#include "ModelManager.h"
#include "PathSanitizer.h"
#include "AudioAnalysisCache.h"
#include "AudioAnalysis.h"

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

int getAudioTrackCount (te::Edit& edit)
{
    return te::getAudioTracks (edit).size();
}

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
    auto ok = session.performEdit ("Throwing Mutation", [&] (te::Edit&)
    {
        throw std::runtime_error ("intentional test exception");
    });

    expect (! ok, "Expected performEdit to return false when mutation throws");
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
        testModelManagerSettingsPersistence();
        testPathSanitizerRejectsTraversal();
        testPathSanitizerValidatesDirectory();
        testPathSanitizerValidatesIdentifier();
        testModelManagerRejectsPathTraversal();
        testAudioAnalysisCacheLRU();

        std::cout << "WaiveCoreTests: PASS" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "WaiveCoreTests: FAIL: " << e.what() << std::endl;
        return 1;
    }
}
