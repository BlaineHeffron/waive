# Waive Architecture

## System Layers

### Layer 1: C++ Engine (Real-Time)

**Stack**: JUCE 7 + Tracktion Engine

Responsibilities:
- Audio I/O and real-time processing
- VST3/AU plugin hosting and parameter control
- Tracktion Engine `Edit` management (the session/project model)
- TCP command server (port 9090) accepting JSON commands
- Transport control (play, stop, seek, loop)

The engine is a headless JUCE console application. It has no GUI — all interaction happens through the command protocol. A GUI layer can be added later as a separate JUCE component that connects to the same Edit.

### Layer 2: Python AI ("Headless Producer")

**Stack**: Python 3.10+, PyTorch, Demucs, Transformers

Responsibilities:
- AI inference (MIDI generation, arrangement suggestions)
- Stem separation (via Demucs or similar)
- Audio analysis and feature extraction
- Sending structured commands to the C++ engine
- Maintaining session context for multi-step AI workflows

The Python layer never touches audio I/O directly. It generates files (WAV, MIDI) and sends commands to the engine to load them.

## Communication Protocol

```
Python Client                          C++ Engine
    │                                       │
    │──── TCP connect (port 9090) ─────────►│
    │                                       │
    │──── { "action": "ping" } ────────────►│
    │◄─── { "status": "ok" } ──────────────│
    │                                       │
    │──── { "action": "set_track_volume",  ►│
    │       "track_id": 0,                  │
    │       "value_db": -6.0 }              │
    │◄─── { "status": "ok",               ─│
    │       "track_id": 0,                  │
    │       "volume_db": -6.0 }             │
    │                                       │
    │──── { "action": "get_edit_state" } ──►│
    │◄─── { "status": "ok",               ─│
    │       "edit_xml": "<EDIT>..." }       │
    │                                       │
```

### Wire Format

Uses JUCE `InterprocessConnection` framing:
- 4 bytes: magic number (0x0000AD10)
- 4 bytes: payload length (big-endian uint32)
- N bytes: JSON payload (UTF-8)

## Tracktion Engine Object Model

```
te::Engine
  └── te::Edit (session — backed by juce::ValueTree / XML)
        ├── te::TempoSequence
        ├── te::AudioTrack[]
        │     ├── te::Clip[] (WaveAudioClip, MidiClip, StepClip)
        │     └── te::PluginList
        │           ├── te::VolumeAndPanPlugin (auto-inserted)
        │           └── te::ExternalPlugin[] (VST3/AU)
        │                 └── te::AutomatableParameter[]
        └── te::TransportControl
```

### Key Patterns

- **Everything is a ValueTree**: The entire Edit state is serializable to XML. This is the key enabler for AI analysis — the AI can request the full state, reason about it, and send targeted mutations.
- **VolumeAndPanPlugin**: Every AudioTrack has one. It exposes `volParam` and `panParam` directly.
- **AutomatableParameter::setParameter()**: All parameter changes go through this method with `juce::sendNotification` to ensure the engine reacts.

## Future Expansion

### Phase 2: GUI Layer
- Separate JUCE GUI app connecting to the same Edit
- Real-time waveform display, mixer, plugin editor hosting
- Could also be a web-based UI (React) connecting via WebSocket

### Phase 3: AI Workflows
- Multi-step arrangement generation
- Style transfer between tracks
- Intelligent mixing suggestions
- Vocal isolation and re-synthesis

### Phase 4: Collaboration
- Multi-user Edit synchronization via ValueTree diffs
- Cloud-based AI inference option (opt-in, not default)
