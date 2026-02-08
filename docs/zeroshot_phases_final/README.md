# Waive Phase Plan (v6)

Generated on 2026-02-07 after completing v5 (20 phases: AI tools, audio processing, automation, tests, benchmarks, fix loop).

## Current State
- All C++ tests pass (WaiveCoreTests, WaiveUiTests, WaiveToolTests)
- All Python tests pass (48 passed, 4 skipped)
- Build clean, no warnings
- 48 commands, 7 C++ tools, 10 Python tools, AI chat with Anthropic/OpenAI/Gemini

## Phase Order (8 phases)

1. **Phase 01: Documentation, CI Hardening & Shortcut Fix** — README, LICENSE, fix Cmd+S conflict, make Python tests blocking, keyboard shortcut reference dialog
2. **Phase 02: Render Dialog & Export Format Support** — RenderDialog with WAV/FLAC/OGG, sample rate/bit depth, normalize, progress bar, stems toggle
3. **Phase 03: MIDI Piano Roll (Core)** — PianoRollComponent with note display, create/delete/move/resize, velocity editing, keyboard sidebar, opens via double-click on MIDI clip
4. **Phase 04: MIDI Piano Roll (Polish)** — Quantize, snap-to-grid, copy/paste/duplicate, lasso selection, zoom/scroll, transpose shortcuts
5. **Phase 05: Plugin Preset Management** — Save/load presets per plugin, preset browser dropdown, presets folder on disk, AI agent preset commands
6. **Phase 06: Track Grouping & VCA Faders** — Folder tracks, move tracks into folders, collapse/expand, group solo/mute, VCA fader, hierarchy in get_tracks
7. **Phase 07: Project Packaging & Media Management** — Collect-and-save, remove unused media, package as zip, AI agent commands
8. **Phase 08: Test Iteration & Fix Loop** — Run all tests, fix regressions from phases 01-07, iterate until green
