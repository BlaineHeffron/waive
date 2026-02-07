#include "CommandHandler.h"

CommandHandler::CommandHandler (te::Edit& e) : edit (e)
{
    // Initialize with safe defaults: user home + current working directory
    allowedMediaDirectories.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    allowedMediaDirectories.add (juce::File::getCurrentWorkingDirectory());
}

void CommandHandler::setAllowedMediaDirectories (const juce::Array<juce::File>& directories)
{
    allowedMediaDirectories = directories;
}

const juce::Array<juce::File>& CommandHandler::getAllowedMediaDirectories() const
{
    return allowedMediaDirectories;
}

//==============================================================================
juce::String CommandHandler::handleCommand (const juce::String& jsonString)
{
    auto parsed = juce::JSON::parse (jsonString);

    if (! parsed.isObject())
        return juce::JSON::toString (makeError ("Invalid JSON"));

    auto action = parsed["action"].toString();

    juce::var result;

    if      (action == "ping")              result = handlePing();
    else if (action == "get_edit_state")     result = handleGetEditState();
    else if (action == "get_tracks")        result = handleGetTracks();
    else if (action == "add_track")         result = handleAddTrack();
    else if (action == "remove_track")      result = handleRemoveTrack (parsed);
    else if (action == "set_track_volume")  result = handleSetTrackVolume (parsed);
    else if (action == "set_track_pan")     result = handleSetTrackPan (parsed);
    else if (action == "insert_audio_clip") result = handleInsertAudioClip (parsed);
    else if (action == "insert_midi_clip")  result = handleInsertMidiClip (parsed);
    else if (action == "load_plugin")       result = handleLoadPlugin (parsed);
    else if (action == "set_parameter")     result = handleSetParameter (parsed);
    else if (action == "transport_play")    result = handleTransportPlay();
    else if (action == "transport_stop")    result = handleTransportStop();
    else if (action == "transport_seek")    result = handleTransportSeek (parsed);
    else if (action == "list_plugins")     result = handleListPlugins();
    else if (action == "arm_track")        result = handleArmTrack (parsed);
    else if (action == "record_from_mic")  result = handleRecordFromMic();
    else if (action == "split_clip")       result = handleSplitClip (parsed);
    else if (action == "delete_clip")      result = handleDeleteClip (parsed);
    else if (action == "move_clip")        result = handleMoveClip (parsed);
    else if (action == "duplicate_clip")   result = handleDuplicateClip (parsed);
    else if (action == "trim_clip")        result = handleTrimClip (parsed);
    else if (action == "set_clip_gain")    result = handleSetClipGain (parsed);
    else if (action == "rename_clip")      result = handleRenameClip (parsed);
    else if (action == "rename_track")        result = handleRenameTrack (parsed);
    else if (action == "solo_track")          result = handleSoloTrack (parsed);
    else if (action == "mute_track")          result = handleMuteTrack (parsed);
    else if (action == "duplicate_track")     result = handleDuplicateTrack (parsed);
    else if (action == "get_transport_state") result = handleGetTransportState();
    else if (action == "set_tempo")           result = handleSetTempo (parsed);
    else if (action == "set_loop_region")     result = handleSetLoopRegion (parsed);
    else if (action == "export_mixdown")      result = handleExportMixdown (parsed);
    else if (action == "export_stems")        result = handleExportStems (parsed);
    else if (action == "bounce_track")        result = handleBounceTrack (parsed);
    else if (action == "remove_plugin")       result = handleRemovePlugin (parsed);
    else if (action == "bypass_plugin")       result = handleBypassPlugin (parsed);
    else if (action == "get_plugin_parameters") result = handleGetPluginParameters (parsed);
    else if (action == "get_automation_params")    result = handleGetAutomationParams (parsed);
    else if (action == "get_automation_points")    result = handleGetAutomationPoints (parsed);
    else if (action == "add_automation_point")     result = handleAddAutomationPoint (parsed);
    else if (action == "remove_automation_point")  result = handleRemoveAutomationPoint (parsed);
    else if (action == "clear_automation")         result = handleClearAutomation (parsed);
    else if (action == "set_clip_fade")            result = handleSetClipFade (parsed);
    else                                    result = makeError ("Unknown action: " + action);

    return juce::JSON::toString (result);
}

//==============================================================================
// Command Handlers
//==============================================================================

juce::var CommandHandler::handlePing()
{
    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("message", "pong");
    return result;
}

juce::var CommandHandler::handleGetEditState()
{
    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        auto xml = edit.state.toXmlString();
        obj->setProperty ("edit_xml", xml);
    }
    return result;
}

juce::var CommandHandler::handleGetTracks()
{
    auto result = makeOk();
    juce::Array<juce::var> trackList;

    int trackId = 0;
    for (auto* track : te::getAudioTracks (edit))
    {
        auto* trackObj = new juce::DynamicObject();
        trackObj->setProperty ("name", track->getName());
        trackObj->setProperty ("track_id", trackId);
        trackObj->setProperty ("index", track->getIndexInEditTrackList());
        trackObj->setProperty ("solo", track->isSolo (false));
        trackObj->setProperty ("mute", track->isMuted (false));

        // Clip info
        juce::Array<juce::var> clipList;
        int clipIdx = 0;
        for (auto* clip : track->getClips())
        {
            auto* clipObj = new juce::DynamicObject();
            auto pos = clip->getPosition();
            clipObj->setProperty ("clip_index", clipIdx);
            clipObj->setProperty ("name", clip->getName());
            clipObj->setProperty ("start", pos.getStart().inSeconds());
            clipObj->setProperty ("end",   pos.getEnd().inSeconds());
            clipObj->setProperty ("length", (pos.getEnd() - pos.getStart()).inSeconds());

            // Clip gain (wave clips only)
            if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip))
            {
                auto gainProp = waveClip->state["gainDb"];
                clipObj->setProperty ("gain_db", gainProp.isVoid() ? 0.0 : (double) gainProp);
            }

            clipList.add (juce::var (clipObj));
            ++clipIdx;
        }
        trackObj->setProperty ("clips", clipList);

        trackList.add (juce::var (trackObj));
        ++trackId;
    }

    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("tracks", trackList);

    return result;
}

juce::var CommandHandler::handleAddTrack()
{
    auto trackCount = te::getAudioTracks (edit).size();
    edit.ensureNumberOfAudioTracks (trackCount + 1);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("track_index", trackCount);
    return result;
}

juce::var CommandHandler::handleSetTrackVolume (const juce::var& params)
{
    int trackId = params["track_id"];
    float valueDb = static_cast<float> (params["value_db"]);

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    auto volPlugins = track->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    if (volPlugins.isEmpty())
        return makeError ("No VolumeAndPanPlugin on track " + juce::String (trackId));

    auto* volPlugin = volPlugins.getFirst();
    volPlugin->volParam->setParameter (te::decibelsToVolumeFaderPosition (valueDb),
                                       juce::sendNotification);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("volume_db", (double) valueDb);
    }
    return result;
}

juce::var CommandHandler::handleSetTrackPan (const juce::var& params)
{
    int trackId = params["track_id"];
    float panValue = static_cast<float> (params["value"]);

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    auto volPlugins = track->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    if (volPlugins.isEmpty())
        return makeError ("No VolumeAndPanPlugin on track " + juce::String (trackId));

    volPlugins.getFirst()->panParam->setParameter (
        (panValue + 1.0f) / 2.0f,  // map [-1, 1] to [0, 1]
        juce::sendNotification);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("pan", (double) panValue);
    }
    return result;
}

juce::var CommandHandler::handleInsertAudioClip (const juce::var& params)
{
    int trackId         = params["track_id"];
    double startTime    = params["start_time"];
    auto filePath       = params["file_path"].toString();

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    juce::File file (filePath);
    if (! file.existsAsFile())
        return makeError ("File not found: " + filePath);

    // Validate file path is within allowed directories
    auto canonicalFile = file.getLinkedTarget();
    bool isAllowed = false;
    for (const auto& allowedDir : allowedMediaDirectories)
    {
        if (canonicalFile.isAChildOf (allowedDir) || canonicalFile == allowedDir)
        {
            isAllowed = true;
            break;
        }
    }

    if (! isAllowed)
        return makeError ("File path is outside allowed directories: " + filePath);

    te::AudioFile audioFile (edit.engine, file);
    auto duration = audioFile.getLength();

    auto clip = track->insertWaveClip (
        file.getFileNameWithoutExtension(),
        file,
        { { te::TimePosition::fromSeconds (startTime),
            te::TimePosition::fromSeconds (startTime + duration) },
          te::TimeDuration() },
        false);

    if (clip == nullptr)
        return makeError ("Failed to insert clip");

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("start_time", startTime);
        obj->setProperty ("duration", duration);
        obj->setProperty ("clip_name", clip->getName());
    }
    return result;
}

juce::var CommandHandler::handleRemoveTrack (const juce::var& params)
{
    int trackId = params["track_id"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    edit.deleteTrack (track);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("track_id", trackId);
    return result;
}

juce::var CommandHandler::handleInsertMidiClip (const juce::var& params)
{
    int trackId         = params["track_id"];
    auto filePath       = params["file_path"].toString();
    double startTime    = params.hasProperty ("start_time") ? (double) params["start_time"] : 0.0;

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    juce::File file (filePath);
    if (! file.existsAsFile())
        return makeError ("File not found: " + filePath);

    // Validate file path is within allowed directories
    auto canonicalFile = file.getLinkedTarget();
    bool isAllowed = false;
    for (const auto& allowedDir : allowedMediaDirectories)
    {
        if (canonicalFile.isAChildOf (allowedDir) || canonicalFile == allowedDir)
        {
            isAllowed = true;
            break;
        }
    }

    if (! isAllowed)
        return makeError ("File path is outside allowed directories: " + filePath);

    // Load MIDI file
    juce::MidiFile midiFile;
    juce::FileInputStream fis (file);
    if (! midiFile.readFrom (fis))
        return makeError ("Failed to read MIDI file: " + filePath);

    // Merge all MIDI tracks into a single sequence
    juce::MidiMessageSequence mergedSequence;
    for (int t = 0; t < midiFile.getNumTracks(); ++t)
        mergedSequence.addSequence (*midiFile.getTrack (t), 0.0);

    // Calculate duration from the last event time
    double duration = 0.0;
    if (mergedSequence.getNumEvents() > 0)
        duration = mergedSequence.getEndTime();

    // Create MIDI clip on the track
    auto clipName = file.getFileNameWithoutExtension();
    auto timeRange = te::TimeRange (
        te::TimePosition::fromSeconds (startTime),
        te::TimePosition::fromSeconds (startTime + duration));

    auto clip = track->insertMIDIClip (clipName, timeRange, nullptr);
    if (clip == nullptr)
        return makeError ("Failed to create MIDI clip");

    // Import MIDI data into the clip's sequence
    auto& clipSequence = clip->getSequence();
    clipSequence.importMidiSequence (mergedSequence, nullptr, te::TimePosition(), nullptr);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("start_time", startTime);
        obj->setProperty ("duration", duration);
        obj->setProperty ("clip_name", clip->getName());
    }
    return result;
}

juce::var CommandHandler::handleLoadPlugin (const juce::var& params)
{
    int trackId      = params["track_id"];
    auto pluginId    = params["plugin_id"].toString();

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Scan for the plugin using the engine's plugin manager
    auto& pm = edit.engine.getPluginManager();
    auto knownTypes = pm.knownPluginList.getTypes();

    juce::PluginDescription matchedDesc;
    bool found = false;
    for (auto& desc : knownTypes)
    {
        if (desc.name == pluginId ||
            desc.fileOrIdentifier == pluginId ||
            desc.createIdentifierString() == pluginId)
        {
            matchedDesc = desc;
            found = true;
            break;
        }
    }

    if (! found)
        return makeError ("Plugin not found: " + pluginId);

    // Create and insert the plugin
    auto plugin = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, matchedDesc);
    if (plugin == nullptr)
        return makeError ("Failed to create plugin: " + pluginId);

    // Insert before default track plugins (Volume/Pan + Meter) so the effect is audible and metered.
    track->pluginList.insertPlugin (plugin, 0, nullptr);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("plugin_id", pluginId);
        obj->setProperty ("plugin_name", plugin->getName());
    }
    return result;
}

juce::var CommandHandler::handleSetParameter (const juce::var& params)
{
    int trackId      = params["track_id"];
    auto pluginId    = params["plugin_id"].toString();
    auto paramId     = params["param_id"].toString();
    float value      = static_cast<float> (params["value"]);

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Find the plugin on the track
    te::Plugin* foundPlugin = nullptr;
    for (auto* p : track->pluginList)
    {
        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p))
        {
            if (ext->getName() == pluginId)
            {
                foundPlugin = ext;
                break;
            }
        }
    }

    if (foundPlugin == nullptr)
        return makeError ("Plugin not found on track: " + pluginId);

    // Find the parameter on the plugin
    auto param = foundPlugin->getAutomatableParameterByID (paramId);
    if (param == nullptr)
        return makeError ("Parameter not found: " + paramId);

    param->setParameter (value, juce::sendNotification);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("plugin_id", pluginId);
        obj->setProperty ("param_id", paramId);
        obj->setProperty ("value", (double) value);
    }
    return result;
}

juce::var CommandHandler::handleTransportPlay()
{
    edit.getTransport().play (false);
    return makeOk();
}

juce::var CommandHandler::handleTransportStop()
{
    edit.getTransport().stop (false, false);
    return makeOk();
}

juce::var CommandHandler::handleTransportSeek (const juce::var& params)
{
    double position = params["position"];
    edit.getTransport().setPosition (te::TimePosition::fromSeconds (position));

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("position", position);
    return result;
}

juce::var CommandHandler::handleListPlugins()
{
    auto& pm = edit.engine.getPluginManager();
    auto knownTypes = pm.knownPluginList.getTypes();

    juce::Array<juce::var> pluginList;
    for (auto& desc : knownTypes)
    {
        auto* pluginObj = new juce::DynamicObject();
        pluginObj->setProperty ("name", desc.name);
        pluginObj->setProperty ("identifier", desc.fileOrIdentifier);
        pluginObj->setProperty ("category", desc.category);
        pluginObj->setProperty ("manufacturer", desc.manufacturerName);
        pluginObj->setProperty ("format", desc.pluginFormatName);
        pluginObj->setProperty ("uid", juce::String (desc.uniqueId));
        pluginList.add (juce::var (pluginObj));
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("plugins", pluginList);
        obj->setProperty ("count", (int) knownTypes.size());
    }
    return result;
}

juce::var CommandHandler::handleArmTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("armed"))
        return makeError ("Missing required parameters: track_id, armed");

    int trackId = params["track_id"];
    bool armed = params["armed"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // If input_device specified, assign it first
    if (params.hasProperty ("input_device"))
    {
        auto inputDeviceName = params["input_device"].toString();
        auto& dm = edit.engine.getDeviceManager();
        auto waveInputs = dm.getWaveInputDevices();

        te::WaveInputDevice* matchedDevice = nullptr;
        for (auto* device : waveInputs)
        {
            if (device->getName() == inputDeviceName)
            {
                matchedDevice = device;
                break;
            }
        }

        if (matchedDevice == nullptr)
            return makeError ("Input device not found: " + inputDeviceName);

        edit.getTransport().ensureContextAllocated();
        (void) edit.getEditInputDevices().getInstanceStateForInputDevice (*matchedDevice);

        if (auto* epc = edit.getCurrentPlaybackContext())
        {
            if (auto* idi = epc->getInputFor (matchedDevice))
            {
                [[maybe_unused]] auto res = idi->setTarget (track->itemID, false, nullptr);
            }
        }
    }

    // Toggle recording enabled state
    auto& eid = edit.getEditInputDevices();
    for (auto* idi : eid.getDevicesForTargetTrack (*track))
        idi->setRecordingEnabled (track->itemID, armed);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("armed", armed);
    }
    return result;
}

juce::var CommandHandler::handleRecordFromMic()
{
    // Find first empty track or create new one
    te::AudioTrack* targetTrack = nullptr;
    for (auto* t : te::getAudioTracks (edit))
    {
        if (t->getClips().isEmpty())
        {
            targetTrack = t;
            break;
        }
    }

    if (targetTrack == nullptr)
    {
        auto trackCount = te::getAudioTracks (edit).size();
        edit.ensureNumberOfAudioTracks (trackCount + 1);
        targetTrack = te::getAudioTracks (edit).getLast();
    }

    if (targetTrack == nullptr)
        return makeError ("Failed to create target track");

    // Get first wave input device
    auto& dm = edit.engine.getDeviceManager();
    auto waveInputs = dm.getWaveInputDevices();
    if (waveInputs.empty())
        return makeError ("No wave input devices available");

    auto* waveIn = waveInputs[0];

    // Assign and arm the input device
    edit.getTransport().ensureContextAllocated();
    (void) edit.getEditInputDevices().getInstanceStateForInputDevice (*waveIn);

    if (auto* epc = edit.getCurrentPlaybackContext())
    {
        if (auto* idi = epc->getInputFor (waveIn))
        {
            if (!idi->setTarget (targetTrack->itemID, false, nullptr))
                return makeError ("Failed to assign input device to track");
            idi->setRecordingEnabled (targetTrack->itemID, true);
        }
        else
        {
            return makeError ("Failed to get input device instance");
        }
    }
    else
    {
        return makeError ("Failed to get playback context");
    }

    // Start recording
    edit.getTransport().record (false, true);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_index", targetTrack->getIndexInEditTrackList());
        obj->setProperty ("input_device", waveIn->getName());
    }
    return result;
}

juce::var CommandHandler::handleSplitClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index") || ! params.hasProperty ("position"))
        return makeError ("Missing required parameters: track_id, clip_index, position");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];
    double position = params["position"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    auto pos = clip->getPosition();
    if (position <= pos.getStart().inSeconds() || position >= pos.getEnd().inSeconds())
        return makeError ("Split position must be within clip bounds ("
                          + juce::String (pos.getStart().inSeconds()) + " - "
                          + juce::String (pos.getEnd().inSeconds()) + ")");

    auto* clipTrack = dynamic_cast<te::ClipTrack*> (clip->getTrack());
    if (clipTrack == nullptr)
        return makeError ("Clip is not on a clip track");

    clipTrack->splitClip (*clip, te::TimePosition::fromSeconds (position));

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("split_position", position);
    }
    return result;
}

juce::var CommandHandler::handleDeleteClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index"))
        return makeError ("Missing required parameters: track_id, clip_index");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    auto clipName = clip->getName();
    clip->removeFromParent();

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("deleted_clip", clipName);
    }
    return result;
}

juce::var CommandHandler::handleMoveClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index") || ! params.hasProperty ("new_start"))
        return makeError ("Missing required parameters: track_id, clip_index, new_start");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];
    double newStart = params["new_start"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    if (newStart < 0.0)
        return makeError ("new_start must be >= 0");

    clip->setStart (te::TimePosition::fromSeconds (newStart), true, false);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("clip_index", clipIdx);
        obj->setProperty ("new_start", newStart);
    }
    return result;
}

juce::var CommandHandler::handleDuplicateClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index"))
        return makeError ("Missing required parameters: track_id, clip_index");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    auto* clipTrack = dynamic_cast<te::ClipTrack*> (clip->getTrack());
    if (clipTrack == nullptr)
        return makeError ("Clip is not on a clip track");

    auto pos = clip->getPosition();
    auto endTime = pos.getEnd();

    auto duplicatedState = clip->state.createCopy();
    edit.createNewItemID().writeID (duplicatedState, nullptr);
    te::assignNewIDsToAutomationCurveModifiers (edit, duplicatedState);

    juce::String newClipName;
    if (auto* newClip = clipTrack->insertClipWithState (duplicatedState))
    {
        newClip->setName (clip->getName() + " copy");
        newClip->setStart (endTime, true, false);
        newClipName = newClip->getName();
    }
    else
    {
        return makeError ("Failed to duplicate clip");
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("original_clip_index", clipIdx);
        obj->setProperty ("new_clip_name", newClipName);
    }
    return result;
}

juce::var CommandHandler::handleTrimClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index"))
        return makeError ("Missing required parameters: track_id, clip_index");

    bool hasNewStart = params.hasProperty ("new_start");
    bool hasNewEnd = params.hasProperty ("new_end");
    if (! hasNewStart && ! hasNewEnd)
        return makeError ("At least one of new_start or new_end must be provided");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    if (hasNewStart)
    {
        double newStart = params["new_start"];
        clip->setStart (te::TimePosition::fromSeconds (newStart), false, true);
    }

    if (hasNewEnd)
    {
        double newEnd = params["new_end"];
        clip->setEnd (te::TimePosition::fromSeconds (newEnd), true);
    }

    auto pos = clip->getPosition();
    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("clip_index", clipIdx);
        obj->setProperty ("start", pos.getStart().inSeconds());
        obj->setProperty ("end", pos.getEnd().inSeconds());
    }
    return result;
}

juce::var CommandHandler::handleSetClipGain (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index") || ! params.hasProperty ("gain_db"))
        return makeError ("Missing required parameters: track_id, clip_index, gain_db");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];
    double gainDb = params["gain_db"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);
    if (waveClip == nullptr)
        return makeError ("Clip is not an audio clip (gain only applies to wave clips)");

    waveClip->state.setProperty ("gainDb", gainDb, &edit.getUndoManager());

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("clip_index", clipIdx);
        obj->setProperty ("gain_db", gainDb);
    }
    return result;
}

juce::var CommandHandler::handleRenameClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index") || ! params.hasProperty ("name"))
        return makeError ("Missing required parameters: track_id, clip_index, name");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];
    auto newName = params["name"].toString();

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    clip->setName (newName);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("clip_index", clipIdx);
        obj->setProperty ("name", newName);
    }
    return result;
}

juce::var CommandHandler::handleRenameTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("name"))
        return makeError ("Missing required parameters: track_id, name");

    int trackId = params["track_id"];
    auto newName = params["name"].toString();

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    track->setName (newName);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("name", newName);
    }
    return result;
}

juce::var CommandHandler::handleSoloTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("solo"))
        return makeError ("Missing required parameters: track_id, solo");

    int trackId = params["track_id"];
    bool solo = params["solo"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    track->setSolo (solo);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("solo", solo);
    }
    return result;
}

juce::var CommandHandler::handleMuteTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("mute"))
        return makeError ("Missing required parameters: track_id, mute");

    int trackId = params["track_id"];
    bool mute = params["mute"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    track->setMute (mute);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("mute", mute);
    }
    return result;
}

juce::var CommandHandler::handleDuplicateTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id"))
        return makeError ("Missing required parameter: track_id");

    int trackId = params["track_id"];

    auto* sourceTrack = getTrackById (trackId);
    if (sourceTrack == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Create a new track
    auto trackCount = te::getAudioTracks (edit).size();
    edit.ensureNumberOfAudioTracks (trackCount + 1);
    auto* newTrack = te::getAudioTracks (edit).getLast();
    if (newTrack == nullptr)
        return makeError ("Failed to create new track");

    newTrack->setName (sourceTrack->getName() + " copy");

    // Copy volume and pan
    auto srcVolPlugins = sourceTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    auto dstVolPlugins = newTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    if (! srcVolPlugins.isEmpty() && ! dstVolPlugins.isEmpty())
    {
        dstVolPlugins.getFirst()->volParam->setParameter (
            srcVolPlugins.getFirst()->volParam->getCurrentValue(), juce::dontSendNotification);
        dstVolPlugins.getFirst()->panParam->setParameter (
            srcVolPlugins.getFirst()->panParam->getCurrentValue(), juce::dontSendNotification);
    }

    // Copy all clips from source to new track
    for (auto* clip : sourceTrack->getClips())
    {
        auto clipState = clip->state.createCopy();
        edit.createNewItemID().writeID (clipState, nullptr);
        te::assignNewIDsToAutomationCurveModifiers (edit, clipState);
        newTrack->insertClipWithState (clipState);
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("source_track_id", trackId);
        obj->setProperty ("new_track_index", (int) (trackCount));
        obj->setProperty ("new_track_name", newTrack->getName());
    }
    return result;
}

juce::var CommandHandler::handleGetTransportState()
{
    auto& transport = edit.getTransport();

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("position", transport.getPosition().inSeconds());
        obj->setProperty ("is_playing", transport.isPlaying());
        obj->setProperty ("is_recording", transport.isRecording());

        // Tempo
        auto& tempoSeq = edit.tempoSequence;
        if (tempoSeq.getNumTempos() > 0)
            obj->setProperty ("tempo_bpm", tempoSeq.getTempo (0)->getBpm());
        else
            obj->setProperty ("tempo_bpm", 120.0);

        // Time signature
        if (tempoSeq.getNumTimeSigs() > 0)
        {
            auto* ts = tempoSeq.getTimeSig (0);
            obj->setProperty ("time_sig_numerator", ts->numerator.get());
            obj->setProperty ("time_sig_denominator", ts->denominator.get());
        }
        else
        {
            obj->setProperty ("time_sig_numerator", 4);
            obj->setProperty ("time_sig_denominator", 4);
        }

        // Loop region
        auto loopRange = transport.getLoopRange();
        obj->setProperty ("loop_enabled", transport.looping.get());
        obj->setProperty ("loop_start", loopRange.getStart().inSeconds());
        obj->setProperty ("loop_end", loopRange.getEnd().inSeconds());

        // Edit length
        obj->setProperty ("edit_length", edit.getLength().inSeconds());

        // Track count
        obj->setProperty ("track_count", (int) te::getAudioTracks (edit).size());
    }
    return result;
}

juce::var CommandHandler::handleSetTempo (const juce::var& params)
{
    if (! params.hasProperty ("bpm"))
        return makeError ("Missing required parameter: bpm");

    double bpm = params["bpm"];
    if (bpm < 20.0 || bpm > 999.0)
        return makeError ("BPM must be between 20 and 999");

    auto& tempoSeq = edit.tempoSequence;
    if (tempoSeq.getNumTempos() > 0)
        tempoSeq.getTempo (0)->setBpm (bpm);
    else
        return makeError ("No tempo settings in edit");

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("bpm", bpm);
    return result;
}

juce::var CommandHandler::handleSetLoopRegion (const juce::var& params)
{
    if (! params.hasProperty ("enabled"))
        return makeError ("Missing required parameter: enabled");

    bool enabled = params["enabled"];
    auto& transport = edit.getTransport();

    transport.looping.setValue (enabled, nullptr);

    if (enabled && params.hasProperty ("start") && params.hasProperty ("end"))
    {
        double startSec = params["start"];
        double endSec = params["end"];
        if (startSec >= endSec)
            return makeError ("Loop start must be less than end");

        transport.setLoopRange ({ te::TimePosition::fromSeconds (startSec),
                                  te::TimePosition::fromSeconds (endSec) });
    }

    auto loopRange = transport.getLoopRange();
    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("enabled", enabled);
        obj->setProperty ("loop_start", loopRange.getStart().inSeconds());
        obj->setProperty ("loop_end", loopRange.getEnd().inSeconds());
    }
    return result;
}

juce::var CommandHandler::handleExportMixdown (const juce::var& params)
{
    if (! params.hasProperty ("file_path"))
        return makeError ("Missing required parameter: file_path");

    auto filePath = params["file_path"].toString();
    juce::File outputFile (filePath);

    // Validate output path
    auto canonicalFile = outputFile.getLinkedTarget();
    bool isAllowed = false;
    for (const auto& allowedDir : allowedMediaDirectories)
    {
        if (canonicalFile.isAChildOf (allowedDir) || canonicalFile.getParentDirectory() == allowedDir)
        {
            isAllowed = true;
            break;
        }
    }
    if (! isAllowed)
        return makeError ("Output path is outside allowed directories: " + filePath);

    // Ensure parent directory exists
    outputFile.getParentDirectory().createDirectory();

    // Determine render range
    double startSec = params.hasProperty ("start") ? (double) params["start"] : 0.0;
    double endSec = params.hasProperty ("end") ? (double) params["end"] : edit.getLength().inSeconds();

    if (endSec <= startSec)
        return makeError ("End time must be greater than start time");

    // Build track mask for all audio tracks
    juce::BigInteger tracksMask;
    auto audioTracks = te::getAudioTracks (edit);
    for (int i = 0; i < audioTracks.size(); ++i)
        tracksMask.setBit (audioTracks[i]->getIndexInEditTrackList());

    // Render to file
    auto renderResult = te::Renderer::renderToFile (
        "Export Mixdown",
        outputFile,
        edit,
        te::TimeRange (te::TimePosition::fromSeconds (startSec), te::TimePosition::fromSeconds (endSec)),
        tracksMask,
        true,     // usePlugins
        true,     // useACID
        {},       // allowedClips (empty = all)
        true);    // useThread

    if (! renderResult)
        return makeError ("Export failed");

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("file_path", outputFile.getFullPathName());
        obj->setProperty ("duration", endSec - startSec);
    }
    return result;
}

juce::var CommandHandler::handleExportStems (const juce::var& params)
{
    if (! params.hasProperty ("output_dir"))
        return makeError ("Missing required parameter: output_dir");

    auto outputDirPath = params["output_dir"].toString();
    juce::File outputDir (outputDirPath);

    // Validate path (with symlink resolution)
    auto canonicalDir = outputDir.getLinkedTarget();
    bool isAllowed = false;
    for (const auto& allowedDir : allowedMediaDirectories)
    {
        if (canonicalDir.isAChildOf (allowedDir) || canonicalDir == allowedDir)
        {
            isAllowed = true;
            break;
        }
    }
    if (! isAllowed)
        return makeError ("Output directory is outside allowed directories");

    outputDir.createDirectory();

    double startSec = params.hasProperty ("start") ? (double) params["start"] : 0.0;
    double endSec = params.hasProperty ("end") ? (double) params["end"] : edit.getLength().inSeconds();

    auto audioTracks = te::getAudioTracks (edit);
    juce::Array<juce::var> exportedFiles;

    for (int i = 0; i < audioTracks.size(); ++i)
    {
        auto* track = audioTracks[i];
        if (track->getClips().isEmpty())
            continue;

        // Sanitize track name for filename
        auto safeName = track->getName().replaceCharacters (" /\\:*?\"<>|", "__________");
        auto stemFile = outputDir.getChildFile (juce::String::formatted ("%02d_", i) + safeName + ".wav");

        // Create mask for just this track
        juce::BigInteger trackMask;
        trackMask.setBit (track->getIndexInEditTrackList());

        auto ok = te::Renderer::renderToFile (
            "Export Stem: " + track->getName(),
            stemFile,
            edit,
            te::TimeRange (te::TimePosition::fromSeconds (startSec), te::TimePosition::fromSeconds (endSec)),
            trackMask,
            true,   // usePlugins
            true,   // useACID
            {},     // allowedClips
            true);  // useThread

        if (ok)
        {
            auto* fileInfo = new juce::DynamicObject();
            fileInfo->setProperty ("track_id", i);
            fileInfo->setProperty ("track_name", track->getName());
            fileInfo->setProperty ("file_path", stemFile.getFullPathName());
            exportedFiles.add (juce::var (fileInfo));
        }
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("output_dir", outputDir.getFullPathName());
        obj->setProperty ("stems", exportedFiles);
        obj->setProperty ("count", exportedFiles.size());
    }
    return result;
}

juce::var CommandHandler::handleBounceTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id"))
        return makeError ("Missing required parameter: track_id");

    int trackId = params["track_id"];
    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    if (track->getClips().isEmpty())
        return makeError ("Track has no clips to bounce");

    // Determine render range from track's clip extents
    double startSec = std::numeric_limits<double>::max();
    double endSec = 0.0;
    for (auto* clip : track->getClips())
    {
        auto pos = clip->getPosition();
        startSec = std::min (startSec, pos.getStart().inSeconds());
        endSec = std::max (endSec, pos.getEnd().inSeconds());
    }

    // Create bounce file in temp directory
    auto bounceDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("waive_bounces");
    bounceDir.createDirectory();

    auto safeName = track->getName().replaceCharacters (" /\\:*?\"<>|", "__________");
    auto bounceFile = bounceDir.getChildFile (safeName + "_bounced.wav");

    juce::BigInteger trackMask;
    trackMask.setBit (track->getIndexInEditTrackList());

    auto ok = te::Renderer::renderToFile (
        "Bounce: " + track->getName(),
        bounceFile,
        edit,
        te::TimeRange (te::TimePosition::fromSeconds (startSec), te::TimePosition::fromSeconds (endSec)),
        trackMask,
        true,  // usePlugins
        true,  // useACID
        {},    // allowedClips
        true); // useThread

    if (! ok || ! bounceFile.existsAsFile())
        return makeError ("Bounce render failed");

    // Remove all existing clips from the track (reverse iteration to avoid invalidation)
    for (int j = track->getClips().size(); --j >= 0;)
        track->getClips().getUnchecked (j)->removeFromParent();

    // Insert the bounced audio as a single clip
    te::AudioFile audioFile (edit.engine, bounceFile);
    track->insertWaveClip (
        track->getName() + " (bounced)",
        bounceFile,
        { { te::TimePosition::fromSeconds (startSec),
            te::TimePosition::fromSeconds (endSec) },
          te::TimeDuration() },
        false);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("bounce_file", bounceFile.getFullPathName());
        obj->setProperty ("start", startSec);
        obj->setProperty ("end", endSec);
    }
    return result;
}

juce::var CommandHandler::handleRemovePlugin (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("plugin_index"))
        return makeError ("Missing required parameters: track_id, plugin_index");

    int trackId = params["track_id"];
    int pluginIdx = params["plugin_index"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Only count user plugins (skip built-in VolumeAndPan, LevelMeter)
    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : track->pluginList)
    {
        if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            userPlugins.add (p);
    }

    if (pluginIdx < 0 || pluginIdx >= userPlugins.size())
        return makeError ("Plugin index out of range. Track has " + juce::String (userPlugins.size()) + " user plugins.");

    auto* plugin = userPlugins[pluginIdx];
    auto pluginName = plugin->getName();
    plugin->deleteFromParent();

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("removed_plugin", pluginName);
    }
    return result;
}

juce::var CommandHandler::handleBypassPlugin (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("plugin_index") || ! params.hasProperty ("bypassed"))
        return makeError ("Missing required parameters: track_id, plugin_index, bypassed");

    int trackId = params["track_id"];
    int pluginIdx = params["plugin_index"];
    bool bypassed = params["bypassed"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : track->pluginList)
    {
        if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            userPlugins.add (p);
    }

    if (pluginIdx < 0 || pluginIdx >= userPlugins.size())
        return makeError ("Plugin index out of range");

    auto* plugin = userPlugins[pluginIdx];
    plugin->setEnabled (! bypassed);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("plugin_name", plugin->getName());
        obj->setProperty ("bypassed", bypassed);
    }
    return result;
}

juce::var CommandHandler::handleGetPluginParameters (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("plugin_index"))
        return makeError ("Missing required parameters: track_id, plugin_index");

    int trackId = params["track_id"];
    int pluginIdx = params["plugin_index"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : track->pluginList)
    {
        if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            userPlugins.add (p);
    }

    if (pluginIdx < 0 || pluginIdx >= userPlugins.size())
        return makeError ("Plugin index out of range");

    auto* plugin = userPlugins[pluginIdx];

    juce::Array<juce::var> paramList;
    for (auto* param : plugin->getAutomatableParameters())
    {
        auto* pObj = new juce::DynamicObject();
        pObj->setProperty ("param_id", param->paramID);
        pObj->setProperty ("name", param->getParameterName());
        pObj->setProperty ("value", (double) param->getCurrentValue());

        // getDefaultValue returns std::optional<float>
        auto defVal = param->getDefaultValue();
        if (defVal.has_value())
            pObj->setProperty ("default_value", (double) *defVal);

        // Try to get value as text
        pObj->setProperty ("value_text", param->getCurrentValueAsString());

        paramList.add (juce::var (pObj));
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("plugin_name", plugin->getName());
        obj->setProperty ("plugin_index", pluginIdx);
        obj->setProperty ("enabled", plugin->isEnabled());
        obj->setProperty ("parameters", paramList);
        obj->setProperty ("parameter_count", paramList.size());
    }
    return result;
}

juce::var CommandHandler::handleGetAutomationParams (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    juce::Array<juce::var> paramList;
    auto allParams = track->getAllAutomatableParams();
    for (int i = 0; i < allParams.size(); ++i)
    {
        auto* p = allParams[i];
        auto* pObj = new juce::DynamicObject();
        pObj->setProperty ("index", i);
        pObj->setProperty ("name", p->getParameterName());
        pObj->setProperty ("label", p->getLabel());
        pObj->setProperty ("current_value", p->getCurrentValue());
        auto defVal = p->getDefaultValue();
        if (defVal.has_value())
            pObj->setProperty ("default_value", (double) *defVal);
        pObj->setProperty ("range_min", 0.0);
        pObj->setProperty ("range_max", 1.0);
        paramList.add (juce::var (pObj));
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("track_id", trackId);
    result->setProperty ("params", paramList);
    return juce::var (result);
}

juce::var CommandHandler::handleGetAutomationPoints (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto paramIndex = (int) params["param_index"];
    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();

    juce::Array<juce::var> points;
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        auto* pt = new juce::DynamicObject();
        pt->setProperty ("index", i);
        pt->setProperty ("time", curve.getPointTime (i).inSeconds());
        pt->setProperty ("value", curve.getPointValue (i));
        pt->setProperty ("curve_value", curve.getPointCurve (i));
        points.add (juce::var (pt));
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("track_id", trackId);
    result->setProperty ("param_index", paramIndex);
    result->setProperty ("param_name", param->getParameterName());
    result->setProperty ("points", points);
    return juce::var (result);
}

juce::var CommandHandler::handleAddAutomationPoint (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto paramIndex = (int) params["param_index"];
    auto timeSec = (double) params["time"];
    auto value = (float) (double) params["value"];
    auto curveVal = params.hasProperty ("curve") ? (float) (double) params["curve"] : 0.0f;

    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    value = juce::jlimit (0.0f, 1.0f, value);

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();
    auto tp = te::TimePosition::fromSeconds (timeSec);
    curve.addPoint (tp, value, curveVal, nullptr);

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("track_id", trackId);
    result->setProperty ("param_name", param->getParameterName());
    result->setProperty ("time", timeSec);
    result->setProperty ("value", (double) value);
    result->setProperty ("total_points", curve.getNumPoints());
    return juce::var (result);
}

juce::var CommandHandler::handleRemoveAutomationPoint (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto paramIndex = (int) params["param_index"];
    auto pointIndex = (int) params["point_index"];

    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();

    if (pointIndex < 0 || pointIndex >= curve.getNumPoints())
        return makeError ("Point index out of range: " + juce::String (pointIndex));

    curve.removePoint (pointIndex, nullptr);

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("remaining_points", curve.getNumPoints());
    return juce::var (result);
}

juce::var CommandHandler::handleClearAutomation (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto paramIndex = (int) params["param_index"];

    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();
    curve.clear (nullptr);

    return makeOk();
}

juce::var CommandHandler::handleSetClipFade (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto clipIndex = (int) params["clip_index"];

    auto* clip = getClipByIndex (trackId, clipIndex);
    if (! clip)
        return makeError ("Clip not found: track " + juce::String (trackId)
                          + " clip " + juce::String (clipIndex));

    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);
    if (! waveClip)
        return makeError ("Clip is not an audio clip (MIDI clips do not support fades)");

    bool changed = false;

    if (params.hasProperty ("fade_in"))
    {
        auto fadeInSec = juce::jmax (0.0, (double) params["fade_in"]);
        waveClip->setFadeIn (te::TimeDuration::fromSeconds (fadeInSec));
        changed = true;
    }

    if (params.hasProperty ("fade_out"))
    {
        auto fadeOutSec = juce::jmax (0.0, (double) params["fade_out"]);
        waveClip->setFadeOut (te::TimeDuration::fromSeconds (fadeOutSec));
        changed = true;
    }

    if (! changed)
        return makeError ("Provide fade_in and/or fade_out (in seconds)");

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("fade_in", waveClip->getFadeIn().inSeconds());
    result->setProperty ("fade_out", waveClip->getFadeOut().inSeconds());
    return juce::var (result);
}

//==============================================================================
// Helpers
//==============================================================================

te::AudioTrack* CommandHandler::getTrackById (int trackIndex)
{
    auto tracks = te::getAudioTracks (edit);
    if (trackIndex >= 0 && trackIndex < tracks.size())
        return tracks[trackIndex];
    return nullptr;
}

te::Clip* CommandHandler::getClipByIndex (int trackIndex, int clipIndex)
{
    auto* track = getTrackById (trackIndex);
    if (track == nullptr)
        return nullptr;

    auto clips = track->getClips();
    if (clipIndex >= 0 && clipIndex < clips.size())
        return clips[clipIndex];
    return nullptr;
}

juce::var CommandHandler::makeError (const juce::String& message)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("status", "error");
    obj->setProperty ("message", message);
    return juce::var (obj);
}

juce::var CommandHandler::makeOk()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("status", "ok");
    return juce::var (obj);
}
