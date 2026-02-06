# Waive Phased Implementation Plan (Agent Handoff)

This plan assumes Waive is a single **in-process JUCE GUI application** backed by **Tracktion Engine**. AI/tools apply edits inside the same process so the user can see changes immediately and continue editing manually (no proxying to a separate engine process).

## Non-Negotiables

- **Real-time safe**: no blocking work on the audio thread (no allocations/locks/network/model inference).
- **Undoable**: all edit mutations are undo/redoable and snapshot-friendly.
- **Previewable**: tools propose changes as a diff that can be previewed before apply.
- **Local-first**: everything works offline; optional model downloads must be explicit.
- **Deterministic edits**: tool execution produces repeatable results given the same inputs and versioned models.

## Repo Reality Check (Current State)

- `gui/` builds a minimal JUCE app with:
  - Session tab: transport, add/remove tracks, volume/pan per track
  - Console tab: sends JSON to `CommandHandler` in-process
- `engine/src/CommandHandler.*` implements a small set of JSON “commands” for basic edit mutations.
- Anything Python under `ai/` is legacy experimentation and not part of the core app direction.

## Phase 0 — Foundation (Make future work easy)

**Goal**: define the internal boundaries so multiple agents can work in parallel without stepping on each other.

### 0A. Project structure + naming
**Owner**: agent-foundation

- Create `app/` (or keep `gui/`) conventions:
  - `gui/src/ui/*` for components
  - `gui/src/edit/*` for edit wrappers
  - `gui/src/tools/*` for tool framework
  - `gui/src/util/*` for shared helpers
- Add a `docs/dev_conventions.md` with:
  - threading rules (audio thread vs message thread vs background)
  - where to put long-running jobs
  - UI component conventions

**Done when**
- New directories exist and are used by at least one moved component.
- Documented conventions are referenced by later phase tickets.

### 0B. Threading + job system
**Owner**: agent-runtime

- Implement a single app-wide job queue:
  - background thread pool for analysis/renders
  - message-thread dispatch helpers for UI updates
- Provide structured logging:
  - `ToolLog` panel can subscribe to job events

**Done when**
- A demo “background job” runs and reports progress/cancel in UI.

### 0C. Undo/redo plumbing
**Owner**: agent-edit-core

- Wrap Tracktion’s undo manager usage in one place:
  - `EditSession` owns `te::Edit`, `UndoManager`, and helpers
  - unified mutation entrypoint `performEdit (name, lambda)`

**Done when**
- UI has Undo/Redo menu items that work for existing mutations (add track, set volume, insert clip).

## Phase 1 — DAW MVP (Interactive basics)

**Goal**: users can create a session, import audio, arrange clips, and mix at a basic level.

### 1A. Session persistence
**Owner**: agent-project

- Implement:
  - New/Open/Save/Save As for `.tracktionedit` (or project folder)
  - Recent projects list

**Done when**
- Projects can be reopened with tracks/clips intact.

### 1B. Library / file browser import
**Owner**: agent-library

- Build Library tab:
  - file browser + favorites
  - drag/drop to track lane to insert audio clip at drop time
- Hook to Tracktion clip insertion (not the JSON console).

**Done when**
- Dragging a WAV inserts a clip at the correct time on the correct track.

### 1C. Timeline with waveforms (read-only first)
**Owner**: agent-timeline

- Implement timeline viewport:
  - horizontal zoom
  - playhead
  - clip rectangles aligned to time
- Implement waveform rendering cache:
  - background peak generation
  - incremental drawing

**Done when**
- Imported audio shows a waveform that scrolls/zooms smoothly.

### 1D. Basic clip editing
**Owner**: agent-editing

- Interactions:
  - select, move, duplicate
  - trim left/right
  - split at playhead

**Done when**
- All operations are undoable and reflect in transport playback.

### 1E. Mixer MVP
**Owner**: agent-mixer

- Mixer tab/pane:
  - per-track fader/pan
  - meters (peak/RMS, best-effort)
  - master channel

**Done when**
- Meters respond during playback and controls are automation-ready (see Phase 3).

## Phase 2 — Plugins, Routing, Recording

**Goal**: “real DAW feel”: inserts, basic routing, and recording.

### 2A. Plugin browser + inserts
**Owner**: agent-plugins

- Scan/list plugins (VST3/AU depending on platform)
- Insert chain UI per track
- Bypass/enable and reorder inserts

**Done when**
- User can insert a plugin and hear the effect in playback.

### 2B. Plugin editor hosting
**Owner**: agent-plugins-ui

- Open/close plugin editor windows
- Ensure correct lifetime + message-thread ownership

**Done when**
- Plugin UIs open reliably and don’t crash on close/reopen.

### 2C. Recording + input monitoring
**Owner**: agent-recording

- Audio inputs selection per track
- Arm/monitor/record controls
- Record audio clips onto timeline

**Done when**
- A new recording creates a clip aligned to the timeline and plays back correctly.

### 2D. Routing (minimal)
**Owner**: agent-routing

- Master bus + per-track output
- Sends/returns (one send is enough for first pass)

**Done when**
- A reverb return can be created and fed by sends.

## Phase 3 — Automation + Time

**Goal**: automation lanes and musical time features required for serious work.

### 3A. Tempo/time signature + grid
**Owner**: agent-time

- tempo track UI
- time signature markers
- snap/grid resolution controls

**Done when**
- Clips snap to grid, and the timeline can show bars/beats.

### 3B. Automation lanes
**Owner**: agent-automation

- Add automation lane per track/plugin parameter
- Draw/edit automation points

**Done when**
- Automating a plugin parameter changes playback and is undoable.

### 3C. Loop + punch
**Owner**: agent-transport

- loop in/out UI
- punch in/out for recording

**Done when**
- Loop playback is stable and respects timeline selection.

## Phase 4 — Tool Framework (LLM-callable, but safe)

**Goal**: tools can propose and apply changes safely while the user watches and edits manually.

### 4A. Tool API (in-process)
**Owner**: agent-tools-core

- Define a `Tool` interface:
  - `describe()` (name, version, inputs schema)
  - `plan()` (returns proposed changes)
  - `apply()` (executes an approved plan)
- Add a tool registry and a UI “Tools” tab:
  - run tool with parameters
  - show progress + logs

**Done when**
- A trivial built-in tool (e.g. “Normalize selected clips”) works end-to-end with preview + apply + undo.

### 4B. Edit diff + preview UI
**Owner**: agent-tools-diff

- Represent edit changes as a structured diff:
  - track adds/removes/renames
  - clip inserts/moves/trims/fades
  - parameter changes and automation changes
- Preview UI:
  - highlight affected items in timeline/mixer
  - show a textual summary (“will trim 3 clips, set track 2 to -6 dB…”)

**Done when**
- User can accept/reject a tool plan; reject makes no changes.

### 4C. Sandboxed execution + cancellation
**Owner**: agent-tools-runtime

- Long-running tools must:
  - be cancellable
  - not block UI/audio
  - write artifacts to a project-local cache

**Done when**
- Cancelling a running tool leaves the session consistent.

## Phase 5 — Built-in Tools (Vetted, useful defaults)

**Goal**: ship a small set of high-value, open-source, deterministic tools.

### 5A. “Assistant” tool set (no model downloads)
**Owner**: agent-tools-basic

- “Rename tracks from clips”
- “Gain-stage selected tracks to target peak”
- “Detect silence and cut regions”
- “Align clips by transient”

**Done when**
- All tools run offline and are stable on real projects.

### 5B. Optional model-backed tools (explicit install)
**Owner**: agent-tools-models

- Add a model manager:
  - install/uninstall
  - version pinning
  - storage location + quotas
- Tools:
  - stem separation
  - auto-mix suggestions

**Done when**
- Models are optional; app boots and works without them.

## Cross-Cutting Work (Always-on)

- **Testing**: add unit tests for edit primitives and tool diffs; add integration tests for load/save + timeline edits.
- **Performance**: waveform cache, UI virtualization for large sessions, avoid ValueTree churn on repaint.
- **Crash safety**: autosave + recovery, safe plugin scanning, isolate risky operations.

## Suggested “Agent Tickets” Format

When splitting work across agents, copy/paste a section and fill in:

- **Scope**: what is included/excluded
- **Files**: primary folders/files to touch
- **APIs**: new types/functions to introduce
- **Threading**: what runs where
- **Done Criteria**: observable behavior + undo/redo expectations
- **Tests**: what to add/run

