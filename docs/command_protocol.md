# Command Protocol

Commands are JSON objects sent over TCP (default port 9090).

## Wire Format

Uses JUCE `InterprocessConnection` framing:

```
[4 bytes: magic (0x0000AD10)] [4 bytes: payload length (big-endian)] [JSON payload (UTF-8)]
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

## Formal Schema

See [`command_schema.json`](command_schema.json) for the JSON Schema definition with conditional required fields.
