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

## Tool Sidebar

The Tool Sidebar (`ToolSidebarComponent`) is a collapsible right panel inside `SessionComponent`. It replaces the previous standalone Tools tab with an integrated experience:

- **Tool selector**: ComboBox populated from `ToolRegistry`
- **Schema-driven parameter UI**: `SchemaFormComponent` reads each tool's `inputSchema` and generates appropriate controls (sliders for numbers with min/max, toggles for booleans, combo boxes for enums, text editors as fallback)
- **Plan/Apply/Reject workflow**: Tools produce a preview diff before applying. Users can review, reject, or apply changes — all undoable.
- **Toggle**: Cmd+T or View menu toggles sidebar visibility. Default width is 280px with a resizer bar.

## Theme System

Waive uses a centralized theme (`gui/src/theme/`) instead of hardcoded colours:

- **`WaiveColours`**: Semantic `ColourPalette` struct with roles like `windowBg`, `primary`, `clipDefault`, `waveform`, `playhead`, etc. `makeDarkPalette()` provides the default dark theme.
- **`WaiveFonts`**: Static font hierarchy — header(16), subheader(13 bold), body(13), label(12), caption(11), mono(12), meter(10).
- **`WaiveLookAndFeel`**: Extends `LookAndFeel_V4`. Maps palette colours to JUCE ColourIds via `applyPalette()`. Provides custom draw methods for buttons (rounded rect), sliders (filled track + circle thumb / arc rotary), tabs (underline active), progress bars, combo boxes, and text editors.

Components access palette colours via `getWaivePalette(component)` with fallback defaults.

## CI/CD

GitHub Actions workflow (`.github/workflows/ci.yml`) runs on every push and PR:
- **build-and-test** job: Ubuntu 22.04, installs system deps, cmake build, `xvfb-run ctest`
- **python-tests** job: advisory (continue-on-error), runs `pytest ai/tests/`
- Build caching via `actions/cache` on the `build/` directory
- Concurrency group with cancel-in-progress for rapid pushes

## Future Expansion

### Phase 2: AI Workflows
- Multi-step arrangement generation
- Style transfer between tracks
- Intelligent mixing suggestions
- Vocal isolation and re-synthesis

### Phase 4: Collaboration
- Multi-user Edit synchronization via ValueTree diffs
- Cloud-based AI inference option (opt-in, not default)
