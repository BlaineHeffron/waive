#include "AiToolSchema.h"
#include "ToolRegistry.h"
#include "Tool.h"

namespace waive
{

static juce::var makeSchema (const juce::String& type,
                             std::initializer_list<std::pair<juce::String, juce::var>> properties = {},
                             std::initializer_list<juce::String> required = {})
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("type", type);

    if (properties.size() > 0)
    {
        auto* props = new juce::DynamicObject();
        for (auto& [name, schema] : properties)
            props->setProperty (name, schema);
        obj->setProperty ("properties", juce::var (props));
    }

    if (required.size() > 0)
    {
        juce::Array<juce::var> reqArr;
        for (auto& r : required)
            reqArr.add (r);
        obj->setProperty ("required", reqArr);
    }

    return juce::var (obj);
}

static juce::var prop (const juce::String& type, const juce::String& description)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("type", type);
    obj->setProperty ("description", description);
    return juce::var (obj);
}

std::vector<AiToolDefinition> generateToolDefinitions (const ToolRegistry& registry)
{
    std::vector<AiToolDefinition> defs;

    for (auto& tool : registry.getTools())
    {
        auto desc = tool->describe();
        AiToolDefinition def;
        def.name = "tool_" + desc.name;
        def.description = desc.description;
        def.inputSchema = desc.inputSchema;
        defs.push_back (std::move (def));
    }

    return defs;
}

std::vector<AiToolDefinition> generateCommandDefinitions()
{
    std::vector<AiToolDefinition> defs;

    // get_tracks
    defs.push_back ({ "cmd_get_tracks",
                      "Get all audio tracks with their clips, names, and indices.",
                      makeSchema ("object") });

    // add_track
    defs.push_back ({ "cmd_add_track",
                      "Add a new empty audio track to the session.",
                      makeSchema ("object") });

    // remove_track
    defs.push_back ({ "cmd_remove_track",
                      "Remove an audio track by its track_id (0-based index).",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index to remove") } },
                                  { "track_id" }) });

    // set_track_volume
    defs.push_back ({ "cmd_set_track_volume",
                      "Set the volume of a track in dB. Typical range: -60 to +6.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "value_db", prop ("number", "Volume in decibels") } },
                                  { "track_id", "value_db" }) });

    // set_track_pan
    defs.push_back ({ "cmd_set_track_pan",
                      "Set the pan position of a track. Range: -1.0 (left) to 1.0 (right).",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "value", prop ("number", "Pan position from -1.0 to 1.0") } },
                                  { "track_id", "value" }) });

    // insert_audio_clip
    defs.push_back ({ "cmd_insert_audio_clip",
                      "Insert an audio file as a clip on a track at a given time.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "file_path", prop ("string", "Absolute path to the audio file") },
                                    { "start_time", prop ("number", "Start time in seconds") } },
                                  { "track_id", "file_path", "start_time" }) });

    // transport_play
    defs.push_back ({ "cmd_transport_play",
                      "Start playback.",
                      makeSchema ("object") });

    // transport_stop
    defs.push_back ({ "cmd_transport_stop",
                      "Stop playback.",
                      makeSchema ("object") });

    // transport_seek
    defs.push_back ({ "cmd_transport_seek",
                      "Seek the transport to a specific position in seconds.",
                      makeSchema ("object",
                                  { { "position", prop ("number", "Position in seconds") } },
                                  { "position" }) });

    // list_plugins
    defs.push_back ({ "cmd_list_plugins",
                      "List all available audio plugins.",
                      makeSchema ("object") });

    // load_plugin
    defs.push_back ({ "cmd_load_plugin",
                      "Load a plugin onto a track by name or identifier.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "plugin_id", prop ("string", "Plugin name or identifier") } },
                                  { "track_id", "plugin_id" }) });

    // set_parameter
    defs.push_back ({ "cmd_set_parameter",
                      "Set a plugin parameter value (0.0 to 1.0 normalized).",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "plugin_id", prop ("string", "Plugin name") },
                                    { "param_id", prop ("string", "Parameter ID") },
                                    { "value", prop ("number", "Normalized value 0.0-1.0") } },
                                  { "track_id", "plugin_id", "param_id", "value" }) });

    // get_edit_state
    defs.push_back ({ "cmd_get_edit_state",
                      "Get the full edit state as XML (verbose, use sparingly).",
                      makeSchema ("object") });

    // arm_track
    defs.push_back ({ "cmd_arm_track",
                      "Arm or disarm a track for recording, optionally assigning an input device.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "armed", prop ("boolean", "true to arm, false to disarm") },
                                    { "input_device", prop ("string", "Optional: wave input device name to assign") } },
                                  { "track_id", "armed" }) });

    // record_from_mic
    defs.push_back ({ "cmd_record_from_mic",
                      "Quick-record from microphone: creates or finds an empty track, arms it with the default input, and starts recording.",
                      makeSchema ("object") });

    return defs;
}

std::vector<AiToolDefinition> generateAllDefinitions (const ToolRegistry& registry)
{
    auto defs = generateCommandDefinitions();
    auto toolDefs = generateToolDefinitions (registry);
    defs.insert (defs.end(), toolDefs.begin(), toolDefs.end());
    return defs;
}

juce::String generateSystemPrompt()
{
    return "You are Waive AI, an intelligent assistant for the Waive digital audio workstation.\n\n"
           "You help users with music production tasks by using the available tools. "
           "You can query track information, add/remove tracks, set volumes and pans, "
           "insert audio clips, control transport (play/stop/seek), manage plugins, "
           "and run advanced audio tools like normalization, stem separation, and auto-mix.\n\n"
           "Guidelines:\n"
           "- Be concise and helpful.\n"
           "- When the user asks to do something, use the appropriate tool/command.\n"
           "- After executing a command, briefly confirm what was done.\n"
           "- Tool names prefixed with 'cmd_' are direct DAW commands.\n"
           "- Tool names prefixed with 'tool_' are higher-level audio processing tools.\n"
           "- Track IDs are 0-based indices.\n"
           "- Volume is in decibels (dB). 0 dB is unity gain.\n"
           "- Pan ranges from -1.0 (full left) to 1.0 (full right).\n"
           "- When asked about the project state, use cmd_get_tracks first.\n"
           "- Do not invent file paths. Ask the user for paths if needed.\n"
           "- tool_* tools run a Plan/Apply workflow: they analyze the current selection, preview changes, then apply them.\n"
           "- Available tool_* tools include audio processing (normalize, stem separation, gain staging, etc.).\n"
           "- If external tools are installed, additional tool_* commands may be available (e.g., timbre transfer, music generation).\n"
           "- When using tool_* commands, the tool operates on the currently selected clips/tracks.\n";
}

} // namespace waive
