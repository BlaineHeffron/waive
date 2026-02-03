#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

//==============================================================================
/** Dispatches JSON commands to Tracktion Engine Edit operations. */
class CommandHandler
{
public:
    explicit CommandHandler (te::Edit& edit);

    /** Process a JSON command string and return a JSON response string. */
    juce::String handleCommand (const juce::String& jsonString);

private:
    te::Edit& edit;

    // ── Individual command handlers ─────────────────────────────────────
    juce::var handlePing();
    juce::var handleGetEditState();
    juce::var handleGetTracks();
    juce::var handleAddTrack();
    juce::var handleSetTrackVolume (const juce::var& params);
    juce::var handleSetTrackPan (const juce::var& params);
    juce::var handleInsertAudioClip (const juce::var& params);
    juce::var handleTransportPlay();
    juce::var handleTransportStop();
    juce::var handleTransportSeek (const juce::var& params);

    // ── Helpers ─────────────────────────────────────────────────────────
    te::AudioTrack* getTrackById (int trackIndex);
    juce::var makeError (const juce::String& message);
    juce::var makeOk();
};
