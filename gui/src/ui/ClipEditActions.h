#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class EditSession;

namespace waive
{

void moveClip (EditSession& session, te::Clip& clip, double newStartSeconds);
void moveClipToTrack (EditSession& session, te::Clip& clip, te::AudioTrack& dest, double newStart);
void duplicateClip (EditSession& session, te::Clip& clip);
void trimClipLeft (EditSession& session, te::Clip& clip, double newStart);
void trimClipRight (EditSession& session, te::Clip& clip, double newEnd);
void splitClipAtPosition (EditSession& session, te::Clip& clip, double splitTime);
void deleteClips (EditSession& session, const juce::Array<te::Clip*>& clips);

} // namespace waive
