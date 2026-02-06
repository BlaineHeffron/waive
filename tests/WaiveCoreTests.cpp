#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

#include "EditSession.h"
#include "ClipEditActions.h"
#include "ModelManager.h"

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

        std::cout << "WaiveCoreTests: PASS" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "WaiveCoreTests: FAIL: " << e.what() << std::endl;
        return 1;
    }
}
