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

            if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
            {
                auto sourceFile = waveClip->getSourceFileReference().getFile();
                if (auto* audioTrack = dynamic_cast<te::AudioTrack*> (track))
                {
                    audioTrack->insertWaveClip (clip.getName() + " copy", sourceFile,
                                                { { endTime, endTime + pos.getLength() },
                                                  te::TimeDuration() },
                                                false);
                }
            }
            else if (dynamic_cast<te::MidiClip*> (&clip) != nullptr)
            {
                if (auto* audioTrack = dynamic_cast<te::AudioTrack*> (track))
                {
                    audioTrack->insertMIDIClip (
                        clip.getName() + " copy",
                        te::TimeRange (endTime, endTime + pos.getLength()),
                        nullptr);
                }
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
