# GUI Plan (Draft)

For implementation sequencing and agent handoff tickets, see `docs/phased_implementation_plan.md`.

## Goals
- Provide a fully interactive DAW-style interface (in-process JUCE) that users can operate directly.
- Make engine state visible and controllable (tracks, clips, transport, plugins).
- Add AI/tooling surfaces that are previewable, reversible, and transparent.
- Keep a developer console to inspect and send raw JSON commands (in-process).

## Primary Screens
1. Session
2. Library
3. Console
4. Tools (AI / Automation)

## Session Screen
### Transport / Timeline
- Play / Stop / Seek
- Time display (seconds + bars)
- Tempo + time signature controls
- Loop in / out
- Zoom + snap

### Track List
- Add / remove audio and MIDI tracks
- Track name
- Solo / Mute / Arm
- Volume + Pan
- Track color

### Clip View
- Drag / drop audio or MIDI files
- Clip trim + fade in/out
- Clip gain
- Properties panel (start, duration, file path)

## Mixer
- Faders + meters
- Pan
- Insert slots
- Basic parameter list

## Plugin Manager
- Scan + list plugins
- Filter by vendor / format
- Insert plugin on track
- Open plugin editor UI (in-process)

## Library
- File browser
- Recent + favorites
- Drag to timeline / track

## AI / Automation Pane
- Trigger actions (arrange, generate, stems, auto-mix)
- Preview “edit diff” before apply (what tracks/clips/params change)
- One-click undo / snapshot restore
- Job status + logs + artifacts (rendered audio/MIDI)

## Console
- JSON request builder
- Request / response log

## MVP Scope (First Milestone)
1. Session screen with transport + track list + basic clip view.
2. Basic mixer per track (volume + pan) wired to existing commands.
3. Console panel for raw JSON and logs.

## Open Questions
- Which editing primitives must be native first (trim/fades/warp/automation)?
- What “diff + undo” UX should tools use by default?
- Audio/MIDI routing UI depth for MVP?
