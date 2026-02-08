#include "PianoRollComponent.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"
#include "WaiveSpacing.h"
#include "WaiveFonts.h"
#include <cmath>

//==============================================================================
// SnapSettings
//==============================================================================
double SnapSettings::snapBeat (double rawBeat, double beatsPerBar) const
{
    if (! snapEnabled || gridSize == Off)
        return rawBeat;

    double resolution = getBeatsForGridSize (beatsPerBar);
    return std::round (rawBeat / resolution) * resolution;
}

double SnapSettings::getBeatsForGridSize (double beatsPerBar) const
{
    switch (gridSize)
    {
        case Bar:         return beatsPerBar;
        case Beat:        return 1.0;
        case HalfBeat:    return 0.5;
        case QuarterBeat: return 0.25;
        case Eighth:      return 0.125;
        case Sixteenth:   return 0.0625;
        case Triplet:     return 1.0 / 3.0;
        case Off:         return 1.0;
        default:          return 1.0;
    }
}

//==============================================================================
// PianoKeyboardSidebar
//==============================================================================
PianoRollComponent::PianoKeyboardSidebar::PianoKeyboardSidebar()
{
    setSize (width, rowHeight * 128);
}

void PianoRollComponent::PianoKeyboardSidebar::paint (juce::Graphics& g)
{
    auto* pal = waive::getWaivePalette (*this);
    auto bounds = getLocalBounds();

    for (int pitch = 0; pitch < 128; ++pitch)
    {
        int y = (127 - pitch) * rowHeight;
        juce::Rectangle<int> keyRect (0, y, width, rowHeight);

        // Determine if this is a black or white key
        int pitchClass = pitch % 12;
        bool isBlackKey = (pitchClass == 1 || pitchClass == 3 || pitchClass == 6 ||
                          pitchClass == 8 || pitchClass == 10);

        // Background
        g.setColour (isBlackKey
                     ? (pal ? pal->surfaceBg : juce::Colour (0xff2a2a2a))
                     : (pal ? pal->windowBg : juce::Colour (0xff3a3a3a)));
        g.fillRect (keyRect);

        // Border
        g.setColour (pal ? pal->borderSubtle : juce::Colour (0xff555555));
        g.drawRect (keyRect, 1);

        // Draw note name for C notes
        if (pitchClass == 0)
        {
            int octave = (pitch / 12) - 1;
            g.setColour (pal ? pal->textSecondary : juce::Colour (0xffaaaaaa));
            g.setFont (waive::Fonts::caption());
            g.drawText (juce::String ("C") + juce::String (octave), keyRect.reduced (2),
                       juce::Justification::centredLeft, true);
        }
    }
}

void PianoRollComponent::PianoKeyboardSidebar::mouseDown (const juce::MouseEvent& e)
{
    // Select notes by pitch
    int pitch = 127 - (e.y / rowHeight);
    if (pitch >= 0 && pitch < 128)
    {
        auto* parent = dynamic_cast<PianoRollComponent*> (getParentComponent());
        if (parent && parent->noteGrid)
        {
            parent->noteGrid->selectNotesByPitch (pitch, e.mods.isShiftDown());
        }
    }
}

void PianoRollComponent::PianoKeyboardSidebar::setRowHeight (int height)
{
    rowHeight = juce::jlimit (8, 24, height);
    setSize (width, rowHeight * 128);
    repaint();
}

//==============================================================================
// NoteGridComponent
//==============================================================================
PianoRollComponent::NoteGridComponent::NoteGridComponent (te::MidiClip& clip,
                                                          EditSession& session,
                                                          PianoRollComponent& parent)
    : midiClip (clip), editSession (session), pianoRoll (parent)
{
    setSize (2000, pianoRoll.keyboard->getRowHeight() * 128);
    setWantsKeyboardFocus (true);
    rebuildNoteRectangles();
}

PianoRollComponent::NoteGridComponent::~NoteGridComponent() = default;

void PianoRollComponent::NoteGridComponent::paint (juce::Graphics& g)
{
    auto* pal = waive::getWaivePalette (*this);
    auto bounds = getLocalBounds();
    int rowHeight = pianoRoll.keyboard->getRowHeight();

    // Background with alternating rows for black/white keys
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        int y = pitchToY (pitch);
        int pitchClass = pitch % 12;
        bool isBlackKey = (pitchClass == 1 || pitchClass == 3 || pitchClass == 6 ||
                          pitchClass == 8 || pitchClass == 10);

        g.setColour (isBlackKey
                     ? (pal ? pal->surfaceBg : juce::Colour (0xff2a2a2a))
                     : (pal ? pal->windowBg : juce::Colour (0xff3a3a3a)));
        g.fillRect (0, y, getWidth(), rowHeight);
    }

    // Grid lines - horizontal (pitch)
    g.setColour (pal ? pal->gridMinor : juce::Colour (0xff444444));
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        int y = pitchToY (pitch);
        g.drawLine (0.0f, (float) y, (float) getWidth(), (float) y, 0.5f);
    }

    // Grid lines - vertical (beats)
    auto& tempoSeq = midiClip.edit.tempoSequence;
    auto beatRange = tempoSeq.toBeats (midiClip.getPosition().time);
    double clipLengthBeats = beatRange.getLength().inBeats();
    double beatsPerBar = 4.0; // Default 4/4 time

    double gridRes = pianoRoll.snapSettings.getBeatsForGridSize (beatsPerBar);
    for (double beat = 0.0; beat <= clipLengthBeats; beat += gridRes)
    {
        int x = (int) beatToX (beat);
        bool isMajor = (std::fmod (beat, beatsPerBar) < 0.001);
        g.setColour (isMajor
                     ? (pal ? pal->gridMajor : juce::Colour (0xff666666))
                     : (pal ? pal->gridMinor : juce::Colour (0xff444444)));
        g.drawLine ((float) x, 0.0f, (float) x, (float) getHeight(),
                   isMajor ? 1.0f : 0.5f);

        // Draw bar numbers
        if (isMajor && beat > 0.001)
        {
            int barNum = (int) (beat / beatsPerBar) + 1;
            g.setColour (pal ? pal->textSecondary : juce::Colour (0xffaaaaaa));
            g.setFont (waive::Fonts::caption());
            g.drawText (juce::String (barNum), x + 2, 2, 30, 12,
                       juce::Justification::centredLeft, false);
        }
    }

    // Playhead
    auto& playheadPos = midiClip.edit.getTransport().position;
    double playheadBeat = tempoSeq.toBeats (playheadPos.get()).inBeats();
    auto clipStartBeat = beatRange.getStart().inBeats();
    double relativePlayheadBeat = playheadBeat - clipStartBeat;
    if (relativePlayheadBeat >= 0.0 && relativePlayheadBeat <= clipLengthBeats)
    {
        int playheadX = (int) beatToX (relativePlayheadBeat);
        g.setColour (pal ? pal->accent : juce::Colour (0xffff6600));
        g.drawLine ((float) playheadX, 0.0f, (float) playheadX, (float) getHeight(), 2.0f);
    }

    // Ghost preview when dragging
    if (dragMode == DragMode::MoveNotes && ! selectedNotes.empty())
    {
        auto mousePos = getMouseXYRelative();
        double targetBeat = snapBeat (xToBeat (mousePos.x) - noteDragOffset.x);
        int targetPitch = juce::jlimit (0, 127, yToPitch (mousePos.y) - (int) noteDragOffset.y);

        te::MidiNote* firstNote = *selectedNotes.begin();
        double deltaBeat = targetBeat - firstNote->getStartBeat().inBeats();
        int deltaPitch = targetPitch - firstNote->getNoteNumber();

        juce::Colour ghostColour = (pal ? pal->primary : juce::Colour (0xff4477aa)).withAlpha (0.5f);
        for (auto* note : selectedNotes)
        {
            double ghostStart = std::max (0.0, note->getStartBeat().inBeats() + deltaBeat);
            int ghostPitch = juce::jlimit (0, 127, note->getNoteNumber() + deltaPitch);

            juce::Rectangle<float> ghostRect (
                (float) beatToX (ghostStart),
                (float) pitchToY (ghostPitch),
                (float) (note->getLengthBeats().inBeats() * pianoRoll.pixelsPerBeat),
                (float) rowHeight
            );

            g.setColour (ghostColour);
            g.fillRect (ghostRect);
            g.setColour (ghostColour.brighter());
            g.drawRect (ghostRect, 1.0f);
        }
    }

    // Notes
    juce::Colour noteColour = pal ? pal->primary : juce::Colour (0xff4477aa);
    for (const auto& noteRect : noteRectangles)
    {
        bool isSelected = selectedNotes.count (noteRect.note) > 0;
        bool isHovered = (hoveredNote == noteRect.note);

        juce::Colour fillColour = isSelected
                     ? (pal ? pal->clipSelected : juce::Colour (0xff5588bb))
                     : noteColour;

        if (isHovered && ! isSelected)
            fillColour = fillColour.brighter (0.2f);

        g.setColour (fillColour);
        g.fillRect (noteRect.bounds);

        g.setColour (isSelected
                     ? (pal ? pal->selectionBorder : juce::Colour (0xffffffff))
                     : noteColour.darker());
        g.drawRect (noteRect.bounds, isSelected ? 2.0f : 1.0f);

        // Draw note name if wide enough
        if (noteRect.bounds.getWidth() > 30.0f)
        {
            g.setColour (isSelected
                        ? juce::Colours::white
                        : (pal ? pal->textPrimary : juce::Colour (0xffffffff)));
            g.setFont (waive::Fonts::caption());
            g.drawText (pitchToNoteName (noteRect.note->getNoteNumber()),
                       noteRect.bounds.reduced (2.0f),
                       juce::Justification::centredLeft, true);
        }
    }

    // Lasso rectangle
    if (dragMode == DragMode::SelectLasso)
    {
        g.setColour (pal ? pal->selection.withAlpha (0.3f) : juce::Colour (0x4d4477aa));
        g.fillRect (lassoRect);
        g.setColour (pal ? pal->selectionBorder : juce::Colour (0xff4477aa));
        g.drawRect (lassoRect, 2);
    }

    // Create note preview
    if (dragMode == DragMode::CreateNote)
    {
        double endBeat = xToBeat (getMouseXYRelative().x);
        double snappedEnd = snapBeat (endBeat);
        double gridSize = pianoRoll.snapSettings.getBeatsForGridSize (beatsPerBar);
        double length = std::max (gridSize, snappedEnd - createNoteStartBeat);

        juce::Rectangle<float> previewRect (
            (float) beatToX (createNoteStartBeat),
            (float) pitchToY (createNotePitch),
            (float) (length * pianoRoll.pixelsPerBeat),
            (float) rowHeight
        );

        g.setColour (pal ? pal->primary.withAlpha (0.5f) : juce::Colour (0x804477aa));
        g.fillRect (previewRect);
        g.setColour (pal ? pal->primary : juce::Colour (0xff4477aa));
        g.drawRect (previewRect, 1.0f);
    }
}

void PianoRollComponent::NoteGridComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    dragStart = e.getPosition();

    // Check if clicking on a note
    te::MidiNote* clickedNote = nullptr;
    for (const auto& noteRect : noteRectangles)
    {
        if (noteRect.bounds.contains (e.position))
        {
            clickedNote = noteRect.note;
            break;
        }
    }

    if (clickedNote != nullptr)
    {
        // Check if clicking on resize edge
        for (const auto& noteRect : noteRectangles)
        {
            if (noteRect.note == clickedNote)
            {
                float rightEdge = noteRect.bounds.getRight();
                if (std::abs (e.position.x - rightEdge) < resizeEdgeWidth)
                {
                    dragMode = DragMode::ResizeNote;
                    resizeNote = clickedNote;
                    noteOriginalStart = clickedNote->getStartBeat().inBeats();
                    noteOriginalLength = clickedNote->getLengthBeats().inBeats();
                    return;
                }
                break;
            }
        }

        // Select note
        if (! e.mods.isShiftDown())
            selectedNotes.clear();
        selectedNotes.insert (clickedNote);

        // Setup for move
        dragMode = DragMode::MoveNotes;
        double clickBeat = xToBeat (e.x);
        int clickPitch = yToPitch (e.y);
        noteDragOffset.x = clickBeat - clickedNote->getStartBeat().inBeats();
        noteDragOffset.y = clickPitch - clickedNote->getNoteNumber();
    }
    else
    {
        // Empty grid - start note creation or lasso
        if (e.mods.isShiftDown())
        {
            dragMode = DragMode::SelectLasso;
            lassoRect = juce::Rectangle<int> (e.x, e.y, 0, 0);
        }
        else
        {
            dragMode = DragMode::CreateNote;
            createNoteStartBeat = snapBeat (xToBeat (e.x));
            createNotePitch = yToPitch (e.y);
            selectedNotes.clear();
        }
    }

    repaint();
}

void PianoRollComponent::NoteGridComponent::mouseDrag (const juce::MouseEvent& e)
{
    switch (dragMode)
    {
        case DragMode::None:
            break;

        case DragMode::CreateNote:
            repaint();
            break;

        case DragMode::SelectLasso:
        {
            lassoRect = juce::Rectangle<int>::leftTopRightBottom (
                std::min (dragStart.x, e.x),
                std::min (dragStart.y, e.y),
                std::max (dragStart.x, e.x),
                std::max (dragStart.y, e.y)
            );
            repaint();
            break;
        }

        case DragMode::MoveNotes:
        {
            if (selectedNotes.empty())
                break;

            double targetBeat = snapBeat (xToBeat (e.x) - noteDragOffset.x);
            int targetPitch = juce::jlimit (0, 127, yToPitch (e.y) - (int) noteDragOffset.y);

            // Find first selected note to calculate delta
            te::MidiNote* firstNote = *selectedNotes.begin();
            double deltaBeat = targetBeat - firstNote->getStartBeat().inBeats();
            int deltaPitch = targetPitch - firstNote->getNoteNumber();

            editSession.performEdit ("Move Notes", true, [this, deltaBeat, deltaPitch] (te::Edit& edit)
            {
                for (auto* note : selectedNotes)
                {
                    double newStart = std::max (0.0, note->getStartBeat().inBeats() + deltaBeat);
                    int newPitch = juce::jlimit (0, 127, note->getNoteNumber() + deltaPitch);

                    note->setStartAndLength (te::BeatPosition::fromBeats (newStart),
                                            note->getLengthBeats(), &edit.getUndoManager());
                    note->setNoteNumber (newPitch, &edit.getUndoManager());
                }
            });

            rebuildNoteRectangles();
            repaint();
            break;
        }

        case DragMode::ResizeNote:
        {
            if (resizeNote == nullptr)
                break;

            double newEndBeat = snapBeat (xToBeat (e.x));
            double beatsPerBar = 4.0;
            double minLength = pianoRoll.snapSettings.getBeatsForGridSize (beatsPerBar);
            double newLength = std::max (minLength, newEndBeat - resizeNote->getStartBeat().inBeats());

            editSession.performEdit ("Resize Note", true, [this, newLength] (te::Edit& edit)
            {
                resizeNote->setStartAndLength (resizeNote->getStartBeat(),
                                              te::BeatDuration::fromBeats (newLength),
                                              &edit.getUndoManager());
            });

            rebuildNoteRectangles();
            repaint();
            break;
        }

        default:
            break;
    }
}

void PianoRollComponent::NoteGridComponent::mouseUp (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::CreateNote)
    {
        // Finalize note creation
        double endBeat = xToBeat (e.x);
        double snappedEnd = snapBeat (endBeat);
        double beatsPerBar = 4.0;
        double minLength = pianoRoll.snapSettings.getBeatsForGridSize (beatsPerBar);
        double length = std::max (minLength, snappedEnd - createNoteStartBeat);

        editSession.performEdit ("Add Note", [this, length] (te::Edit& edit)
        {
            auto& midiList = midiClip.getSequence();
            midiList.addNote (createNotePitch,
                             te::BeatPosition::fromBeats (createNoteStartBeat),
                             te::BeatDuration::fromBeats (length),
                             100, // default velocity
                             0,   // colour
                             &edit.getUndoManager());
        });

        rebuildNoteRectangles();
    }
    else if (dragMode == DragMode::SelectLasso)
    {
        // Select notes in lasso
        if (! e.mods.isShiftDown())
            selectedNotes.clear();

        for (const auto& noteRect : noteRectangles)
        {
            if (lassoRect.intersects (noteRect.bounds.toNearestInt()))
                selectedNotes.insert (noteRect.note);
        }
    }

    dragMode = DragMode::None;
    editSession.endCoalescedTransaction();
    repaint();
}

void PianoRollComponent::NoteGridComponent::mouseMove (const juce::MouseEvent& e)
{
    // Update hover state
    te::MidiNote* newHoveredNote = nullptr;
    bool overResizeEdge = false;

    for (const auto& noteRect : noteRectangles)
    {
        if (noteRect.bounds.contains (e.position))
        {
            newHoveredNote = noteRect.note;
            float rightEdge = noteRect.bounds.getRight();
            if (std::abs (e.position.x - rightEdge) < resizeEdgeWidth)
            {
                overResizeEdge = true;
            }
            break;
        }
    }

    if (hoveredNote != newHoveredNote)
    {
        hoveredNote = newHoveredNote;
        repaint();
    }

    setMouseCursor (overResizeEdge ? juce::MouseCursor::LeftRightResizeCursor
                                   : juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::NoteGridComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click on note to delete (alternative to Delete key)
    for (const auto& noteRect : noteRectangles)
    {
        if (noteRect.bounds.contains (e.position))
        {
            editSession.performEdit ("Delete Note", [this, note = noteRect.note] (te::Edit& edit)
            {
                auto& midiList = midiClip.getSequence();
                midiList.removeNote (*note, &edit.getUndoManager());
            });

            selectedNotes.erase (noteRect.note);
            rebuildNoteRectangles();
            repaint();
            break;
        }
    }
}

void PianoRollComponent::NoteGridComponent::mouseWheelMove (const juce::MouseEvent& e,
                                                            const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() && e.mods.isShiftDown())
    {
        // Vertical zoom (row height)
        int delta = wheel.deltaY > 0 ? -1 : 1;
        int newRowHeight = pianoRoll.keyboard->getRowHeight() + delta;
        pianoRoll.keyboard->setRowHeight (newRowHeight);
        setSize (getWidth(), pianoRoll.keyboard->getRowHeight() * 128);
        rebuildNoteRectangles();
        repaint();
    }
    else if (e.mods.isCommandDown())
    {
        // Horizontal zoom centered on cursor
        double zoomFactor = wheel.deltaY > 0 ? 0.9 : 1.1;
        pianoRoll.pixelsPerBeat = juce::jlimit (20.0, 120.0, pianoRoll.pixelsPerBeat * zoomFactor);

        // Adjust viewport to maintain cursor position
        if (auto* viewport = pianoRoll.viewport.get())
        {
            auto viewPos = viewport->getViewPosition();
            double cursorBeat = xToBeat (e.x + viewPos.x);
            int newCursorX = (int) beatToX (cursorBeat);
            int targetViewX = newCursorX - e.x;
            viewport->setViewPosition (targetViewX, viewPos.y);
        }

        rebuildNoteRectangles();
        if (pianoRoll.velocityLane)
            pianoRoll.velocityLane->updateVisibleNotes();
        repaint();
    }
    else if (e.mods.isShiftDown())
    {
        // Horizontal scroll
        if (auto* viewport = pianoRoll.viewport.get())
        {
            auto viewPos = viewport->getViewPosition();
            int delta = (int) (wheel.deltaY * 30.0f);
            viewport->setViewPosition (viewPos.x + delta, viewPos.y);
        }
    }
    else
    {
        // Vertical scroll
        if (auto* viewport = pianoRoll.viewport.get())
        {
            auto viewPos = viewport->getViewPosition();
            int delta = (int) (wheel.deltaY * 30.0f);
            viewport->setViewPosition (viewPos.x, viewPos.y + delta);
        }
    }
}

bool PianoRollComponent::NoteGridComponent::keyPressed (const juce::KeyPress& key)
{
    // Delete
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (! selectedNotes.empty())
        {
            editSession.performEdit ("Delete Notes", [this] (te::Edit& edit)
            {
                auto& midiList = midiClip.getSequence();
                for (auto* note : selectedNotes)
                    midiList.removeNote (*note, &edit.getUndoManager());
            });

            selectedNotes.clear();
            rebuildNoteRectangles();
            repaint();
            return true;
        }
    }

    // Copy (Cmd+C)
    if (key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0))
    {
        if (! selectedNotes.empty())
        {
            clipboard.clear();
            double minBeat = std::numeric_limits<double>::max();
            for (auto* note : selectedNotes)
                minBeat = std::min (minBeat, note->getStartBeat().inBeats());

            for (auto* note : selectedNotes)
            {
                clipboard.push_back ({
                    note->getNoteNumber(),
                    note->getStartBeat().inBeats() - minBeat,
                    note->getLengthBeats().inBeats(),
                    note->getVelocity()
                });
            }
            return true;
        }
    }

    // Cut (Cmd+X)
    if (key == juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0))
    {
        if (! selectedNotes.empty())
        {
            // Copy
            clipboard.clear();
            double minBeat = std::numeric_limits<double>::max();
            for (auto* note : selectedNotes)
                minBeat = std::min (minBeat, note->getStartBeat().inBeats());

            for (auto* note : selectedNotes)
            {
                clipboard.push_back ({
                    note->getNoteNumber(),
                    note->getStartBeat().inBeats() - minBeat,
                    note->getLengthBeats().inBeats(),
                    note->getVelocity()
                });
            }

            // Delete
            editSession.performEdit ("Cut Notes", [this] (te::Edit& edit)
            {
                auto& midiList = midiClip.getSequence();
                for (auto* note : selectedNotes)
                    midiList.removeNote (*note, &edit.getUndoManager());
            });

            selectedNotes.clear();
            rebuildNoteRectangles();
            repaint();
            return true;
        }
    }

    // Paste (Cmd+V)
    if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
    {
        if (! clipboard.empty())
        {
            auto& tempoSeq = midiClip.edit.tempoSequence;
            auto& playheadPos = midiClip.edit.getTransport().position;
            double playheadBeat = tempoSeq.toBeats (playheadPos.get()).inBeats();
            auto beatRange = tempoSeq.toBeats (midiClip.getPosition().time);
            double clipStartBeat = beatRange.getStart().inBeats();
            double pasteOffset = playheadBeat - clipStartBeat;

            editSession.performEdit ("Paste Notes", [this, pasteOffset] (te::Edit& edit)
            {
                auto& midiList = midiClip.getSequence();
                for (const auto& clipNote : clipboard)
                {
                    midiList.addNote (clipNote.pitch,
                                     te::BeatPosition::fromBeats (pasteOffset + clipNote.startBeat),
                                     te::BeatDuration::fromBeats (clipNote.lengthBeats),
                                     clipNote.velocity,
                                     0,
                                     &edit.getUndoManager());
                }
            });

            rebuildNoteRectangles();
            repaint();
            return true;
        }
    }

    // Duplicate (Cmd+D)
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))
    {
        if (! selectedNotes.empty())
        {
            double beatsPerBar = 4.0;
            double gridSize = pianoRoll.snapSettings.getBeatsForGridSize (beatsPerBar);

            editSession.performEdit ("Duplicate Notes", [this, gridSize] (te::Edit& edit)
            {
                auto& midiList = midiClip.getSequence();
                std::vector<te::MidiNote*> duplicates;

                for (auto* note : selectedNotes)
                {
                    double newStart = note->getStartBeat().inBeats() + gridSize;
                    auto* newNote = midiList.addNote (note->getNoteNumber(),
                                                     te::BeatPosition::fromBeats (newStart),
                                                     note->getLengthBeats(),
                                                     note->getVelocity(),
                                                     0,
                                                     &edit.getUndoManager());
                    if (newNote)
                        duplicates.push_back (newNote);
                }
            });

            rebuildNoteRectangles();
            repaint();
            return true;
        }
    }

    // Select All (Cmd+A)
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectAllNotes();
        return true;
    }

    // Quantize (Cmd+Q)
    if (key == juce::KeyPress ('q', juce::ModifierKeys::commandModifier, 0))
    {
        quantizeSelectedNotes();
        return true;
    }

    // Deselect (Escape)
    if (key == juce::KeyPress::escapeKey)
    {
        selectedNotes.clear();
        repaint();
        return true;
    }

    // Zoom in (+)
    if (key == juce::KeyPress ('+', 0, 0) || key == juce::KeyPress ('=', 0, 0))
    {
        pianoRoll.pixelsPerBeat = juce::jlimit (20.0, 120.0, pianoRoll.pixelsPerBeat * 1.5);
        rebuildNoteRectangles();
        if (pianoRoll.velocityLane)
            pianoRoll.velocityLane->updateVisibleNotes();
        repaint();
        return true;
    }

    // Zoom out (-)
    if (key == juce::KeyPress ('-', 0, 0))
    {
        pianoRoll.pixelsPerBeat = juce::jlimit (20.0, 120.0, pianoRoll.pixelsPerBeat / 1.5);
        rebuildNoteRectangles();
        if (pianoRoll.velocityLane)
            pianoRoll.velocityLane->updateVisibleNotes();
        repaint();
        return true;
    }

    // Zoom to fit (F)
    if (key == juce::KeyPress ('f', 0, 0))
    {
        auto& midiList = midiClip.getSequence();
        if (midiList.getNotes().isEmpty())
            return true;

        double minBeat = std::numeric_limits<double>::max();
        double maxBeat = std::numeric_limits<double>::lowest();

        for (auto* note : midiList.getNotes())
        {
            minBeat = std::min (minBeat, note->getStartBeat().inBeats());
            maxBeat = std::max (maxBeat, note->getStartBeat().inBeats() + note->getLengthBeats().inBeats());
        }

        if (auto* viewport = pianoRoll.viewport.get())
        {
            double beatRange = maxBeat - minBeat;
            if (beatRange > 0.0)
            {
                pianoRoll.pixelsPerBeat = viewport->getWidth() / beatRange * 0.9;
                pianoRoll.pixelsPerBeat = juce::jlimit (20.0, 120.0, pianoRoll.pixelsPerBeat);
                rebuildNoteRectangles();
                if (pianoRoll.velocityLane)
                    pianoRoll.velocityLane->updateVisibleNotes();
                viewport->setViewPosition ((int) beatToX (minBeat) - 10, viewport->getViewPosition().y);
                repaint();
            }
        }
        return true;
    }

    // Transpose up (Up arrow)
    if (key == juce::KeyPress::upKey)
    {
        if (! selectedNotes.empty())
        {
            int semitones = key.getModifiers().isShiftDown() ? 12 : 1;
            editSession.performEdit ("Transpose Notes", [this, semitones] (te::Edit& edit)
            {
                for (auto* note : selectedNotes)
                {
                    int newPitch = juce::jlimit (0, 127, note->getNoteNumber() + semitones);
                    note->setNoteNumber (newPitch, &edit.getUndoManager());
                }
            });

            rebuildNoteRectangles();
            repaint();
            return true;
        }
    }

    // Transpose down (Down arrow)
    if (key == juce::KeyPress::downKey)
    {
        if (! selectedNotes.empty())
        {
            int semitones = key.getModifiers().isShiftDown() ? 12 : 1;
            editSession.performEdit ("Transpose Notes", [this, semitones] (te::Edit& edit)
            {
                for (auto* note : selectedNotes)
                {
                    int newPitch = juce::jlimit (0, 127, note->getNoteNumber() - semitones);
                    note->setNoteNumber (newPitch, &edit.getUndoManager());
                }
            });

            rebuildNoteRectangles();
            repaint();
            return true;
        }
    }

    return false;
}

void PianoRollComponent::NoteGridComponent::rebuildNoteRectangles()
{
    noteRectangles.clear();
    int rowHeight = pianoRoll.keyboard->getRowHeight();

    auto& midiList = midiClip.getSequence();
    for (auto* note : midiList.getNotes())
    {
        double startBeat = note->getStartBeat().inBeats();
        double lengthBeats = note->getLengthBeats().inBeats();
        int pitch = note->getNoteNumber();

        juce::Rectangle<float> bounds (
            (float) beatToX (startBeat),
            (float) pitchToY (pitch),
            (float) (lengthBeats * pianoRoll.pixelsPerBeat),
            (float) rowHeight
        );

        noteRectangles.push_back ({ note, bounds });
    }

    // Remove invalid selections
    auto it = selectedNotes.begin();
    while (it != selectedNotes.end())
    {
        bool found = false;
        for (auto* note : midiList.getNotes())
        {
            if (note == *it)
            {
                found = true;
                break;
            }
        }
        if (! found)
            it = selectedNotes.erase (it);
        else
            ++it;
    }
}

void PianoRollComponent::NoteGridComponent::quantizeSelectedNotes()
{
    if (selectedNotes.empty())
        return;

    double beatsPerBar = 4.0; // Default 4/4

    editSession.performEdit ("Quantize Notes", [this, beatsPerBar] (te::Edit& edit)
    {
        for (auto* note : selectedNotes)
        {
            double rawBeat = note->getStartBeat().inBeats();
            double quantizedBeat = pianoRoll.snapSettings.snapBeat (rawBeat, beatsPerBar);
            note->setStartAndLength (te::BeatPosition::fromBeats (quantizedBeat),
                                    note->getLengthBeats(),
                                    &edit.getUndoManager());
        }
    });

    rebuildNoteRectangles();
    repaint();
}

void PianoRollComponent::NoteGridComponent::selectAllNotes()
{
    selectedNotes.clear();
    auto& midiList = midiClip.getSequence();
    for (auto* note : midiList.getNotes())
        selectedNotes.insert (note);
    repaint();
}

void PianoRollComponent::NoteGridComponent::selectNotesByPitch (int pitch, bool addToSelection)
{
    if (! addToSelection)
        selectedNotes.clear();

    auto& midiList = midiClip.getSequence();
    for (auto* note : midiList.getNotes())
    {
        if (note->getNoteNumber() == pitch)
            selectedNotes.insert (note);
    }
    repaint();
}

double PianoRollComponent::NoteGridComponent::beatToX (double beat) const
{
    return beat * pianoRoll.pixelsPerBeat;
}

double PianoRollComponent::NoteGridComponent::xToBeat (int x) const
{
    return x / pianoRoll.pixelsPerBeat;
}

int PianoRollComponent::NoteGridComponent::pitchToY (int pitch) const
{
    return (127 - pitch) * pianoRoll.keyboard->getRowHeight();
}

int PianoRollComponent::NoteGridComponent::yToPitch (int y) const
{
    return 127 - (y / pianoRoll.keyboard->getRowHeight());
}

double PianoRollComponent::NoteGridComponent::snapBeat (double beat) const
{
    double beatsPerBar = 4.0; // Default 4/4
    return pianoRoll.snapSettings.snapBeat (beat, beatsPerBar);
}

std::string PianoRollComponent::NoteGridComponent::pitchToNoteName (int pitch) const
{
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int pitchClass = pitch % 12;
    int octave = (pitch / 12) - 1;
    return std::string (noteNames[pitchClass]) + std::to_string (octave);
}

//==============================================================================
// VelocityLane
//==============================================================================
PianoRollComponent::VelocityLane::VelocityLane (te::MidiClip& clip,
                                                EditSession& session,
                                                NoteGridComponent& grid)
    : midiClip (clip), editSession (session), noteGrid (grid)
{
    setSize (2000, height);
}

void PianoRollComponent::VelocityLane::paint (juce::Graphics& g)
{
    auto* pal = waive::getWaivePalette (*this);

    // Background
    g.fillAll (pal ? pal->insetBg : juce::Colour (0xff1a1a1a));

    // Border
    g.setColour (pal ? pal->border : juce::Colour (0xff555555));
    g.drawLine (0.0f, 0.0f, (float) getWidth(), 0.0f, 1.0f);

    // Velocity bars
    juce::Colour barColour = pal ? pal->accent : juce::Colour (0xff66aaff);
    for (const auto& bar : velocityBars)
    {
        g.setColour (barColour);
        g.fillRect (bar.bounds);
        g.setColour (barColour.darker());
        g.drawRect (bar.bounds, 1.0f);
    }
}

void PianoRollComponent::VelocityLane::mouseDown (const juce::MouseEvent& e)
{
    // Find velocity bar under mouse
    for (const auto& bar : velocityBars)
    {
        if (bar.bounds.contains (e.position))
        {
            draggedNote = bar.note;
            dragStartVelocity = bar.note->getVelocity();
            break;
        }
    }
}

void PianoRollComponent::VelocityLane::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedNote == nullptr)
        return;

    // Calculate new velocity from y position
    float normalised = 1.0f - (e.position.y / (float) height);
    int newVelocity = juce::jlimit (1, 127, (int) (normalised * 127.0f));

    editSession.performEdit ("Change Velocity", true, [this, newVelocity] (te::Edit& edit)
    {
        draggedNote->setVelocity (newVelocity, &edit.getUndoManager());
    });

    updateVisibleNotes();
    repaint();
}

void PianoRollComponent::VelocityLane::mouseUp (const juce::MouseEvent&)
{
    draggedNote = nullptr;
    editSession.endCoalescedTransaction();
}

void PianoRollComponent::VelocityLane::updateVisibleNotes()
{
    velocityBars.clear();

    auto& midiList = midiClip.getSequence();
    for (auto* note : midiList.getNotes())
    {
        double startBeat = note->getStartBeat().inBeats();
        double lengthBeats = note->getLengthBeats().inBeats();
        int velocity = note->getVelocity();

        float x = (float) noteGrid.beatToX (startBeat);
        float w = (float) (lengthBeats * noteGrid.getPixelsPerBeat());
        float barHeight = (velocity / 127.0f) * (float) height;

        juce::Rectangle<float> bounds (x, height - barHeight, w, barHeight);
        velocityBars.push_back ({ note, bounds });
    }
}

//==============================================================================
// Toolbar
//==============================================================================
PianoRollComponent::Toolbar::Toolbar (PianoRollComponent& parent)
    : pianoRoll (parent)
{
    snapBox.addItem ("Snap: Off", 1);
    snapBox.addItem ("Snap: Bar", 2);
    snapBox.addItem ("Snap: Beat", 3);
    snapBox.addItem ("Snap: 1/2", 4);
    snapBox.addItem ("Snap: 1/4", 5);
    snapBox.addItem ("Snap: 1/8", 6);
    snapBox.addItem ("Snap: 1/16", 7);
    snapBox.addItem ("Snap: Triplet", 8);
    snapBox.setSelectedId (3, juce::dontSendNotification);

    gridBox.addItem ("Grid: Bar", 1);
    gridBox.addItem ("Grid: Beat", 2);
    gridBox.addItem ("Grid: 1/2", 3);
    gridBox.addItem ("Grid: 1/4", 4);
    gridBox.addItem ("Grid: 1/8", 5);
    gridBox.addItem ("Grid: 1/16", 6);
    gridBox.addItem ("Grid: Triplet", 7);
    gridBox.setSelectedId (2, juce::dontSendNotification);

    snapBox.onChange = [this]
    {
        int id = snapBox.getSelectedId();
        switch (id)
        {
            case 1: pianoRoll.snapSettings.snapEnabled = false; break;
            case 2: pianoRoll.snapSettings.snapEnabled = true; pianoRoll.snapSettings.gridSize = SnapSettings::Bar; break;
            case 3: pianoRoll.snapSettings.snapEnabled = true; pianoRoll.snapSettings.gridSize = SnapSettings::Beat; break;
            case 4: pianoRoll.snapSettings.snapEnabled = true; pianoRoll.snapSettings.gridSize = SnapSettings::HalfBeat; break;
            case 5: pianoRoll.snapSettings.snapEnabled = true; pianoRoll.snapSettings.gridSize = SnapSettings::QuarterBeat; break;
            case 6: pianoRoll.snapSettings.snapEnabled = true; pianoRoll.snapSettings.gridSize = SnapSettings::Eighth; break;
            case 7: pianoRoll.snapSettings.snapEnabled = true; pianoRoll.snapSettings.gridSize = SnapSettings::Sixteenth; break;
            case 8: pianoRoll.snapSettings.snapEnabled = true; pianoRoll.snapSettings.gridSize = SnapSettings::Triplet; break;
            default: break;
        }
        if (pianoRoll.noteGrid)
            pianoRoll.noteGrid->repaint();
    };

    gridBox.onChange = [this]
    {
        int id = gridBox.getSelectedId();
        switch (id)
        {
            case 1: pianoRoll.snapSettings.gridSize = SnapSettings::Bar; break;
            case 2: pianoRoll.snapSettings.gridSize = SnapSettings::Beat; break;
            case 3: pianoRoll.snapSettings.gridSize = SnapSettings::HalfBeat; break;
            case 4: pianoRoll.snapSettings.gridSize = SnapSettings::QuarterBeat; break;
            case 5: pianoRoll.snapSettings.gridSize = SnapSettings::Eighth; break;
            case 6: pianoRoll.snapSettings.gridSize = SnapSettings::Sixteenth; break;
            case 7: pianoRoll.snapSettings.gridSize = SnapSettings::Triplet; break;
            default: break;
        }
        if (pianoRoll.noteGrid)
            pianoRoll.noteGrid->repaint();
    };

    quantizeButton.onClick = [this]
    {
        if (pianoRoll.noteGrid)
            pianoRoll.noteGrid->quantizeSelectedNotes();
    };

    zoomInButton.onClick = [this]
    {
        pianoRoll.pixelsPerBeat = std::min (120.0, pianoRoll.pixelsPerBeat * 1.5);
        if (pianoRoll.noteGrid)
        {
            pianoRoll.noteGrid->rebuildNoteRectangles();
            pianoRoll.noteGrid->repaint();
        }
        if (pianoRoll.velocityLane)
        {
            pianoRoll.velocityLane->updateVisibleNotes();
            pianoRoll.velocityLane->repaint();
        }
    };

    zoomOutButton.onClick = [this]
    {
        pianoRoll.pixelsPerBeat = std::max (20.0, pianoRoll.pixelsPerBeat / 1.5);
        if (pianoRoll.noteGrid)
        {
            pianoRoll.noteGrid->rebuildNoteRectangles();
            pianoRoll.noteGrid->repaint();
        }
        if (pianoRoll.velocityLane)
        {
            pianoRoll.velocityLane->updateVisibleNotes();
            pianoRoll.velocityLane->repaint();
        }
    };

    addAndMakeVisible (snapLabel);
    addAndMakeVisible (snapBox);
    addAndMakeVisible (gridLabel);
    addAndMakeVisible (gridBox);
    addAndMakeVisible (quantizeButton);
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomOutButton);
}

void PianoRollComponent::Toolbar::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::xs);

    snapLabel.setBounds (bounds.removeFromLeft (44));
    bounds.removeFromLeft (waive::Spacing::xs);
    snapBox.setBounds (bounds.removeFromLeft (110));
    bounds.removeFromLeft (waive::Spacing::md);

    gridLabel.setBounds (bounds.removeFromLeft (38));
    bounds.removeFromLeft (waive::Spacing::xs);
    gridBox.setBounds (bounds.removeFromLeft (110));
    bounds.removeFromLeft (waive::Spacing::md);

    quantizeButton.setBounds (bounds.removeFromLeft (80));
    bounds.removeFromLeft (waive::Spacing::md);

    zoomInButton.setBounds (bounds.removeFromLeft (32));
    bounds.removeFromLeft (waive::Spacing::xs);
    zoomOutButton.setBounds (bounds.removeFromLeft (32));
}

//==============================================================================
// PianoRollComponent
//==============================================================================
PianoRollComponent::PianoRollComponent (te::MidiClip& clip, EditSession& session)
    : midiClip (clip), editSession (session)
{
    toolbar = std::make_unique<Toolbar> (*this);
    addAndMakeVisible (toolbar.get());

    keyboard = std::make_unique<PianoKeyboardSidebar>();
    addAndMakeVisible (keyboard.get());

    noteGrid = std::make_unique<NoteGridComponent> (clip, session, *this);
    velocityLane = std::make_unique<VelocityLane> (clip, session, *noteGrid);
    velocityLane->updateVisibleNotes();

    viewport = std::make_unique<juce::Viewport>();
    viewport->setViewedComponent (noteGrid.get(), false);
    addAndMakeVisible (viewport.get());

    addAndMakeVisible (velocityLane.get());
}

PianoRollComponent::~PianoRollComponent() = default;

void PianoRollComponent::paint (juce::Graphics& g)
{
    auto* pal = waive::getWaivePalette (*this);
    g.fillAll (pal ? pal->panelBg : juce::Colour (0xff2a2a2a));
}

void PianoRollComponent::resized()
{
    auto bounds = getLocalBounds();

    // Toolbar at top
    toolbar->setBounds (bounds.removeFromTop (Toolbar::height));

    // Keyboard on left
    auto keyboardBounds = bounds.removeFromLeft (PianoKeyboardSidebar::width);
    keyboard->setBounds (keyboardBounds.removeFromTop (bounds.getHeight() - VelocityLane::height));

    // Velocity lane at bottom
    auto velocityBounds = bounds.removeFromBottom (VelocityLane::height);
    velocityBounds.removeFromLeft (0); // Align with note grid (no keyboard offset)
    velocityLane->setBounds (velocityBounds);

    // Note grid viewport in center
    viewport->setBounds (bounds);
}
