# Phase 03: MIDI Piano Roll (Core)

## Objective
Create a PianoRollComponent that opens when double-clicking a MIDI clip in the timeline. The piano roll displays notes as colored rectangles on a pitch/time grid with a keyboard sidebar, and supports basic note editing: create, delete, move, resize, and velocity editing.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Current MIDI Support
- MIDI clips can be inserted via `insert_midi_clip` command (CommandHandler.cpp:291-359)
- `te::MidiClip` holds a `te::MidiList` which contains `te::MidiNote` objects
- No UI for viewing or editing individual MIDI notes
- TimelineComponent shows clips as colored blocks but doesn't drill into note data

### Tracktion Engine MIDI API
```cpp
// Get MIDI data from a clip
te::MidiClip& midiClip = ...;
te::MidiList& midiList = midiClip.getSequence();

// Iterate notes
for (auto* note : midiList.getNotes())
{
    auto startBeat = note->getStartBeat();   // BeatPosition
    auto lengthBeats = note->getLengthBeats(); // BeatDuration
    int pitch = note->getNoteNumber();         // 0-127
    int velocity = note->getVelocity();        // 0-127
}

// Add a note
midiList.addNote (pitch, startBeat, lengthBeats, velocity, 0 /*colour*/, nullptr);

// Remove a note
midiList.removeNote (*note, nullptr);

// Modify a note — use te::MidiNote::setStartAndLength(), setVelocity(), etc.
// These methods take an UndoManager* parameter
```

### Integration Point
The piano roll should be opened from `ClipComponent` (gui/src/ui/ClipComponent.cpp). When a user double-clicks a MIDI clip, the piano roll opens in a panel below the timeline (or as a resizable split view within SessionComponent).

### Theme
Use `getWaivePalette(component)` for all colours — never hardcode. Palette is in WaiveColours.h.
Use `WaiveSpacing` constants for padding/margins.

## Implementation Tasks

### 1. Create PianoRollComponent

Create `gui/src/ui/PianoRollComponent.h` and `gui/src/ui/PianoRollComponent.cpp`.

**Layout:**
```
┌──────────────────────────────────────────────┐
│ Toolbar: [Snap ▼] [Grid ▼] [Zoom +/-]       │
├─────┬────────────────────────────────────────┤
│ C5  │ ████    ████████                       │
│ B4  │                                        │
│ A4  │      ██████                            │
│ G4  │                    ████                │
│ ... │                                        │
├─────┼────────────────────────────────────────┤
│     │ ▂▅▇▅▂▅▇ (velocity bars)               │
└─────┴────────────────────────────────────────┘
```

**Components:**
1. **PianoKeyboardSidebar** — vertical piano keys (C0-C8), 128 rows, black/white key colours. Each key is ~12px tall. Clicking a key previews the note via MIDI output.
2. **NoteGridComponent** — main editing area. Draws horizontal grid lines per pitch, vertical grid lines per beat/bar. Notes are filled rectangles. Background alternates light/dark for white/black keys.
3. **VelocityLane** — bottom strip (~60px tall) showing velocity bars for each note. Draggable.
4. **Toolbar** — snap settings, grid resolution, zoom controls.

**Constructor:**
```cpp
PianoRollComponent (te::MidiClip& clip, EditSession& editSession);
```

**Scrolling:**
- Horizontal: follows time (beats). Use `juce::Viewport` or custom scroll handling.
- Vertical: scroll through pitch range. Default view centers on notes present in the clip.

### 2. Note display

For each note in `clip.getSequence().getNotes()`:
- x = beat-to-pixel conversion of `note->getStartBeat()`
- width = beat-to-pixel conversion of `note->getLengthBeats()`
- y = pitch-to-row conversion (127 - noteNumber) * rowHeight
- height = rowHeight (default 12px)
- Fill colour: use track colour from `getWaivePalette()`
- Border: 1px darker shade

### 3. Note creation

- **Tool: Pencil (default)**
- Click on empty grid → create note at snapped position
- Drag horizontally → set length
- Default velocity: 100
- Wrap in undo transaction: `editSession.beginNewTransaction("Add Note")`
- Call `midiList.addNote(...)` with the EditSession's UndoManager

### 4. Note selection and deletion

- Click a note to select it (highlight border)
- Shift+click for multi-select
- Drag a lasso rectangle to select multiple notes
- Delete key removes selected notes
- Each deletion wrapped in undo transaction

### 5. Note moving

- Drag selected notes to move them (time + pitch)
- Snap to grid when moving
- Wrap in undo transaction: `editSession.beginNewTransaction("Move Notes")`
- Use `note->setStartAndLength()` and change pitch as needed

### 6. Note resizing

- Drag the right edge of a note to resize its length
- Snap end position to grid
- Wrap in undo transaction

### 7. Velocity editing

- Bottom lane shows a vertical bar for each note
- Bar height = velocity / 127 * laneHeight
- Drag bar top to change velocity
- Use `note->setVelocity()` with undo

### 8. Open from timeline

In `ClipComponent` (gui/src/ui/ClipComponent.cpp), detect double-click on a MIDI clip:
```cpp
void ClipComponent::mouseDoubleClick (const juce::MouseEvent&) override
{
    if (auto* midiClip = dynamic_cast<te::MidiClip*> (clip.get()))
    {
        // Notify SessionComponent to open piano roll
        if (auto* session = findParentComponentOfClass<SessionComponent>())
            session->openPianoRoll (*midiClip);
    }
}
```

In `SessionComponent`, add a method `openPianoRoll(te::MidiClip&)` that:
- Creates a PianoRollComponent in a resizable bottom panel (similar to how the mixer/console work)
- Or replaces the current bottom panel content
- Closing the piano roll returns to the previous view

### 9. Add to CMakeLists.txt

Add all new files to `gui/CMakeLists.txt`.

## Files Expected To Change
- `gui/src/ui/PianoRollComponent.h` (create)
- `gui/src/ui/PianoRollComponent.cpp` (create)
- `gui/src/ui/ClipComponent.cpp` (add double-click handler for MIDI clips)
- `gui/src/ui/SessionComponent.h` (add openPianoRoll method and bottom panel)
- `gui/src/ui/SessionComponent.cpp` (implement piano roll panel)
- `gui/CMakeLists.txt` (add new files)

## Validation

```bash
# Build must succeed
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# All tests must still pass
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- PianoRollComponent compiles and displays notes from a MIDI clip
- Notes can be created, deleted, moved, and resized
- Velocity editing works in bottom lane
- Double-clicking a MIDI clip in timeline opens the piano roll
- Undo/redo works for all note operations
- All existing tests still pass
