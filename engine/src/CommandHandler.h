#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;
namespace waive { class PluginPresetManager; }

//==============================================================================
/** Dispatches JSON commands to Tracktion Engine Edit operations. */
class CommandHandler
{
public:
    explicit CommandHandler (te::Edit& edit);
    ~CommandHandler();

    /** Process a JSON command string and return a JSON response string. */
    juce::String handleCommand (const juce::String& jsonString);

    /** Set allowed media directories for file path validation. */
    void setAllowedMediaDirectories (const juce::Array<juce::File>& directories);

    /** Get current allowed media directories. */
    const juce::Array<juce::File>& getAllowedMediaDirectories() const;

private:
    te::Edit& edit;
    juce::Array<juce::File> allowedMediaDirectories;
    std::unique_ptr<waive::PluginPresetManager> presetManager;

    // ── Individual command handlers ─────────────────────────────────────
    juce::var handlePing();
    juce::var handleGetEditState();
    juce::var handleGetTracks();
    juce::var handleAddTrack();
    juce::var handleSetTrackVolume (const juce::var& params);
    juce::var handleSetTrackPan (const juce::var& params);
    juce::var handleInsertAudioClip (const juce::var& params);
    juce::var handleRemoveTrack (const juce::var& params);
    juce::var handleInsertMidiClip (const juce::var& params);
    juce::var handleLoadPlugin (const juce::var& params);
    juce::var handleSetParameter (const juce::var& params);
    juce::var handleTransportPlay();
    juce::var handleTransportStop();
    juce::var handleTransportSeek (const juce::var& params);
    juce::var handleListPlugins();
    juce::var handleArmTrack (const juce::var& params);
    juce::var handleRecordFromMic();
    juce::var handleSplitClip (const juce::var& params);
    juce::var handleDeleteClip (const juce::var& params);
    juce::var handleMoveClip (const juce::var& params);
    juce::var handleDuplicateClip (const juce::var& params);
    juce::var handleTrimClip (const juce::var& params);
    juce::var handleSetClipGain (const juce::var& params);
    juce::var handleRenameClip (const juce::var& params);
    juce::var handleRenameTrack (const juce::var& params);
    juce::var handleSoloTrack (const juce::var& params);
    juce::var handleMuteTrack (const juce::var& params);
    juce::var handleDuplicateTrack (const juce::var& params);
    juce::var handleGetTransportState();
    juce::var handleSetTempo (const juce::var& params);
    juce::var handleSetLoopRegion (const juce::var& params);
    juce::var handleExportMixdown (const juce::var& params);
    juce::var handleExportStems (const juce::var& params);
    juce::var handleBounceTrack (const juce::var& params);
    juce::var handleRemovePlugin (const juce::var& params);
    juce::var handleBypassPlugin (const juce::var& params);
    juce::var handleGetPluginParameters (const juce::var& params);
    juce::var handleGetAutomationParams (const juce::var& params);
    juce::var handleGetAutomationPoints (const juce::var& params);
    juce::var handleAddAutomationPoint (const juce::var& params);
    juce::var handleRemoveAutomationPoint (const juce::var& params);
    juce::var handleClearAutomation (const juce::var& params);
    juce::var handleSetClipFade (const juce::var& params);
    juce::var handleSetTimeSignature (const juce::var& params);
    juce::var handleAddMarker (const juce::var& params);
    juce::var handleRemoveMarker (const juce::var& params);
    juce::var handleListMarkers();
    juce::var handleReorderTrack (const juce::var& params);
    juce::var handleSavePluginPreset (const juce::var& params);
    juce::var handleLoadPluginPreset (const juce::var& params);

    // ── Helpers ─────────────────────────────────────────────────────────
    te::AudioTrack* getTrackById (int trackIndex);
    te::Clip* getClipByIndex (int trackIndex, int clipIndex);
    juce::var makeError (const juce::String& message);
    juce::var makeOk();
};
