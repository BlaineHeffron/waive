# Phase 04: MIDI Piano Roll Polish

## Objective
Add essential polish features to the piano roll: note quantization, snap-to-grid, note duplication, copy/paste, selection tools, and zoom/scroll improvements. Make the piano roll feel like a usable editor, not just a display.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Current State (after Phase 03)
- PianoRollComponent exists with basic note editing (create, delete, move, resize, velocity)
- Double-click MIDI clip opens piano roll in SessionComponent bottom panel
- Undo/redo works for note operations

### Key API
```cpp
te::MidiList& midiList = midiClip.getSequence();
te::MidiNote* note = midiList.getNotes()[i];
note->getStartBeat();      // BeatPosition
note->getLengthBeats();     // BeatDuration
note->getNoteNumber();      // 0-127
note->getVelocity();        // 0-127
note->setStartAndLength (newStart, newLength, undoManager);
note->setVelocity (vel, undoManager);
```

### Theme
Use `getWaivePalette(component)` for colours, `WaiveSpacing` for layout constants.

## Implementation Tasks

### 1. Snap-to-grid system

Add a `SnapSettings` struct to PianoRollComponent:
```cpp
struct SnapSettings
{
    enum GridSize { Bar, Beat, HalfBeat, QuarterBeat, Eighth, Sixteenth, Triplet, Off };
    GridSize gridSize = Beat;
    bool snapEnabled = true;

    double snapBeat (double rawBeat, double beatsPerBar) const;
};
```

Apply snapping to:
- Note creation start position
- Note move destination
- Note resize end position
- The snap function quantizes a beat position to the nearest grid line

### 2. Quantize selected notes

Add a "Quantize" button to the toolbar (or Cmd+Q shortcut):
- Quantize selected notes' start positions to the current grid size
- Optionally quantize note lengths (checkbox)
- Wrap in single undo transaction: "Quantize Notes"

### 3. Note duplication

- Cmd+D on selected notes → duplicate all selected notes
- Duplicated notes appear shifted right by one grid step
- Wrap in undo transaction: "Duplicate Notes"

### 4. Copy/paste

- Cmd+C: copy selected notes to an internal clipboard (store as vector of {pitch, startBeat, lengthBeats, velocity} relative to selection start)
- Cmd+V: paste at current playhead position, maintaining relative note positions
- Cmd+X: cut (copy + delete)
- Each paste wrapped in undo transaction

### 5. Selection tools

Improve selection beyond click and shift-click:
- **Lasso select**: drag on empty area to draw selection rectangle
- **Select All**: Cmd+A selects all notes in the clip
- **Select by pitch**: click a piano key on the sidebar to select all notes of that pitch
- **Deselect**: Escape or click empty area

### 6. Zoom and scroll

- **Horizontal zoom**: Cmd+scroll wheel or +/- keys. Zoom centers on mouse cursor position.
- **Vertical zoom**: Cmd+Shift+scroll wheel. Changes row height (8px minimum, 24px maximum).
- **Scroll**: scroll wheel for vertical, shift+scroll for horizontal
- **Zoom to fit**: press F to zoom so all notes are visible

### 7. Visual improvements

- Show note names on notes that are wide enough (e.g., "C4", "A#3")
- Highlight the currently hovered note (lighter shade)
- Show a ghost preview when dragging notes (semi-transparent)
- Draw bar numbers on the top ruler
- Highlight the playhead position as a vertical line
- Selected notes use a brighter color than unselected

### 8. Keyboard shortcuts for piano roll

When PianoRollComponent has focus:
| Key | Action |
|-----|--------|
| Delete / Backspace | Delete selected notes |
| Cmd+D | Duplicate selected |
| Cmd+A | Select all |
| Cmd+C / X / V | Copy / Cut / Paste |
| Cmd+Q | Quantize selected |
| Escape | Deselect all |
| +/- | Zoom in/out horizontal |
| F | Zoom to fit |
| Up/Down arrows | Transpose selected notes ±1 semitone |
| Shift+Up/Down | Transpose selected ±1 octave |

## Files Expected To Change
- `gui/src/ui/PianoRollComponent.h` (add snap, clipboard, selection, zoom)
- `gui/src/ui/PianoRollComponent.cpp` (implement all features)

## Validation

```bash
# Build must succeed
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# All tests must still pass
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- Snap-to-grid works for all note operations
- Quantize selected notes works
- Copy/paste/duplicate notes works with undo
- Lasso selection, select all, select by pitch
- Zoom/scroll is smooth and intuitive
- Note names visible on wide notes
- All existing tests still pass
