# Command Protocol

Commands are JSON objects sent over TCP (default port 9090).

## Wire Format

Uses JUCE `InterprocessConnection` framing:

```
[4 bytes: magic (0x0000AD10)] [4 bytes: payload length (little-endian)] [JSON payload (UTF-8)]
```

## Core Commands

| Action              | Required Fields                          | Description                           |
|---------------------|------------------------------------------|---------------------------------------|
| `ping`              | —                                        | Health check                          |
| `get_edit_state`    | —                                        | Returns full Edit XML as JSON         |
| `get_tracks`        | —                                        | List all tracks and their clips       |
| `add_track`         | —                                        | Add a new audio track                 |
| `remove_track`      | `track_id`                               | Remove a track by index               |
| `insert_audio_clip` | `track_id`, `file_path`                  | Insert a WAV/FLAC clip onto a track   |
| `insert_midi_clip`  | `track_id`, `file_path`                  | Insert a MIDI clip onto a track       |
| `set_track_volume`  | `track_id`, `value_db`                   | Set track volume (dB)                 |
| `set_track_pan`     | `track_id`, `value`                      | Set track pan (-1.0 to 1.0)           |
| `set_parameter`     | `track_id`, `plugin_id`, `param_id`, `value` | Set a plugin parameter value      |
| `load_plugin`       | `track_id`, `plugin_id`                  | Load a VST3 plugin onto a track       |
| `list_plugins`      | —                                        | List available VST3 plugins           |
| `transport_play`    | —                                        | Start playback                        |
| `transport_stop`    | —                                        | Stop playback                         |
| `transport_seek`    | `position`                               | Seek to position (seconds)            |

## Example Request/Response

```json
// Request
{ "action": "set_track_volume", "track_id": 0, "value_db": -6.0 }

// Response
{ "status": "ok", "track_id": 0, "volume_db": -6.0 }
```

```json
// Request
{ "action": "insert_audio_clip", "track_id": 2, "start_time": 90.0, "file_path": "/path/to/ai_gen_drums.wav" }

// Response
{ "status": "ok", "clip_name": "ai_gen_drums", "track_id": 2, "start_time": 90.0, "duration": 4.5 }
```

```json
// Request — Insert MIDI clip
{ "action": "insert_midi_clip", "track_id": 1, "file_path": "/path/to/groove.mid", "start_time": 0.0 }

// Response
{ "status": "ok", "track_id": 1, "start_time": 0.0, "duration": 8.0, "clip_name": "groove" }
```

```json
// Request — Load a plugin
{ "action": "load_plugin", "track_id": 0, "plugin_id": "TDR Nova" }

// Response
{ "status": "ok", "track_id": 0, "plugin_id": "TDR Nova", "plugin_name": "TDR Nova" }
```

```json
// Request — Set a plugin parameter
{ "action": "set_parameter", "track_id": 0, "plugin_id": "TDR Nova", "param_id": "band1_gain", "value": 3.5 }

// Response
{ "status": "ok", "track_id": 0, "plugin_id": "TDR Nova", "param_id": "band1_gain", "value": 3.5 }
```

```json
// Request — List available plugins
{ "action": "list_plugins" }

// Response
{ "status": "ok", "count": 2, "plugins": [
    { "name": "TDR Nova", "identifier": "/path/to/TDR Nova.vst3", "category": "EQ", "manufacturer": "TDR", "format": "VST3", "uid": "12345" },
    { "name": "Valhalla Room", "identifier": "/path/to/ValhallaRoom.vst3", "category": "Reverb", "manufacturer": "Valhalla DSP", "format": "VST3", "uid": "67890" }
  ]
}
```

## Error Response Format

When a command fails or references an unknown action, the engine returns an error response:

```json
// Request with invalid action
{ "action": "unknown_command" }

// Error response
{ "status": "error", "message": "Unknown action: unknown_command" }
```

All error responses include:
- `status`: Always `"error"`
- `message`: Human-readable error message describing what went wrong

The C++ implementation uses `CommandHandler::makeError(message)` to generate these responses.

## Formal Schema

See [`command_schema.json`](command_schema.json) for the JSON Schema definition with conditional required fields.
