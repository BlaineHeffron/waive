#include "ClipEditActions.h"
#include "EditSession.h"

namespace waive
{

void moveClip (EditSession& session, te::Clip& clip, double newStartSeconds)
{
    session.performEdit ("Move Clip", true, [&] (te::Edit&)
    {
        clip.setStart (te::TimePosition::fromSeconds (newStartSeconds), true, false);
    });
}

void moveClipToTrack (EditSession& session, te::Clip& clip, te::AudioTrack& dest, double newStart)
{
    session.performEdit ("Move Clip to Track", [&] (te::Edit&)
    {
        clip.setStart (te::TimePosition::fromSeconds (newStart), true, false);
        clip.moveTo (dest);
    });
}

void duplicateClip (EditSession& session, te::Clip& clip)
{
    session.performEdit ("Duplicate Clip", [&] (te::Edit&)
    {
        if (auto* track = dynamic_cast<te::ClipTrack*> (clip.getTrack()))
        {
            auto pos = clip.getPosition();
            auto endTime = pos.getEnd();

            auto duplicatedState = clip.state.createCopy();
            clip.edit.createNewItemID().writeID (duplicatedState, nullptr);
            te::assignNewIDsToAutomationCurveModifiers (clip.edit, duplicatedState);

            if (auto* newClip = track->insertClipWithState (duplicatedState))
            {
                newClip->setName (clip.getName() + " copy");
                newClip->setStart (endTime, true, false);
            }
        }
    });
}

void trimClipLeft (EditSession& session, te::Clip& clip, double newStart)
{
    session.performEdit ("Trim Clip Left", true, [&] (te::Edit&)
    {
        clip.setStart (te::TimePosition::fromSeconds (newStart), false, true);
    });
}

void trimClipRight (EditSession& session, te::Clip& clip, double newEnd)
{
    session.performEdit ("Trim Clip Right", true, [&] (te::Edit&)
    {
        clip.setEnd (te::TimePosition::fromSeconds (newEnd), true);
    });
}

void splitClipAtPosition (EditSession& session, te::Clip& clip, double splitTime)
{
    auto pos = clip.getPosition();
    if (splitTime <= pos.getStart().inSeconds() || splitTime >= pos.getEnd().inSeconds())
        return;

    session.performEdit ("Split Clip", [&] (te::Edit&)
    {
        if (auto* clipTrack = dynamic_cast<te::ClipTrack*> (clip.getTrack()))
            clipTrack->splitClip (clip, te::TimePosition::fromSeconds (splitTime));
    });
}

void deleteClips (EditSession& session, const juce::Array<te::Clip*>& clips)
{
    if (clips.isEmpty())
        return;

    session.performEdit ("Delete Clips", [&] (te::Edit&)
    {
        for (auto* clip : clips)
            clip->removeFromParent();
    });
}

} // namespace waive
