# Waive Architecture

## System Layers

### Layer 1: C++ App (Real-Time + UI)

**Stack**: JUCE 7 + Tracktion Engine

Responsibilities:
- Interactive DAW UI (timeline, mixer, plugin UI hosting)
- Audio I/O and real-time processing
- VST3/AU plugin hosting and parameter control
- Tracktion Engine `Edit` management (the session/project model)
- Transport control (play, stop, seek)

Waive runs the GUI and engine **in-process** so users can see edits applied in real time, open plugin editors, and manually adjust what tools do.

### Layer 2: Tools (In-Process)

Tools run in-process and can apply edits to the active `Edit`. This keeps the editing experience fully interactive and avoids proxying requests across process boundaries.

Responsibilities:
- AI inference (arrangement, mixing suggestions, MIDI generation)
- Offline DSP / file-based transforms (stems, alignment, time-stretch renders)
- Clip/track mutations (inserts, trims, fades, automation)
- Project-wide operations (render/export, analysis)

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

- **Everything is a ValueTree**: The entire Edit state is serializable to XML. This enables tooling and undo/redo snapshots.
- **VolumeAndPanPlugin**: Every AudioTrack has one. It exposes `volParam` and `panParam` directly.
- **AutomatableParameter::setParameter()**: All parameter changes go through this method with `juce::sendNotification` to ensure the engine reacts.

## Future Expansion

### Phase 2: AI Workflows
- Multi-step arrangement generation
- Style transfer between tracks
- Intelligent mixing suggestions
- Vocal isolation and re-synthesis

### Phase 4: Collaboration
- Multi-user Edit synchronization via ValueTree diffs
- Cloud-based AI inference option (opt-in, not default)
