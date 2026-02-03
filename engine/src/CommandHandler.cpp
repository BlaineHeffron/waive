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
    else if (action == "set_track_volume")  result = handleSetTrackVolume (parsed);
    else if (action == "set_track_pan")     result = handleSetTrackPan (parsed);
    else if (action == "insert_audio_clip") result = handleInsertAudioClip (parsed);
    else if (action == "transport_play")    result = handleTransportPlay();
    else if (action == "transport_stop")    result = handleTransportStop();
    else if (action == "transport_seek")    result = handleTransportSeek (parsed);
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

    for (auto* track : te::getAudioTracks (edit))
    {
        auto* trackObj = new juce::DynamicObject();
        trackObj->setProperty ("name", track->getName());
        trackObj->setProperty ("index", track->getIndexInEditTrackList());

        // Clip info
        juce::Array<juce::var> clipList;
        for (auto* clip : track->getClips())
        {
            auto* clipObj = new juce::DynamicObject();
            clipObj->setProperty ("name", clip->getName());
            clipObj->setProperty ("start", clip->getPosition().time.start.inSeconds());
            clipObj->setProperty ("end",   clip->getPosition().time.end.inSeconds());
            clipList.add (juce::var (clipObj));
        }
        trackObj->setProperty ("clips", clipList);

        trackList.add (juce::var (trackObj));
    }

    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("tracks", trackList);

    return result;
}

juce::var CommandHandler::handleAddTrack()
{
    auto trackCount = te::getAudioTracks (edit).size();
    edit.getOrInsertAudioTrackAt (trackCount);

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
    edit.getTransport().setCurrentPosition (position);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("position", position);
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
