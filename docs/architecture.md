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

### Safety Architecture

Waive enforces strict safety rules to prevent crashes and data corruption in async tool workflows:

- **ID-based selection state**: Timeline selection and tool preview highlighting use `te::EditItemID` (persistent identifiers) instead of raw `te::Clip*` pointers. This prevents dangling pointers when the `Edit` is swapped (new project, open project) or clips are deleted while a tool job is running.
- **Async callback safety**: Tool jobs run on background threads via `JobQueue`. Callbacks that touch UI or Edit state use weak references or validity checks to ensure the component/edit still exists. Never capture bare `this` in async lambdas; use `juce::Component::SafePointer` or explicit validity flags.
- **Transaction rollback on exception**: All mutations through `EditSession::performEdit()` are exception-safe. If a mutation lambda throws, the undo transaction is aborted and the edit state remains unchanged. This prevents partial edits from corrupting the session.

### Security Architecture

Waive implements defense-in-depth security controls to prevent path traversal, injection, and malicious file access:

- **Path Sanitization**: All file path components from external input (user-provided model IDs, tool artifact names, library file names) must pass through `sanitizePathComponent()` before filesystem operations. This utility rejects `../`, `..\\`, null bytes, control characters, and embedded slashes, preventing directory traversal attacks.
  - Usage: `ModelManager` applies `sanitizePathComponent()` to `modelID` and `version` parameters before constructing filesystem paths in `getModelDirectory()`.
  - Usage: Tool artifact storage paths are sanitized before writing plan outputs or cached analysis results.
- **Command Handler Input Validation**: The `CommandHandler` (engine/src/) validates all file paths in commands before passing them to Tracktion Engine APIs. File paths must be absolute, canonical, and within allowed directories (user's home, project directory, model cache). Relative paths and symlinks pointing outside allowed directories are rejected.
- **Model Storage Isolation**: The `ModelManager` enforces strict storage directory boundaries. Models are installed only within the user-configured model storage directory (default: `~/.waive/models/`). Quota enforcement prevents disk exhaustion attacks via oversized model downloads.

### Performance Architecture

Waive uses several performance optimizations to scale to large projects:

- **ClipTrackIndexMap** (`gui/src/tools/ClipTrackIndexMap.h`): O(1) clip-to-track index lookup via `std::unordered_map<te::EditItemID, int>`. Built once per edit snapshot before multi-clip tools run. Replaces O(n·m) nested track/clip iteration with O(n+m) precomputation + O(1) lookups.
- **AudioAnalysisCache** (`gui/src/tools/AudioAnalysisCache.h`): Deduplicates repeated `analyseAudioFile()` calls (peak/RMS/transient detection) when tools analyze the same audio file with the same parameters. Key is `{File, thresholdDb, minDurationMs}`. Cache hit avoids re-reading the audio file and re-running expensive DSP.
- **Repaint throttling via timer coalescing**: Timeline and mixer components use `juce::Timer` with 30–60 ms intervals to batch repaint requests. This prevents UI stalls when tools update many clips/tracks rapidly. See `TimelineComponent::timerCallback()` and `MixerChannelStrip::timerCallback()`.

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

### Auto-Save Architecture

The `AutoSaveManager` (if present) provides periodic automatic project saving to prevent data loss:

- **Save interval**: Default 5 minutes, configurable via settings.
- **Dirty state detection**: Monitors `Edit::hasChanged()` flag before triggering saves.
- **Auto-save file naming**: Saves to `.waive-autosave-{projectName}.tracktionedit` in the project directory.
- **Recovery flow**: On next launch, if auto-save file is newer than the project file, prompt user to recover unsaved changes.
- **Cleanup**: Auto-save files are deleted on successful explicit save or clean exit.

### Clip Fade Handles

Clip fade-in and fade-out handles (`ClipComponent`) enable graphical fade envelope editing:

- **Handle rendering**: Fade handles are drawn as draggable control points at clip start (fade-in) and clip end (fade-out).
- **Interaction model**:
  - Mouse hover: handle highlights to indicate drag affordance.
  - Drag: mouse drag adjusts fade duration (clamped to clip length).
  - Undo: fade changes are undoable via `EditSession::performEdit()`.
- **Fade curve**: Default fade curve is linear. Future expansion: exponential/logarithmic fade curves.

### Track Color Assignment

Track color assignment (`TrackLaneComponent`) provides deterministic, visually distinct colors for each track:

- **Deterministic assignment**: Track color is derived from track index modulo palette size. Same track order always produces same colors.
- **Palette**: `WaiveColours::makeDarkPalette()` provides a set of high-contrast, perceptually distinct colors for track lanes.
- **Collision avoidance**: Color assignment skips palette indices that are too similar to the background or other semantic colors (playhead, waveform, selection).

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
