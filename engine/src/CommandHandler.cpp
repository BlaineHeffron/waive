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
