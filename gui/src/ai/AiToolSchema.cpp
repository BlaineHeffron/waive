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

        // Categorize tools
        if (desc.name.containsIgnoreCase ("stem") ||
            desc.name.containsIgnoreCase ("mix") ||
            desc.name.containsIgnoreCase ("timbre") ||
            desc.name.containsIgnoreCase ("generation"))
            def.category = "ai";
        else
            def.category = "analysis";

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
                      makeSchema ("object"),
                      "query" });

    // add_track
    defs.push_back ({ "cmd_add_track",
                      "Add a new empty audio track to the session.",
                      makeSchema ("object"),
                      "track" });

    // remove_track
    defs.push_back ({ "cmd_remove_track",
                      "Remove an audio track by its track_id (0-based index).",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index to remove") } },
                                  { "track_id" }),
                      "track" });

    // set_track_volume
    defs.push_back ({ "cmd_set_track_volume",
                      "Set the volume of a track in dB. Typical range: -60 to +6.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "value_db", prop ("number", "Volume in decibels") } },
                                  { "track_id", "value_db" }),
                      "mixing" });

    // set_track_pan
    defs.push_back ({ "cmd_set_track_pan",
                      "Set the pan position of a track. Range: -1.0 (left) to 1.0 (right).",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "value", prop ("number", "Pan position from -1.0 to 1.0") } },
                                  { "track_id", "value" }),
                      "mixing" });

    // insert_audio_clip
    defs.push_back ({ "cmd_insert_audio_clip",
                      "Insert an audio file as a clip on a track at a given time.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "file_path", prop ("string", "Absolute path to the audio file") },
                                    { "start_time", prop ("number", "Start time in seconds") } },
                                  { "track_id", "file_path", "start_time" }),
                      "audio" });

    // transport_play
    defs.push_back ({ "cmd_transport_play",
                      "Start playback.",
                      makeSchema ("object"),
                      "transport" });

    // transport_stop
    defs.push_back ({ "cmd_transport_stop",
                      "Stop playback.",
                      makeSchema ("object"),
                      "transport" });

    // transport_seek
    defs.push_back ({ "cmd_transport_seek",
                      "Seek the transport to a specific position in seconds.",
                      makeSchema ("object",
                                  { { "position", prop ("number", "Position in seconds") } },
                                  { "position" }),
                      "transport" });

    // list_plugins
    defs.push_back ({ "cmd_list_plugins",
                      "List all available audio plugins.",
                      makeSchema ("object"),
                      "query" });

    // load_plugin
    defs.push_back ({ "cmd_load_plugin",
                      "Load a plugin onto a track by name or identifier.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "plugin_id", prop ("string", "Plugin name or identifier") } },
                                  { "track_id", "plugin_id" }),
                      "mixing" });

    // set_parameter
    defs.push_back ({ "cmd_set_parameter",
                      "Set a plugin parameter value (0.0 to 1.0 normalized).",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "plugin_id", prop ("string", "Plugin name") },
                                    { "param_id", prop ("string", "Parameter ID") },
                                    { "value", prop ("number", "Normalized value 0.0-1.0") } },
                                  { "track_id", "plugin_id", "param_id", "value" }),
                      "mixing" });

    // get_edit_state
    defs.push_back ({ "cmd_get_edit_state",
                      "Get the full edit state as XML (verbose, use sparingly).",
                      makeSchema ("object"),
                      "query" });

    // arm_track
    defs.push_back ({ "cmd_arm_track",
                      "Arm or disarm a track for recording, optionally assigning an input device.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "armed", prop ("boolean", "true to arm, false to disarm") },
                                    { "input_device", prop ("string", "Optional: wave input device name to assign") } },
                                  { "track_id", "armed" }),
                      "recording" });

    // record_from_mic
    defs.push_back ({ "cmd_record_from_mic",
                      "Quick-record from microphone: creates or finds an empty track, arms it with the default input, and starts recording.",
                      makeSchema ("object"),
                      "recording" });

    // split_clip
    defs.push_back ({ "cmd_split_clip",
                      "Split a clip at a specific time position, creating two clips.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "clip_index", prop ("integer", "0-based clip index within the track") },
                                    { "position", prop ("number", "Time position in seconds to split at (must be within clip bounds)") } },
                                  { "track_id", "clip_index", "position" }),
                      "clip" });

    // delete_clip
    defs.push_back ({ "cmd_delete_clip",
                      "Delete a clip from a track.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "clip_index", prop ("integer", "0-based clip index within the track") } },
                                  { "track_id", "clip_index" }),
                      "clip" });

    // move_clip
    defs.push_back ({ "cmd_move_clip",
                      "Move a clip to a new start position in seconds.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "clip_index", prop ("integer", "0-based clip index within the track") },
                                    { "new_start", prop ("number", "New start position in seconds") } },
                                  { "track_id", "clip_index", "new_start" }),
                      "clip" });

    // duplicate_clip
    defs.push_back ({ "cmd_duplicate_clip",
                      "Duplicate a clip, placing the copy immediately after the original.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "clip_index", prop ("integer", "0-based clip index within the track") } },
                                  { "track_id", "clip_index" }),
                      "clip" });

    // trim_clip
    defs.push_back ({ "cmd_trim_clip",
                      "Trim a clip by adjusting its start and/or end time. Provide at least one of new_start or new_end.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "clip_index", prop ("integer", "0-based clip index within the track") },
                                    { "new_start", prop ("number", "New start time in seconds (trims from left)") },
                                    { "new_end", prop ("number", "New end time in seconds (trims from right)") } },
                                  { "track_id", "clip_index" }),
                      "clip" });

    // set_clip_gain
    defs.push_back ({ "cmd_set_clip_gain",
                      "Set the gain of an audio clip in dB. Only works on wave/audio clips.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "clip_index", prop ("integer", "0-based clip index within the track") },
                                    { "gain_db", prop ("number", "Gain in decibels (0 = unity, negative = quieter)") } },
                                  { "track_id", "clip_index", "gain_db" }),
                      "clip" });

    // rename_clip
    defs.push_back ({ "cmd_rename_clip",
                      "Rename a clip.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "clip_index", prop ("integer", "0-based clip index within the track") },
                                    { "name", prop ("string", "New name for the clip") } },
                                  { "track_id", "clip_index", "name" }),
                      "clip" });

    // rename_track
    defs.push_back ({ "cmd_rename_track",
                      "Rename an audio track.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "name", prop ("string", "New track name") } },
                                  { "track_id", "name" }),
                      "track" });

    // solo_track
    defs.push_back ({ "cmd_solo_track",
                      "Solo or unsolo a track.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "solo", prop ("boolean", "true to solo, false to unsolo") } },
                                  { "track_id", "solo" }),
                      "track" });

    // mute_track
    defs.push_back ({ "cmd_mute_track",
                      "Mute or unmute a track.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "mute", prop ("boolean", "true to mute, false to unmute") } },
                                  { "track_id", "mute" }),
                      "track" });

    // duplicate_track
    defs.push_back ({ "cmd_duplicate_track",
                      "Duplicate a track including all its clips.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index to duplicate") } },
                                  { "track_id" }),
                      "track" });

    // get_transport_state
    defs.push_back ({ "cmd_get_transport_state",
                      "Get transport state: position, tempo, playing/recording status, loop region, track count.",
                      makeSchema ("object"),
                      "query" });

    // set_tempo
    defs.push_back ({ "cmd_set_tempo",
                      "Set the project tempo in BPM.",
                      makeSchema ("object",
                                  { { "bpm", prop ("number", "Tempo in beats per minute (20-999)") } },
                                  { "bpm" }),
                      "transport" });

    // set_loop_region
    defs.push_back ({ "cmd_set_loop_region",
                      "Enable/disable looping and optionally set loop start/end points.",
                      makeSchema ("object",
                                  { { "enabled", prop ("boolean", "true to enable looping, false to disable") },
                                    { "start", prop ("number", "Loop start in seconds (required when enabling)") },
                                    { "end", prop ("number", "Loop end in seconds (required when enabling)") } },
                                  { "enabled" }),
                      "transport" });

    // export_mixdown
    defs.push_back ({ "cmd_export_mixdown",
                      "Export the full mix as a WAV audio file (format: 44.1kHz/24-bit WAV).",
                      makeSchema ("object",
                                  { { "file_path", prop ("string", "Output file path (must be in allowed directories)") },
                                    { "start", prop ("number", "Start time in seconds (default: 0)") },
                                    { "end", prop ("number", "End time in seconds (default: edit length)") } },
                                  { "file_path" }),
                      "export" });

    // export_stems
    defs.push_back ({ "cmd_export_stems",
                      "Export each track as a separate WAV file into a directory.",
                      makeSchema ("object",
                                  { { "output_dir", prop ("string", "Output directory path") },
                                    { "start", prop ("number", "Start time in seconds (default: 0)") },
                                    { "end", prop ("number", "End time in seconds (default: edit length)") } },
                                  { "output_dir" }),
                      "export" });

    // bounce_track
    defs.push_back ({ "cmd_bounce_track",
                      "Render a track to a single audio file, replacing all clips with the bounced result.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index to bounce") } },
                                  { "track_id" }),
                      "export" });

    // remove_plugin
    defs.push_back ({ "cmd_remove_plugin",
                      "Remove a plugin from a track by its index (0-based among user/external plugins only).",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "plugin_index", prop ("integer", "0-based index among user plugins on the track") } },
                                  { "track_id", "plugin_index" }),
                      "mixing" });

    // bypass_plugin
    defs.push_back ({ "cmd_bypass_plugin",
                      "Bypass or unbypass a plugin on a track.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "plugin_index", prop ("integer", "0-based index among user plugins on the track") },
                                    { "bypassed", prop ("boolean", "true to bypass, false to enable") } },
                                  { "track_id", "plugin_index", "bypassed" }),
                      "mixing" });

    // get_plugin_parameters
    defs.push_back ({ "cmd_get_plugin_parameters",
                      "List all automatable parameters of a plugin on a track, with current values.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "plugin_index", prop ("integer", "0-based index among user plugins on the track") } },
                                  { "track_id", "plugin_index" }),
                      "query" });

    // get_automation_params
    defs.push_back ({ "cmd_get_automation_params",
                      "Get all automatable parameters for a track (volume, pan, plugin params). Returns parameter indices needed for other automation commands.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") } },
                                  { "track_id" }),
                      "query" });

    // get_automation_points
    defs.push_back ({ "cmd_get_automation_points",
                      "Get all automation points for a specific parameter. Use get_automation_params first to find the param_index.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "param_index", prop ("integer", "Parameter index from get_automation_params") } },
                                  { "track_id", "param_index" }),
                      "query" });

    // add_automation_point
    defs.push_back ({ "cmd_add_automation_point",
                      "Add an automation point at a specific time with a normalised value (0.0-1.0). Use get_automation_params to find the param_index.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "param_index", prop ("integer", "Parameter index from get_automation_params") },
                                    { "time", prop ("number", "Time in seconds for the automation point") },
                                    { "value", prop ("number", "Normalised value 0.0-1.0") },
                                    { "curve", prop ("number", "Curve tension (-1.0 to 1.0, 0=linear). Optional.") } },
                                  { "track_id", "param_index", "time", "value" }),
                      "mixing" });

    // remove_automation_point
    defs.push_back ({ "cmd_remove_automation_point",
                      "Remove an automation point by its index. Use get_automation_points to see current points.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "param_index", prop ("integer", "Parameter index") },
                                    { "point_index", prop ("integer", "0-based index of the point to remove") } },
                                  { "track_id", "param_index", "point_index" }),
                      "mixing" });

    // clear_automation
    defs.push_back ({ "cmd_clear_automation",
                      "Remove all automation points for a parameter, resetting it to its current static value.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "param_index", prop ("integer", "Parameter index") } },
                                  { "track_id", "param_index" }),
                      "mixing" });

    // set_clip_fade
    defs.push_back ({ "cmd_set_clip_fade",
                      "Set fade-in and/or fade-out duration on an audio clip. Provide fade_in and/or fade_out in seconds.",
                      makeSchema ("object",
                                  { { "track_id", prop ("integer", "0-based track index") },
                                    { "clip_index", prop ("integer", "0-based clip index within the track") },
                                    { "fade_in", prop ("number", "Fade-in duration in seconds") },
                                    { "fade_out", prop ("number", "Fade-out duration in seconds") } },
                                  { "track_id", "clip_index" }),
                      "mixing" });

    return defs;
}

std::vector<AiToolDefinition> generateAllDefinitions (const ToolRegistry& registry)
{
    auto defs = generateCommandDefinitions();
    auto toolDefs = generateToolDefinitions (registry);
    defs.insert (defs.end(), toolDefs.begin(), toolDefs.end());
    return defs;
}

std::vector<AiToolDefinition> generateCoreDefinitions()
{
    std::vector<AiToolDefinition> core;

    // Always-available query tools
    core.push_back ({ "cmd_get_tracks",
                      "Get all audio tracks with clips, names, indices, solo/mute.",
                      makeSchema ("object"),
                      "query" });

    core.push_back ({ "cmd_get_transport_state",
                      "Get transport state: position, tempo, playing/recording, loop region.",
                      makeSchema ("object"),
                      "query" });

    // The search tool itself
    core.push_back ({ "cmd_search_tools",
                      "Search for available tools and commands by keyword. Returns matching tool schemas that you can then call. "
                      "Use this to discover clip editing, mixing, export, recording, AI, and analysis tools. "
                      "Categories: query, transport, track, clip, mixing, export, recording, audio, analysis, ai.",
                      makeSchema ("object",
                                  { { "query", prop ("string", "Search keywords (e.g. 'split clip', 'export', 'tempo', 'plugin', 'ai stem')") } },
                                  { "query" }),
                      "query" });

    return core;
}

std::vector<AiToolDefinition> searchDefinitions (const ToolRegistry& registry,
                                                  const juce::String& query,
                                                  int maxResults)
{
    auto allDefs = generateAllDefinitions (registry);
    auto queryLower = query.toLowerCase();
    auto keywords = juce::StringArray::fromTokens (queryLower, " ", "");

    // Score each definition
    struct ScoredDef
    {
        AiToolDefinition def;
        int score = 0;
    };

    std::vector<ScoredDef> scored;
    for (auto& d : allDefs)
    {
        // Skip the search tool itself
        if (d.name == "cmd_search_tools")
            continue;

        int score = 0;
        auto nameLower = d.name.toLowerCase();
        auto descLower = d.description.toLowerCase();
        auto catLower = d.category.toLowerCase();

        for (auto& kw : keywords)
        {
            if (nameLower.contains (kw))  score += 3;
            if (catLower == kw)           score += 2;
            if (catLower.contains (kw))   score += 2;
            if (descLower.contains (kw))  score += 1;
        }

        if (score > 0)
            scored.push_back ({ d, score });
    }

    // Sort by score descending
    std::sort (scored.begin(), scored.end(),
               [] (const ScoredDef& a, const ScoredDef& b) { return a.score > b.score; });

    std::vector<AiToolDefinition> results;
    for (int i = 0; i < std::min ((int) scored.size(), maxResults); ++i)
        results.push_back (scored[(size_t) i].def);

    return results;
}

juce::String generateSystemPrompt()
{
    return "You are Waive AI, an intelligent assistant for the Waive digital audio workstation.\n\n"
           "You have a small set of core tools always available:\n"
           "- cmd_get_tracks: Query all tracks and clips\n"
           "- cmd_get_transport_state: Check playback position, tempo, loop settings\n"
           "- cmd_search_tools: Discover more tools by keyword\n\n"
           "When you need to perform an action (editing clips, mixing, exporting, using AI tools, etc.), "
           "first use cmd_search_tools to find the right tool. For example:\n"
           "- To split a clip: search for 'split clip'\n"
           "- To export audio: search for 'export'\n"
           "- To adjust volume: search for 'volume mixing'\n"
           "- To use AI features: search for 'ai' or 'stem separation'\n\n"
           "Tool categories: query, transport, track, clip, mixing, export, recording, audio, analysis, ai.\n\n"
           "Guidelines:\n"
           "- Be concise and helpful.\n"
           "- Track IDs and clip indices are 0-based.\n"
           "- Volume is in decibels (dB). 0 dB is unity gain.\n"
           "- Pan ranges from -1.0 (full left) to 1.0 (full right).\n"
           "- Always query project state before making changes.\n"
           "- Do not invent file paths. Ask the user for paths if needed.\n"
           "- Tool names prefixed with 'cmd_' are direct DAW commands.\n"
           "- Tool names prefixed with 'tool_' are higher-level audio processing tools.\n";
}

} // namespace waive
