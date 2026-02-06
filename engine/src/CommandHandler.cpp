#include "CommandHandler.h"

CommandHandler::CommandHandler (te::Edit& e) : edit (e) {}

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

        // Clip info
        juce::Array<juce::var> clipList;
        for (auto* clip : track->getClips())
        {
            auto* clipObj = new juce::DynamicObject();
            auto pos = clip->getPosition();
            clipObj->setProperty ("name", clip->getName());
            clipObj->setProperty ("start", pos.getStart().inSeconds());
            clipObj->setProperty ("end",   pos.getEnd().inSeconds());
            clipList.add (juce::var (clipObj));
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
