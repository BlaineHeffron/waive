#include "CommandHandler.h"
#include "PathSanitizer.h"
#include "PluginPresetManager.h"
#include "ProjectPackager.h"

#include <cmath>
#include <filesystem>

namespace
{
void setLoopRangeWithUndo (te::Edit& edit, te::TimePosition start, te::TimePosition end,
                           juce::UndoManager* undoManager)
{
    auto& transport = edit.getTransport();
    const auto maxEndTime = te::toPosition (edit.getLength() + te::Edit::getMaximumLength() * 0.75);
    const auto clampedStart = juce::jlimit (te::TimePosition(), maxEndTime, start);
    const auto clampedEnd = juce::jlimit (te::TimePosition(), maxEndTime, end);

    transport.loopPoint1.setValue (clampedStart, undoManager);
    transport.loopPoint2.setValue (clampedEnd, undoManager);
}

juce::String getMicAccessHelpText()
{
#if JUCE_MAC
    return "Open System Settings > Privacy & Security > Microphone and allow Waive.";
#elif JUCE_WINDOWS
    return "Open Settings > Privacy & security > Microphone and enable access for desktop apps.";
#elif JUCE_LINUX
    return "Ensure your audio stack (PipeWire/PulseAudio/ALSA) exposes an input and app permissions allow microphone access.";
#else
    return "Enable microphone permission for this app in your operating system settings.";
#endif
}

bool isAddressablePlugin (te::Plugin* plugin)
{
    return plugin != nullptr
        && dynamic_cast<te::VolumeAndPanPlugin*> (plugin) == nullptr
        && dynamic_cast<te::LevelMeterPlugin*> (plugin) == nullptr;
}

juce::Array<te::Plugin*> getAddressablePlugins (te::PluginList& pluginList)
{
    juce::Array<te::Plugin*> plugins;
    for (auto* plugin : pluginList)
        if (isAddressablePlugin (plugin))
            plugins.add (plugin);
    return plugins;
}

bool pluginMatchesIdentifier (te::Plugin& plugin, const juce::String& requestedIdentifier)
{
    if (requestedIdentifier.isEmpty())
        return false;

    if (plugin.getName() == requestedIdentifier)
        return true;

    const auto stableIdentifier = waive::PluginPresetManager::getPluginIdentifier (plugin);
    if (stableIdentifier == requestedIdentifier)
        return true;

    const auto fileIdentifier = plugin.state.getProperty ("fileOrIdentifier", {}).toString().trim();
    if (fileIdentifier == requestedIdentifier)
        return true;

    const auto typeIdentifier = plugin.state.getProperty (te::IDs::type, {}).toString().trim();
    return typeIdentifier == requestedIdentifier;
}

bool wouldCreateFolderCycle (te::Track& trackToMove, te::FolderTrack& destinationFolder)
{
    if (&trackToMove == &destinationFolder)
        return true;

    for (auto* parent = destinationFolder.getParentFolderTrack(); parent != nullptr; parent = parent->getParentFolderTrack())
        if (parent == &trackToMove)
            return true;

    return false;
}

juce::Array<te::Track*> getPublicTracks (te::Edit& edit)
{
    juce::Array<te::Track*> tracks;

    for (auto* track : edit.getTrackList())
        if (dynamic_cast<te::AudioTrack*> (track) != nullptr
            || dynamic_cast<te::FolderTrack*> (track) != nullptr)
            tracks.add (track);

    return tracks;
}

int getPublicTrackIndex (te::Edit& edit, te::Track* target)
{
    if (target == nullptr)
        return -1;

    const auto publicTracks = getPublicTracks (edit);
    for (int i = 0; i < publicTracks.size(); ++i)
        if (publicTracks.getUnchecked (i) == target)
            return i;

    return -1;
}

bool isWithinAllowedDirectories (const juce::String& originalPath,
                                 const juce::File& candidate,
                                 const juce::Array<juce::File>& allowedDirectories)
{
    if (! juce::File::isAbsolutePath (originalPath))
        return false;

    std::error_code ec;
    const auto candidatePath = std::filesystem::path (candidate.getFullPathName().toStdString());
    const auto canonicalCandidatePath = std::filesystem::weakly_canonical (candidatePath, ec);
    if (ec)
        return false;

    const auto canonicalCandidate = juce::File (juce::String (canonicalCandidatePath.string()));

    for (const auto& allowedDir : allowedDirectories)
    {
        const auto allowedPath = std::filesystem::path (allowedDir.getFullPathName().toStdString());
        const auto canonicalAllowedPath = std::filesystem::weakly_canonical (allowedPath, ec);
        if (ec)
            continue;

        const auto canonicalAllowedDir = juce::File (juce::String (canonicalAllowedPath.string()));
        if (canonicalCandidate == canonicalAllowedDir || canonicalCandidate.isAChildOf (canonicalAllowedDir))
            return true;
    }

    return false;
}

bool areCanonicalPathsEquivalent (const juce::File& lhs, const juce::File& rhs)
{
    std::error_code ec;
    const auto canonicalLhs = std::filesystem::weakly_canonical (std::filesystem::path (lhs.getFullPathName().toStdString()), ec);
    if (ec)
        return false;

    const auto canonicalRhs = std::filesystem::weakly_canonical (std::filesystem::path (rhs.getFullPathName().toStdString()), ec);
    if (ec)
        return false;

    return canonicalLhs == canonicalRhs;
}

bool copyTrackPlugins (te::Edit& edit, te::AudioTrack& sourceTrack, te::AudioTrack& destinationTrack)
{
    for (auto* sourcePlugin : getAddressablePlugins (sourceTrack.pluginList))
    {
        if (sourcePlugin == nullptr)
            return false;

        auto sourceState = sourcePlugin->state.createCopy();
        edit.createNewItemID().writeID (sourceState, nullptr);
        te::assignNewIDsToAutomationCurveModifiers (edit, sourceState);

        auto destinationPlugin = edit.getPluginCache().createNewPlugin (sourceState);
        if (destinationPlugin == nullptr)
            return false;

        destinationPlugin->restorePluginStateFromValueTree (sourceState);
        destinationTrack.pluginList.insertPlugin (destinationPlugin, destinationTrack.pluginList.size(), nullptr);
    }

    return true;
}

void copyAutomationCurveState (te::Edit& edit,
                               te::AutomatableParameter& sourceParam,
                               te::AutomatableParameter& destinationParam)
{
    auto& sourceCurve = sourceParam.getCurve();
    auto& destinationCurve = destinationParam.getCurve();

    destinationCurve.state.copyPropertiesAndChildrenFrom (sourceCurve.state.createCopy(), nullptr);
    edit.createNewItemID().writeID (destinationCurve.state, nullptr);
    te::assignNewIDsToAutomationCurveModifiers (edit, destinationCurve.state);
}

bool isOutputPathAllowed (const juce::String& originalPath,
                          const juce::File& outputPath,
                          const juce::Array<juce::File>& allowedDirectories)
{
    if (! juce::File::isAbsolutePath (originalPath))
        return false;

    return isWithinAllowedDirectories (originalPath, outputPath, allowedDirectories);
}

juce::File buildManagedBounceOutputFile (te::Edit& edit,
                                         const juce::File& projectFile,
                                         const juce::String& trackName)
{
    juce::ignoreUnused (edit);

    auto safeName = trackName.replaceCharacters (" /\\:*?\"<>|", "__________").trim();
    safeName = waive::PathSanitizer::sanitizePathComponent (safeName);
    if (safeName.isEmpty())
        safeName = "Track";

    if (projectFile != juce::File())
    {
        auto bouncesDir = projectFile.getParentDirectory()
                                     .getChildFile ("Audio")
                                     .getChildFile ("Bounces");
        if (bouncesDir.createDirectory().failed())
            return {};

        return bouncesDir.getNonexistentChildFile (safeName + "_bounced", ".wav", false);
    }

    auto bounceDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("waive_bounces");
    if (bounceDir.createDirectory().failed())
        return {};

    return bounceDir.getNonexistentChildFile (safeName + "_bounced", ".wav", false);
}

bool isInvalidRenderRange (double startSec, double endSec)
{
    return ! std::isfinite (startSec) || ! std::isfinite (endSec) || endSec <= startSec;
}

void applyFolderSoloToSubtree (te::FolderTrack& folderTrack, bool solo)
{
    folderTrack.setSolo (solo);

    for (auto* child : folderTrack.getAllSubTracks (false))
    {
        if (child == nullptr)
            continue;

        child->setSolo (solo);

        if (auto* childFolder = dynamic_cast<te::FolderTrack*> (child))
            applyFolderSoloToSubtree (*childFolder, solo);
    }
}

void applyFolderMuteToSubtree (te::FolderTrack& folderTrack, bool mute)
{
    folderTrack.setMute (mute);

    for (auto* child : folderTrack.getAllSubTracks (false))
    {
        if (child == nullptr)
            continue;

        child->setMute (mute);

        if (auto* childFolder = dynamic_cast<te::FolderTrack*> (child))
            applyFolderMuteToSubtree (*childFolder, mute);
    }
}

bool tryGetDoubleProperty (const juce::var& params,
                           std::initializer_list<const char*> propertyNames,
                           double& valueOut)
{
    for (auto* propertyName : propertyNames)
    {
        if (params.hasProperty (propertyName))
        {
            valueOut = static_cast<double> (params[propertyName]);
            return true;
        }
    }

    return false;
}

bool tryGetBoolProperty (const juce::var& params,
                         std::initializer_list<const char*> propertyNames,
                         bool& valueOut)
{
    for (auto* propertyName : propertyNames)
    {
        if (params.hasProperty (propertyName))
        {
            valueOut = static_cast<bool> (params[propertyName]);
            return true;
        }
    }

    return false;
}

bool tryGetStringProperty (const juce::var& params,
                           std::initializer_list<const char*> propertyNames,
                           juce::String& valueOut)
{
    for (auto* propertyName : propertyNames)
    {
        if (params.hasProperty (propertyName))
        {
            valueOut = params[propertyName].toString();
            return true;
        }
    }

    return false;
}

juce::String sanitiseOutputFileComponent (const juce::String& name, const juce::String& fallback)
{
    auto safe = name.replaceCharacters (" /\\:*?\"<>|", "__________").trim();
    safe = waive::PathSanitizer::sanitizePathComponent (safe);
    return safe.isNotEmpty() ? safe : fallback;
}
}

CommandHandler::CommandHandler (te::Edit& e)
    : edit (e),
      presetManager (std::make_unique<waive::PluginPresetManager>())
{
    // Initialize with safe defaults: user home + current working directory
    allowedMediaDirectories.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    allowedMediaDirectories.add (juce::File::getCurrentWorkingDirectory());
}

CommandHandler::~CommandHandler() = default;

void CommandHandler::setProjectFile (const juce::File& projectFile)
{
    currentProjectFile = projectFile;
}

void CommandHandler::setAllowedMediaDirectories (const juce::Array<juce::File>& directories)
{
    allowedMediaDirectories = directories;
}

const juce::Array<juce::File>& CommandHandler::getAllowedMediaDirectories() const
{
    return allowedMediaDirectories;
}

juce::File CommandHandler::resolveProjectFile() const
{
    if (currentProjectFile != juce::File())
        return currentProjectFile;

    return edit.editFileRetriever();
}

bool CommandHandler::requireIntProperty (const juce::var& params,
                                         const char* propertyName,
                                         int& valueOut,
                                         juce::var& errorResult)
{
    if (! params.hasProperty (propertyName))
    {
        errorResult = makeError ("Missing required parameter: " + juce::String (propertyName));
        return false;
    }

    const auto& value = params[propertyName];
    if (! value.isInt() && ! value.isInt64())
    {
        errorResult = makeError ("Parameter must be integer: " + juce::String (propertyName));
        return false;
    }

    valueOut = static_cast<int> (value);
    return true;
}

bool CommandHandler::requireDoubleProperty (const juce::var& params,
                                            const char* propertyName,
                                            double& valueOut,
                                            juce::var& errorResult)
{
    if (! params.hasProperty (propertyName))
    {
        errorResult = makeError ("Missing required parameter: " + juce::String (propertyName));
        return false;
    }

    const auto& value = params[propertyName];
    if (! value.isInt() && ! value.isInt64() && ! value.isDouble())
    {
        errorResult = makeError ("Parameter must be numeric: " + juce::String (propertyName));
        return false;
    }

    valueOut = static_cast<double> (value);
    return true;
}

bool CommandHandler::requireBoolProperty (const juce::var& params,
                                          const char* propertyName,
                                          bool& valueOut,
                                          juce::var& errorResult)
{
    if (! params.hasProperty (propertyName))
    {
        errorResult = makeError ("Missing required parameter: " + juce::String (propertyName));
        return false;
    }

    const auto& value = params[propertyName];
    if (! value.isBool())
    {
        errorResult = makeError ("Parameter must be boolean: " + juce::String (propertyName));
        return false;
    }

    valueOut = static_cast<bool> (value);
    return true;
}

bool CommandHandler::requireOptionalDoubleProperty (const juce::var& params,
                                                    const char* propertyName,
                                                    double& valueOut,
                                                    juce::var& errorResult)
{
    if (! params.hasProperty (propertyName))
        return true;

    const auto& value = params[propertyName];
    if (! value.isInt() && ! value.isInt64() && ! value.isDouble())
    {
        errorResult = makeError ("Parameter must be numeric: " + juce::String (propertyName));
        return false;
    }

    valueOut = static_cast<double> (value);
    return true;
}

bool CommandHandler::requireStringProperty (const juce::var& params,
                                            const char* propertyName,
                                            juce::String& valueOut,
                                            juce::var& errorResult)
{
    if (! params.hasProperty (propertyName))
    {
        errorResult = makeError ("Missing required parameter: " + juce::String (propertyName));
        return false;
    }

    const auto& value = params[propertyName];
    if (! value.isString())
    {
        errorResult = makeError ("Parameter must be string: " + juce::String (propertyName));
        return false;
    }

    valueOut = value.toString();
    return true;
}

bool CommandHandler::requireOptionalStringProperty (const juce::var& params,
                                                    const char* propertyName,
                                                    juce::String& valueOut,
                                                    juce::var& errorResult)
{
    if (! params.hasProperty (propertyName))
        return true;

    return requireStringProperty (params, propertyName, valueOut, errorResult);
}

bool CommandHandler::requireAnyStringProperty (const juce::var& params,
                                               std::initializer_list<const char*> propertyNames,
                                               juce::String& valueOut,
                                               juce::var& errorResult)
{
    for (auto* propertyName : propertyNames)
    {
        if (params.hasProperty (propertyName))
            return requireStringProperty (params, propertyName, valueOut, errorResult);
    }

    errorResult = makeError ("Missing required parameter: " + juce::String (*propertyNames.begin()));
    return false;
}

bool CommandHandler::requireAnyBoolProperty (const juce::var& params,
                                             std::initializer_list<const char*> propertyNames,
                                             bool& valueOut,
                                             juce::var& errorResult)
{
    for (auto* propertyName : propertyNames)
    {
        if (params.hasProperty (propertyName))
            return requireBoolProperty (params, propertyName, valueOut, errorResult);
    }

    errorResult = makeError ("Missing required parameter: " + juce::String (*propertyNames.begin()));
    return false;
}

bool CommandHandler::requireAnyDoubleProperty (const juce::var& params,
                                               std::initializer_list<const char*> propertyNames,
                                               double& valueOut,
                                               juce::var& errorResult)
{
    for (auto* propertyName : propertyNames)
    {
        if (params.hasProperty (propertyName))
        {
            if (! requireOptionalDoubleProperty (params, propertyName, valueOut, errorResult))
                return false;

            return true;
        }
    }

    errorResult = makeError ("Missing required parameter: " + juce::String (*propertyNames.begin()));
    return false;
}

//==============================================================================
juce::String CommandHandler::handleCommand (const juce::String& jsonString)
{
    auto parsed = juce::JSON::parse (jsonString);

    if (! parsed.isObject())
        return juce::JSON::toString (makeError ("Invalid JSON"));

    juce::String action;
    juce::var errorResult;
    if (! requireStringProperty (parsed, "action", action, errorResult))
        return juce::JSON::toString (errorResult);

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
    else if (action == "set_time_signature")       result = handleSetTimeSignature (parsed);
    else if (action == "add_marker")               result = handleAddMarker (parsed);
    else if (action == "remove_marker")            result = handleRemoveMarker (parsed);
    else if (action == "list_markers")             result = handleListMarkers();
    else if (action == "reorder_track")            result = handleReorderTrack (parsed);
    else if (action == "save_plugin_preset")       result = handleSavePluginPreset (parsed);
    else if (action == "load_plugin_preset")       result = handleLoadPluginPreset (parsed);
    else if (action == "add_folder_track")         result = handleAddFolderTrack (parsed);
    else if (action == "move_track_to_folder")     result = handleMoveTrackToFolder (parsed);
    else if (action == "remove_from_folder")       result = handleRemoveFromFolder (parsed);
    else if (action == "collect_and_save")         result = handleCollectAndSave();
    else if (action == "remove_unused_media")      result = handleRemoveUnusedMedia();
    else if (action == "package_as_zip")           result = handlePackageAsZip (parsed);
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

    // Build index mapping for all tracks (folders + audio)
    juce::HashMap<te::Track*, int> trackIndexMap;
    const auto publicTracks = getPublicTracks (edit);
    for (int trackId = 0; trackId < publicTracks.size(); ++trackId)
        trackIndexMap.set (publicTracks.getUnchecked (trackId), trackId);

    for (int trackId = 0; trackId < publicTracks.size(); ++trackId)
    {
        auto* track = publicTracks.getUnchecked (trackId);
        auto* audioTrack = dynamic_cast<te::AudioTrack*> (track);
        auto* folderTrack = dynamic_cast<te::FolderTrack*> (track);

        auto* trackObj = new juce::DynamicObject();
        trackObj->setProperty ("name", track->getName());
        trackObj->setProperty ("track_id", trackId);
        trackObj->setProperty ("index", track->getIndexInEditTrackList());
        trackObj->setProperty ("is_folder", folderTrack != nullptr);

        // Parent folder info
        auto* parentFolder = track->getParentFolderTrack();
        if (parentFolder && trackIndexMap.contains (parentFolder))
            trackObj->setProperty ("parent_folder", trackIndexMap[parentFolder]);
        else
            trackObj->setProperty ("parent_folder", juce::var());

        // Children info for folder tracks
        if (folderTrack)
        {
            juce::Array<juce::var> childrenArray;
            for (auto* child : folderTrack->getAllSubTracks (false))
            {
                if (trackIndexMap.contains (child))
                    childrenArray.add (trackIndexMap[child]);
            }
            trackObj->setProperty ("children", childrenArray);
            trackObj->setProperty ("solo", folderTrack->isSolo (false));
            trackObj->setProperty ("mute", folderTrack->isMuted (false));
        }
        else if (audioTrack)
        {
            trackObj->setProperty ("children", juce::Array<juce::var>());
            trackObj->setProperty ("solo", audioTrack->isSolo (false));
            trackObj->setProperty ("mute", audioTrack->isMuted (false));

            // Clip info
            juce::Array<juce::var> clipList;
            int clipIdx = 0;
            for (auto* clip : audioTrack->getClips())
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
        }

        trackList.add (juce::var (trackObj));
    }

    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("tracks", trackList);

    return result;
}

juce::var CommandHandler::handleAddTrack()
{
    auto trackCount = te::getAudioTracks (edit).size();
    edit.ensureNumberOfAudioTracks (trackCount + 1);
    auto* newTrack = te::getAudioTracks (edit).getLast();

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("track_index", getPublicTrackIndex (edit, newTrack));
    return result;
}

juce::var CommandHandler::handleSetTrackVolume (const juce::var& params)
{
    juce::var errorResult;
    int trackId = 0;
    double valueDbDouble = 0.0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireDoubleProperty (params, "value_db", valueDbDouble, errorResult))
        return errorResult;

    auto valueDb = static_cast<float> (valueDbDouble);

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
    juce::var errorResult;
    int trackId = 0;
    double panValueDouble = 0.0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireDoubleProperty (params, "value", panValueDouble, errorResult))
        return errorResult;

    auto panValue = static_cast<float> (panValueDouble);

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
    juce::var errorResult;
    int trackId = 0;
    juce::String filePath;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireStringProperty (params, "file_path", filePath, errorResult))
        return errorResult;

    double startTime = 0.0;
    if (! requireOptionalDoubleProperty (params, "start_time", startTime, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

    juce::File file (filePath);
    if (! file.existsAsFile())
        return makeError ("File not found: " + filePath);

    // Validate file path is within allowed directories
    if (! isWithinAllowedDirectories (filePath, file, allowedMediaDirectories))
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
    juce::var errorResult;
    int trackId = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult))
        return errorResult;

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
    juce::var errorResult;
    int trackId = 0;
    juce::String filePath;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireStringProperty (params, "file_path", filePath, errorResult))
        return errorResult;

    double startTime = 0.0;
    if (! requireOptionalDoubleProperty (params, "start_time", startTime, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

    juce::File file (filePath);
    if (! file.existsAsFile())
        return makeError ("File not found: " + filePath);

    // Validate file path is within allowed directories
    if (! isWithinAllowedDirectories (filePath, file, allowedMediaDirectories))
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
    juce::var errorResult;
    int trackId = 0;
    juce::String pluginId;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireStringProperty (params, "plugin_id", pluginId, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

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
    juce::var errorResult;
    int trackId = 0;
    juce::String pluginId;
    juce::String paramId;
    double valueDouble = 0.0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireStringProperty (params, "plugin_id", pluginId, errorResult)
        || ! requireStringProperty (params, "param_id", paramId, errorResult)
        || ! requireDoubleProperty (params, "value", valueDouble, errorResult))
        return errorResult;

    auto value = static_cast<float> (valueDouble);

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Find the plugin on the track.
    te::Plugin* foundPlugin = nullptr;
    for (auto* p : getAddressablePlugins (track->pluginList))
    {
        if (p != nullptr && pluginMatchesIdentifier (*p, pluginId))
        {
            foundPlugin = p;
            break;
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
    juce::var errorResult;
    double position = 0.0;
    if (! requireDoubleProperty (params, "position", position, errorResult))
        return errorResult;

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
    juce::var errorResult;
    int trackId = 0;
    bool armed = false;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireAnyBoolProperty (params, { "armed", "enabled" }, armed, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

    // If input_device specified, assign it first
    juce::String inputDeviceName;
    if (! requireOptionalStringProperty (params, "input_device", inputDeviceName, errorResult))
        return errorResult;

    if (params.hasProperty ("input_device"))
    {
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
    auto permission = juce::RuntimePermissions::recordAudio;
    if (juce::RuntimePermissions::isRequired (permission)
        && ! juce::RuntimePermissions::isGranted (permission))
    {
        return makeError ("Microphone permission not granted. " + getMicAccessHelpText());
    }

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
        obj->setProperty ("track_index", getPublicTrackIndex (edit, targetTrack));
        obj->setProperty ("input_device", waveIn->getName());
    }
    return result;
}

juce::var CommandHandler::handleSplitClip (const juce::var& params)
{
    juce::var errorResult;
    int trackId = 0;
    int clipIdx = 0;
    double position = 0.0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "clip_index", clipIdx, errorResult)
        || ! requireAnyDoubleProperty (params, { "position", "time" }, position, errorResult))
        return errorResult;

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
    juce::var errorResult;
    int trackId = 0;
    int clipIdx = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "clip_index", clipIdx, errorResult))
        return errorResult;

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
    juce::var errorResult;
    int trackId = 0;
    int clipIdx = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "clip_index", clipIdx, errorResult))
        return errorResult;

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    double newStart = 0.0;
    if (! requireOptionalDoubleProperty (params, "new_start", newStart, errorResult))
        return errorResult;

    if (! params.hasProperty ("new_start"))
    {
        double deltaSeconds = 0.0;
        if (! requireAnyDoubleProperty (params, { "delta_seconds" }, deltaSeconds, errorResult))
            return errorResult;

        newStart = clip->getPosition().getStart().inSeconds() + deltaSeconds;
    }

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
    juce::var errorResult;
    int trackId = 0;
    int clipIdx = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "clip_index", clipIdx, errorResult))
        return errorResult;

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
    juce::var errorResult;
    int trackId = 0;
    int clipIdx = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "clip_index", clipIdx, errorResult))
        return errorResult;

    bool hasNewStart = params.hasProperty ("new_start");
    bool hasNewEnd = params.hasProperty ("new_end");
    if (! hasNewStart && ! hasNewEnd)
        return makeError ("At least one of new_start or new_end must be provided");

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    if (hasNewStart)
    {
        double newStart = 0.0;
        if (! requireOptionalDoubleProperty (params, "new_start", newStart, errorResult))
            return errorResult;
        clip->setStart (te::TimePosition::fromSeconds (newStart), false, true);
    }

    if (hasNewEnd)
    {
        double newEnd = 0.0;
        if (! requireOptionalDoubleProperty (params, "new_end", newEnd, errorResult))
            return errorResult;
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
    juce::var errorResult;
    int trackId = 0;
    int clipIdx = 0;
    double gainDb = 0.0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "clip_index", clipIdx, errorResult)
        || ! requireDoubleProperty (params, "gain_db", gainDb, errorResult))
        return errorResult;

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
    juce::var errorResult;
    int trackId = 0;
    int clipIdx = 0;
    juce::String newName;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "clip_index", clipIdx, errorResult)
        || ! requireAnyStringProperty (params, { "name", "new_name" }, newName, errorResult))
        return errorResult;

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
    juce::var errorResult;
    int trackId = 0;
    juce::String newName;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireAnyStringProperty (params, { "name", "new_name" }, newName, errorResult))
        return errorResult;

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
    juce::var errorResult;
    int trackId = 0;
    bool solo = false;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireBoolProperty (params, "solo", solo, errorResult))
        return errorResult;

    // Find track in all tracks (including folders)
    te::Track* track = nullptr;
    int currentId = 0;
    for (auto* t : edit.getTrackList())
    {
        if (dynamic_cast<te::AudioTrack*> (t) || dynamic_cast<te::FolderTrack*> (t))
        {
            if (currentId == trackId)
            {
                track = t;
                break;
            }
            ++currentId;
        }
    }

    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Check if folder track
    if (auto* folderTrack = dynamic_cast<te::FolderTrack*> (track))
    {
        applyFolderSoloToSubtree (*folderTrack, solo);
    }
    else
    {
        track->setSolo (solo);
    }

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
    juce::var errorResult;
    int trackId = 0;
    bool mute = false;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireBoolProperty (params, "mute", mute, errorResult))
        return errorResult;

    // Find track in all tracks (including folders)
    te::Track* track = nullptr;
    int currentId = 0;
    for (auto* t : edit.getTrackList())
    {
        if (dynamic_cast<te::AudioTrack*> (t) || dynamic_cast<te::FolderTrack*> (t))
        {
            if (currentId == trackId)
            {
                track = t;
                break;
            }
            ++currentId;
        }
    }

    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Check if folder track
    if (auto* folderTrack = dynamic_cast<te::FolderTrack*> (track))
    {
        applyFolderMuteToSubtree (*folderTrack, mute);
    }
    else
    {
        track->setMute (mute);
    }

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
    juce::var errorResult;
    int trackId = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult))
        return errorResult;

    auto* sourceTrack = getAudioTrackById (trackId);
    if (sourceTrack == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

    // Create a new track
    auto trackCount = te::getAudioTracks (edit).size();
    edit.ensureNumberOfAudioTracks (trackCount + 1);
    auto* newTrack = te::getAudioTracks (edit).getLast();
    if (newTrack == nullptr)
        return makeError ("Failed to create new track");

    newTrack->setName (sourceTrack->getName() + " copy");
    newTrack->setMute (sourceTrack->isMuted (false));
    newTrack->setSolo (sourceTrack->isSolo (false));

    // Restore the full built-in volume/pan plugin state so automation metadata
    // is preserved alongside current values and curve points.
    auto srcVolPlugins = sourceTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    auto dstVolPlugins = newTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    if (! srcVolPlugins.isEmpty() && ! dstVolPlugins.isEmpty())
    {
        auto sourceState = srcVolPlugins.getFirst()->state.createCopy();
        edit.createNewItemID().writeID (sourceState, nullptr);
        te::assignNewIDsToAutomationCurveModifiers (edit, sourceState);
        dstVolPlugins.getFirst()->state.copyPropertiesAndChildrenFrom (sourceState, nullptr);
        dstVolPlugins.getFirst()->restorePluginStateFromValueTree (dstVolPlugins.getFirst()->state);
        copyAutomationCurveState (edit, *srcVolPlugins.getFirst()->volParam, *dstVolPlugins.getFirst()->volParam);
        copyAutomationCurveState (edit, *srcVolPlugins.getFirst()->panParam, *dstVolPlugins.getFirst()->panParam);
    }

    if (! copyTrackPlugins (edit, *sourceTrack, *newTrack))
    {
        edit.deleteTrack (newTrack);
        return makeError ("Failed to duplicate one or more track plugins");
    }

    // Copy all clips from source to new track
    for (auto* clip : sourceTrack->getClips())
    {
        auto clipState = clip->state.createCopy();
        edit.createNewItemID().writeID (clipState, nullptr);
        te::assignNewIDsToAutomationCurveModifiers (edit, clipState);
        if (newTrack->insertClipWithState (clipState) == nullptr)
        {
            edit.deleteTrack (newTrack);
            return makeError ("Failed to duplicate one or more clips on the track");
        }
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("source_track_id", trackId);
        obj->setProperty ("new_track_index", getPublicTrackIndex (edit, newTrack));
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
    juce::var errorResult;
    double bpm = 0.0;
    if (! requireAnyDoubleProperty (params, { "bpm", "value" }, bpm, errorResult))
        return errorResult;

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
    juce::var errorResult;
    bool enabled = false;
    if (! requireBoolProperty (params, "enabled", enabled, errorResult))
        return errorResult;

    double startSec = 0.0;
    double endSec = 0.0;
    const bool hasStart = params.hasProperty ("start");
    const bool hasEnd = params.hasProperty ("end");
    if (! requireOptionalDoubleProperty (params, "start", startSec, errorResult)
        || ! requireOptionalDoubleProperty (params, "end", endSec, errorResult))
        return errorResult;

    if (enabled && params.hasProperty ("start") && params.hasProperty ("end"))
    {
        if (startSec >= endSec)
            return makeError ("Loop start must be less than end");
    }

    auto& transport = edit.getTransport();
    auto* undoManager = &edit.getUndoManager();
    transport.looping.setValue (enabled, undoManager);

    if (enabled && hasStart && hasEnd)
    {
        setLoopRangeWithUndo (edit,
                              te::TimePosition::fromSeconds (startSec),
                              te::TimePosition::fromSeconds (endSec),
                              undoManager);
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
    juce::var errorResult;
    juce::String filePath;
    if (! requireStringProperty (params, "file_path", filePath, errorResult))
        return errorResult;
    juce::File outputFile (filePath);

    // Validate output path
    if (! isOutputPathAllowed (filePath, outputFile, allowedMediaDirectories))
        return makeError ("Output path is outside allowed directories: " + filePath);

    // Determine render range
    double startSec = 0.0;
    double endSec = edit.getLength().inSeconds();
    if (! requireOptionalDoubleProperty (params, "start", startSec, errorResult)
        || ! requireOptionalDoubleProperty (params, "end", endSec, errorResult))
        return errorResult;

    if (isInvalidRenderRange (startSec, endSec))
        return makeError ("End time must be greater than start time");

    // Ensure parent directory exists only after validation succeeds.
    outputFile.getParentDirectory().createDirectory();

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
        false);   // useThread

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
    juce::var errorResult;
    juce::String outputDirPath;
    if (! requireStringProperty (params, "output_dir", outputDirPath, errorResult))
        return errorResult;
    juce::File outputDir (outputDirPath);

    // Validate path (with symlink resolution)
    if (! isWithinAllowedDirectories (outputDirPath, outputDir, allowedMediaDirectories))
        return makeError ("Output directory is outside allowed directories");

    double startSec = 0.0;
    double endSec = edit.getLength().inSeconds();
    if (! requireOptionalDoubleProperty (params, "start", startSec, errorResult)
        || ! requireOptionalDoubleProperty (params, "end", endSec, errorResult))
        return errorResult;

    if (isInvalidRenderRange (startSec, endSec))
        return makeError ("End time must be greater than start time");

    outputDir.createDirectory();

    auto audioTracks = te::getAudioTracks (edit);
    juce::Array<juce::var> exportedFiles;
    juce::StringArray errors;

    for (int i = 0; i < audioTracks.size(); ++i)
    {
        auto* track = audioTracks[i];
        if (track->getClips().isEmpty())
            continue;

        // Sanitize track name for filename
        auto safeName = sanitiseOutputFileComponent (track->getName(), "Track");
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
            false); // useThread

        if (ok)
        {
            auto* fileInfo = new juce::DynamicObject();
            fileInfo->setProperty ("track_id", getPublicTrackIndex (edit, track));
            fileInfo->setProperty ("track_name", track->getName());
            fileInfo->setProperty ("file_path", stemFile.getFullPathName());
            exportedFiles.add (juce::var (fileInfo));
        }
        else
        {
            errors.add ("Failed to render stem for track: " + track->getName());
        }
    }

    auto result = errors.isEmpty()
                    ? makeOk()
                    : makeError ("Export stems failed: " + errors.joinIntoString ("; "));
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("output_dir", outputDir.getFullPathName());
        obj->setProperty ("stems", exportedFiles);
        obj->setProperty ("count", exportedFiles.size());
        if (! errors.isEmpty())
        {
            juce::Array<juce::var> errorArray;
            for (const auto& error : errors)
                errorArray.add (error);
            obj->setProperty ("errors", errorArray);
        }
    }
    return result;
}

juce::var CommandHandler::handleBounceTrack (const juce::var& params)
{
    juce::var errorResult;
    int trackId = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

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

    auto bounceFile = buildManagedBounceOutputFile (edit, resolveProjectFile(), track->getName());
    if (bounceFile == juce::File())
        return makeError ("Failed to prepare bounce output file");

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
        false); // useThread

    if (! ok || ! bounceFile.existsAsFile())
        return makeError ("Bounce render failed");

    auto bouncedClip = track->insertWaveClip (
        track->getName() + " (bounced)",
        bounceFile,
        { { te::TimePosition::fromSeconds (startSec),
            te::TimePosition::fromSeconds (endSec) },
          te::TimeDuration() },
        false);
    if (bouncedClip == nullptr)
    {
        (void) bounceFile.deleteFile();
        return makeError ("Bounce render succeeded but failed to insert bounced clip");
    }

    // Remove all original clips once the bounced clip is safely in the edit.
    for (int j = track->getClips().size(); --j >= 0;)
    {
        auto* existingClip = track->getClips().getUnchecked (j);
        if (existingClip != nullptr && existingClip != bouncedClip.get())
            existingClip->removeFromParent();
    }

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
    juce::var errorResult;
    int trackId = 0;
    int pluginIdx = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "plugin_index", pluginIdx, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

    auto userPlugins = getAddressablePlugins (track->pluginList);

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
    juce::var errorResult;
    int trackId = 0;
    int pluginIdx = 0;
    bool bypassed = false;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "plugin_index", pluginIdx, errorResult)
        || ! requireBoolProperty (params, "bypassed", bypassed, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

    auto userPlugins = getAddressablePlugins (track->pluginList);

    if (pluginIdx < 0 || pluginIdx >= userPlugins.size())
        return makeError ("Plugin index out of range. Track has " + juce::String (userPlugins.size()) + " user plugins.");

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
    juce::var errorResult;
    int trackId = 0;
    int pluginIdx = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "plugin_index", pluginIdx, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
        return makeError ("Audio track not found: " + juce::String (trackId));

    auto userPlugins = getAddressablePlugins (track->pluginList);

    if (pluginIdx < 0 || pluginIdx >= userPlugins.size())
        return makeError ("Plugin index out of range. Track has " + juce::String (userPlugins.size()) + " user plugins.");

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
    juce::var errorResult;
    int trackId = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (! track)
        return makeError ("Audio track not found: " + juce::String (trackId));

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
    juce::var errorResult;
    int trackId = 0;
    int paramIndex = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "param_index", paramIndex, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (! track)
        return makeError ("Audio track not found: " + juce::String (trackId));

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
    juce::var errorResult;
    int trackId = 0;
    int paramIndex = 0;
    double timeSec = 0.0;
    double valueDouble = 0.0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "param_index", paramIndex, errorResult)
        || ! requireDoubleProperty (params, "time", timeSec, errorResult)
        || ! requireDoubleProperty (params, "value", valueDouble, errorResult))
        return errorResult;

    auto value = (float) valueDouble;
    double curveDouble = 0.0;
    if (! requireOptionalDoubleProperty (params, "curve", curveDouble, errorResult))
        return errorResult;
    auto curveVal = (float) curveDouble;

    auto* track = getAudioTrackById (trackId);
    if (! track)
        return makeError ("Audio track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    value = juce::jlimit (0.0f, 1.0f, value);

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();
    auto tp = te::TimePosition::fromSeconds (timeSec);
    curve.addPoint (tp, value, curveVal, &edit.getUndoManager());

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
    juce::var errorResult;
    int trackId = 0;
    int paramIndex = 0;
    int pointIndex = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "param_index", paramIndex, errorResult)
        || ! requireIntProperty (params, "point_index", pointIndex, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (! track)
        return makeError ("Audio track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();

    if (pointIndex < 0 || pointIndex >= curve.getNumPoints())
        return makeError ("Point index out of range: " + juce::String (pointIndex));

    curve.removePoint (pointIndex, &edit.getUndoManager());

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("remaining_points", curve.getNumPoints());
    return juce::var (result);
}

juce::var CommandHandler::handleClearAutomation (const juce::var& params)
{
    juce::var errorResult;
    int trackId = 0;
    int paramIndex = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "param_index", paramIndex, errorResult))
        return errorResult;

    auto* track = getAudioTrackById (trackId);
    if (! track)
        return makeError ("Audio track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();
    curve.clear (&edit.getUndoManager());

    return makeOk();
}

juce::var CommandHandler::handleSetClipFade (const juce::var& params)
{
    juce::var errorResult;
    int trackId = 0;
    int clipIndex = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "clip_index", clipIndex, errorResult))
        return errorResult;

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
        double fadeInSec = 0.0;
        if (! requireOptionalDoubleProperty (params, "fade_in", fadeInSec, errorResult))
            return errorResult;
        fadeInSec = juce::jmax (0.0, fadeInSec);
        waveClip->setFadeIn (te::TimeDuration::fromSeconds (fadeInSec));
        changed = true;
    }

    if (params.hasProperty ("fade_out"))
    {
        double fadeOutSec = 0.0;
        if (! requireOptionalDoubleProperty (params, "fade_out", fadeOutSec, errorResult))
            return errorResult;
        fadeOutSec = juce::jmax (0.0, fadeOutSec);
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

te::Track* CommandHandler::getTrackById (int trackIndex)
{
    if (trackIndex < 0)
        return nullptr;

    int publicTrackIndex = 0;
    for (auto* track : edit.getTrackList())
    {
        if (dynamic_cast<te::AudioTrack*> (track) == nullptr
            && dynamic_cast<te::FolderTrack*> (track) == nullptr)
            continue;

        if (publicTrackIndex == trackIndex)
            return track;

        ++publicTrackIndex;
    }

    return nullptr;
}

te::AudioTrack* CommandHandler::getAudioTrackById (int trackIndex)
{
    auto* track = getTrackById (trackIndex);
    return dynamic_cast<te::AudioTrack*> (track);
}

te::PluginList* CommandHandler::getPluginListForParams (const juce::var& params,
                                                        juce::String& errorMessage,
                                                        juce::String* targetDescription)
{
    const bool useMaster = params.hasProperty ("master")
                             && params["master"].isBool()
                             && static_cast<bool> (params["master"]);
    if (useMaster)
    {
        if (targetDescription != nullptr)
            *targetDescription = "master";
        return &edit.getMasterPluginList();
    }

    if (! params.hasProperty ("track_index"))
    {
        errorMessage = "Missing required parameter: track_index (or set master=true)";
        return nullptr;
    }

    const auto& trackIndexValue = params["track_index"];
    if (! trackIndexValue.isInt() && ! trackIndexValue.isInt64())
    {
        errorMessage = "Parameter must be integer: track_index";
        return nullptr;
    }

    const int trackId = static_cast<int> (trackIndexValue);
    auto* track = getAudioTrackById (trackId);
    if (track == nullptr)
    {
        errorMessage = "Audio track " + juce::String (trackId) + " not found";
        return nullptr;
    }

    if (targetDescription != nullptr)
        *targetDescription = "track " + juce::String (trackId);

    return &track->pluginList;
}

te::Clip* CommandHandler::getClipByIndex (int trackIndex, int clipIndex)
{
    auto* track = getAudioTrackById (trackIndex);
    if (track == nullptr)
        return nullptr;

    auto clips = track->getClips();
    if (clipIndex >= 0 && clipIndex < clips.size())
        return clips[clipIndex];
    return nullptr;
}

juce::var CommandHandler::handleSetTimeSignature (const juce::var& params)
{
    juce::var errorResult;
    int numerator = 0;
    int denominator = 0;
    if (! requireIntProperty (params, "numerator", numerator, errorResult)
        || ! requireIntProperty (params, "denominator", denominator, errorResult))
        return errorResult;

    if (numerator < 1 || numerator > 32)
        return makeError ("Numerator must be 1-32");
    if (denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16)
        return makeError ("Denominator must be 2, 4, 8, or 16");

    auto& tempoSeq = edit.tempoSequence;
    if (auto* timeSig = tempoSeq.getTimeSig (0))
    {
        timeSig->numerator = numerator;
        timeSig->denominator = denominator;
    }
    else
    {
        // No existing time signature - insert a new one at time 0
        tempoSeq.insertTimeSig (te::TimePosition::fromSeconds (0.0));
        if (auto* newTimeSig = tempoSeq.getTimeSig (0))
        {
            newTimeSig->numerator = numerator;
            newTimeSig->denominator = denominator;
        }
        else
        {
            return makeError ("Failed to create time signature");
        }
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("numerator", numerator);
    result->setProperty ("denominator", denominator);
    return juce::var (result);
}

juce::var CommandHandler::handleAddMarker (const juce::var& params)
{
    juce::var errorResult;
    double timeSec = 0.0;
    juce::String name;
    if (! requireDoubleProperty (params, "time", timeSec, errorResult)
        || ! requireStringProperty (params, "name", name, errorResult))
        return errorResult;

    if (name.isEmpty())
        return makeError ("Marker name is required");
    if (timeSec < 0.0)
        return makeError ("Time must be >= 0");

    auto* markerTrack = edit.getMarkerTrack();
    if (! markerTrack)
        return makeError ("Could not access marker track");

    auto tp = te::TimePosition::fromSeconds (timeSec);
    auto markerClip = dynamic_cast<te::MarkerClip*> (
        markerTrack->insertNewClip (te::TrackItem::Type::marker, name,
                                     te::TimeRange (tp, tp + te::TimeDuration::fromSeconds (0.1)),
                                     nullptr));

    if (! markerClip)
        return makeError ("Failed to create marker");

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("name", name);
    result->setProperty ("time", timeSec);
    result->setProperty ("marker_id", markerClip->getMarkerID());
    return juce::var (result);
}

juce::var CommandHandler::handleRemoveMarker (const juce::var& params)
{
    juce::var errorResult;
    int markerId = 0;
    if (! requireIntProperty (params, "marker_id", markerId, errorResult))
        return errorResult;

    auto* markerTrack = edit.getMarkerTrack();
    if (! markerTrack)
        return makeError ("Could not access marker track");

    for (auto* clip : markerTrack->getClips())
    {
        if (auto* mc = dynamic_cast<te::MarkerClip*> (clip))
        {
            if (mc->getMarkerID() == markerId)
            {
                mc->removeFromParent();
                return makeOk();
            }
        }
    }

    return makeError ("Marker not found with ID: " + juce::String (markerId));
}

juce::var CommandHandler::handleListMarkers()
{
    auto* markerTrack = edit.getMarkerTrack();

    juce::Array<juce::var> markerList;
    if (markerTrack)
    {
        for (auto* clip : markerTrack->getClips())
        {
            if (auto* mc = dynamic_cast<te::MarkerClip*> (clip))
            {
                auto* m = new juce::DynamicObject();
                m->setProperty ("marker_id", mc->getMarkerID());
                m->setProperty ("name", mc->getName());
                m->setProperty ("time", mc->getPosition().getStart().inSeconds());
                markerList.add (juce::var (m));
            }
        }
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("markers", markerList);
    return juce::var (result);
}

juce::var CommandHandler::handleReorderTrack (const juce::var& params)
{
    juce::var errorResult;
    int trackId = 0;
    int newPosition = 0;
    if (! requireIntProperty (params, "track_id", trackId, errorResult)
        || ! requireIntProperty (params, "new_position", newPosition, errorResult))
        return errorResult;

    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    const auto publicTracks = getPublicTracks (edit);
    if (newPosition < 0 || newPosition >= publicTracks.size())
        return makeError ("new_position out of range (0-" + juce::String (publicTracks.size() - 1) + ")");

    auto* destTrack = publicTracks.getUnchecked (newPosition);
    if (destTrack != track)
    {
        auto* destinationFolder = destTrack->getParentFolderTrack();
        if (destinationFolder != nullptr)
            if (auto* folderTrack = dynamic_cast<te::FolderTrack*> (track))
                if (wouldCreateFolderCycle (*folderTrack, *destinationFolder))
                    return makeError ("Cannot reorder a folder into its own subtree");

        edit.moveTrack (track, te::TrackInsertPoint (destinationFolder, destTrack));
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("track_id", trackId);
    result->setProperty ("new_position", newPosition);
    return juce::var (result);
}

juce::var CommandHandler::handleSavePluginPreset (const juce::var& params)
{
    juce::var errorResult;
    int pluginIndex = 0;
    juce::String presetName;
    if (! requireIntProperty (params, "plugin_index", pluginIndex, errorResult)
        || ! requireStringProperty (params, "preset_name", presetName, errorResult))
        return errorResult;

    if (presetName.isEmpty())
        return makeError ("preset_name is required");

    juce::String errorMessage;
    juce::String targetDescription;
    auto* pluginList = getPluginListForParams (params, errorMessage, &targetDescription);
    if (pluginList == nullptr)
        return makeError (errorMessage);

    auto userPlugins = getAddressablePlugins (*pluginList);

    if (pluginIndex < 0 || pluginIndex >= userPlugins.size())
        return makeError ("Plugin index out of range. " + targetDescription + " has "
                          + juce::String (userPlugins.size()) + " user plugins.");

    auto plugin = userPlugins[pluginIndex];
    if (plugin == nullptr)
        return makeError ("Plugin not found");

    if (! presetManager->savePreset (*plugin, presetName))
        return makeError ("Failed to save preset");

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("preset_name", presetName);
    const bool useMaster = params.hasProperty ("master")
                             && params["master"].isBool()
                             && static_cast<bool> (params["master"]);
    if (useMaster)
        result->setProperty ("master", true);
    else if (params.hasProperty ("track_index"))
        result->setProperty ("track_index", static_cast<int> (params["track_index"]));
    return juce::var (result);
}

juce::var CommandHandler::handleLoadPluginPreset (const juce::var& params)
{
    juce::var errorResult;
    int pluginIndex = 0;
    juce::String presetName;
    if (! requireIntProperty (params, "plugin_index", pluginIndex, errorResult)
        || ! requireStringProperty (params, "preset_name", presetName, errorResult))
        return errorResult;

    if (presetName.isEmpty())
        return makeError ("preset_name is required");

    juce::String errorMessage;
    juce::String targetDescription;
    auto* pluginList = getPluginListForParams (params, errorMessage, &targetDescription);
    if (pluginList == nullptr)
        return makeError (errorMessage);

    auto userPlugins = getAddressablePlugins (*pluginList);

    if (pluginIndex < 0 || pluginIndex >= userPlugins.size())
        return makeError ("Plugin index out of range. " + targetDescription + " has "
                          + juce::String (userPlugins.size()) + " user plugins.");

    auto plugin = userPlugins[pluginIndex];
    if (plugin == nullptr)
        return makeError ("Plugin not found");

    if (! presetManager->loadPreset (*plugin, presetName))
        return makeError ("Failed to load preset");

    edit.markAsChanged();

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("preset_name", presetName);
    const bool useMaster = params.hasProperty ("master")
                             && params["master"].isBool()
                             && static_cast<bool> (params["master"]);
    if (useMaster)
        result->setProperty ("master", true);
    else if (params.hasProperty ("track_index"))
        result->setProperty ("track_index", static_cast<int> (params["track_index"]));
    return juce::var (result);
}

juce::var CommandHandler::handleAddFolderTrack (const juce::var& params)
{
    juce::String name = "Folder";
    juce::var errorResult;
    if (params.hasProperty ("name")
        && ! requireStringProperty (params, "name", name, errorResult))
        return errorResult;

    auto folderTrackPtr = edit.insertNewFolderTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr, false);
    auto* folderTrack = folderTrackPtr.get();
    if (folderTrack == nullptr)
        return makeError ("Failed to create folder track");

    folderTrack->setName (name);

    // Find the track index
    int trackIndex = 0;
    for (auto* t : edit.getTrackList())
    {
        if (dynamic_cast<te::AudioTrack*> (t) || dynamic_cast<te::FolderTrack*> (t))
        {
            if (t == folderTrack)
                break;
            ++trackIndex;
        }
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_index", trackIndex);
        obj->setProperty ("name", name);
    }
    return result;
}

juce::var CommandHandler::handleMoveTrackToFolder (const juce::var& params)
{
    juce::var errorResult;
    int trackIndex = 0;
    int folderIndex = 0;
    if (! requireIntProperty (params, "track_index", trackIndex, errorResult)
        || ! requireIntProperty (params, "folder_index", folderIndex, errorResult))
        return errorResult;

    // Find tracks by index
    te::Track* track = nullptr;
    te::Track* folder = nullptr;
    int currentId = 0;
    for (auto* t : edit.getTrackList())
    {
        if (dynamic_cast<te::AudioTrack*> (t) || dynamic_cast<te::FolderTrack*> (t))
        {
            if (currentId == trackIndex)
                track = t;
            if (currentId == folderIndex)
                folder = t;
            ++currentId;
        }
    }

    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackIndex));
    if (folder == nullptr)
        return makeError ("Folder not found: " + juce::String (folderIndex));

    auto* folderTrack = dynamic_cast<te::FolderTrack*> (folder);
    if (folderTrack == nullptr)
        return makeError ("Target track is not a folder: " + juce::String (folderIndex));
    if (wouldCreateFolderCycle (*track, *folderTrack))
        return makeError ("Invalid folder move: destination is the same track or inside its subtree");

    // Move track into folder using Edit::moveTrack
    edit.moveTrack (track, te::TrackInsertPoint (folderTrack, nullptr));

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_index", trackIndex);
        obj->setProperty ("folder_index", folderIndex);
    }
    return result;
}

juce::var CommandHandler::handleRemoveFromFolder (const juce::var& params)
{
    juce::var errorResult;
    int trackIndex = 0;
    if (! requireIntProperty (params, "track_index", trackIndex, errorResult))
        return errorResult;

    // Find track by index
    te::Track* track = nullptr;
    int currentId = 0;
    for (auto* t : edit.getTrackList())
    {
        if (dynamic_cast<te::AudioTrack*> (t) || dynamic_cast<te::FolderTrack*> (t))
        {
            if (currentId == trackIndex)
            {
                track = t;
                break;
            }
            ++currentId;
        }
    }

    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackIndex));

    auto* parentFolder = track->getParentFolderTrack();
    if (parentFolder == nullptr)
        return makeError ("Track is not in a folder");

    // Move to top level (insert after the parent folder at top level)
    edit.moveTrack (track, te::TrackInsertPoint (nullptr, parentFolder));

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("track_index", trackIndex);
    return result;
}

juce::var CommandHandler::handleCollectAndSave()
{
    auto editFile = resolveProjectFile();
    if (editFile == juce::File())
        return makeError ("Edit file not saved - cannot determine project directory");

    auto projectDir = editFile.getParentDirectory();
    auto result = waive::ProjectPackager::collectAndSave (edit, projectDir, editFile);

    auto response = result.errors.isEmpty()
                        ? makeOk()
                        : makeError ("Collect and save failed: " + result.errors.joinIntoString ("; "));
    if (auto* obj = response.getDynamicObject())
    {
        obj->setProperty ("files_copied", result.filesCopied);
        obj->setProperty ("bytes_copied", result.bytesCopied);

        if (! result.errors.isEmpty())
        {
            juce::Array<juce::var> errorArray;
            for (const auto& err : result.errors)
                errorArray.add (err);
            obj->setProperty ("errors", errorArray);
        }
    }
    return response;
}

juce::var CommandHandler::handleRemoveUnusedMedia()
{
    auto editFile = resolveProjectFile();
    if (editFile == juce::File())
        return makeError ("Edit file not saved - cannot determine project directory");

    auto projectDir = editFile.getParentDirectory();
    auto removeResult = waive::ProjectPackager::removeUnusedMedia (edit, projectDir);

    auto response = removeResult.errors.isEmpty()
                        ? makeOk()
                        : makeError ("Remove unused media failed: " + removeResult.errors.joinIntoString ("; "));
    if (auto* obj = response.getDynamicObject())
    {
        obj->setProperty ("files_removed", removeResult.filesRemoved);
        obj->setProperty ("bytes_freed", removeResult.bytesFreed);

        if (! removeResult.errors.isEmpty())
        {
            juce::Array<juce::var> errorArray;
            for (const auto& err : removeResult.errors)
                errorArray.add (err);
            obj->setProperty ("errors", errorArray);
        }
    }
    return response;
}

juce::var CommandHandler::handlePackageAsZip (const juce::var& params)
{
    juce::var errorResult;
    juce::String outputPath;
    if (! requireStringProperty (params, "file_path", outputPath, errorResult))
        return errorResult;

    auto projectFile = resolveProjectFile();
    if (projectFile == juce::File())
        return makeError ("Edit file not saved - cannot package project");

    juce::File outputZip (outputPath);

    if (! isOutputPathAllowed (outputPath, outputZip, allowedMediaDirectories))
        return makeError ("Output path is outside allowed directories: " + outputPath);

    auto projectDir = projectFile.getParentDirectory();
    if (areCanonicalPathsEquivalent (outputZip, projectFile)
        || waive::ProjectPackager::isWithinProjectDirectory (outputZip, projectDir))
        return makeError ("Output zip must be outside the project directory");

    auto collectResult = waive::ProjectPackager::collectAndSave (edit, projectDir, projectFile);
    if (! collectResult.errors.isEmpty())
        return makeError ("Failed to collect project media before packaging: "
                          + collectResult.errors.joinIntoString ("; "));

    outputZip.getParentDirectory().createDirectory();
    if (! waive::ProjectPackager::packageAsZip (projectFile, outputZip))
        return makeError ("Failed to create zip archive");

    auto response = makeOk();
    if (auto* obj = response.getDynamicObject())
    {
        obj->setProperty ("file_path", outputZip.getFullPathName());
        obj->setProperty ("files_copied", collectResult.filesCopied);
        obj->setProperty ("bytes_copied", collectResult.bytesCopied);
    }
    return response;
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
