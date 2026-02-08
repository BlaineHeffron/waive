#include "PianoRollComponent.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"
#include "WaiveSpacing.h"
#include "WaiveFonts.h"

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
    // Note preview on click - to be implemented with MIDI output
    juce::ignoreUnused (e);
}

//==============================================================================
// NoteGridComponent
//==============================================================================
PianoRollComponent::NoteGridComponent::NoteGridComponent (te::MidiClip& clip,
                                                          EditSession& session,
                                                          PianoRollComponent& parent)
    : midiClip (clip), editSession (session), pianoRoll (parent)
{
    setSize (2000, PianoKeyboardSidebar::rowHeight * 128);
    setWantsKeyboardFocus (true);
    rebuildNoteRectangles();
}

PianoRollComponent::NoteGridComponent::~NoteGridComponent() = default;

void PianoRollComponent::NoteGridComponent::paint (juce::Graphics& g)
{
    auto* pal = waive::getWaivePalette (*this);
    auto bounds = getLocalBounds();

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
        g.fillRect (0, y, getWidth(), PianoKeyboardSidebar::rowHeight);
    }

    // Grid lines - horizontal (pitch)
    g.setColour (pal ? pal->gridMinor : juce::Colour (0xff444444));
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        int y = pitchToY (pitch);
        g.drawLine (0.0f, (float) y, (float) getWidth(), (float) y, 0.5f);
    }

    // Grid lines - vertical (beats)
    auto beatRange = midiClip.edit.tempoSequence.toBeats (midiClip.getPosition().time);
    double clipLengthBeats = beatRange.getLength().inBeats();
    for (double beat = 0.0; beat <= clipLengthBeats; beat += pianoRoll.snapResolution)
    {
        int x = (int) beatToX (beat);
        bool isMajor = (std::fmod (beat, 1.0) < 0.001); // Major line on whole beats
        g.setColour (isMajor
                     ? (pal ? pal->gridMajor : juce::Colour (0xff666666))
                     : (pal ? pal->gridMinor : juce::Colour (0xff444444)));
        g.drawLine ((float) x, 0.0f, (float) x, (float) getHeight(),
                   isMajor ? 1.0f : 0.5f);
    }

    // Notes
    juce::Colour noteColour = pal ? pal->primary : juce::Colour (0xff4477aa);
    for (const auto& noteRect : noteRectangles)
    {
        bool isSelected = selectedNotes.count (noteRect.note) > 0;

        g.setColour (isSelected
                     ? (pal ? pal->clipSelected : juce::Colour (0xff5588bb))
                     : noteColour);
        g.fillRect (noteRect.bounds);

        g.setColour (isSelected
                     ? (pal ? pal->selectionBorder : juce::Colour (0xffffffff))
                     : noteColour.darker());
        g.drawRect (noteRect.bounds, isSelected ? 2.0f : 1.0f);
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
        double length = std::max (pianoRoll.snapResolution, snappedEnd - createNoteStartBeat);

        juce::Rectangle<float> previewRect (
            (float) beatToX (createNoteStartBeat),
            (float) pitchToY (createNotePitch),
            (float) (length * pianoRoll.pixelsPerBeat),
            (float) PianoKeyboardSidebar::rowHeight
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
            double newLength = std::max (pianoRoll.snapResolution,
                                        newEndBeat - resizeNote->getStartBeat().inBeats());

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
        double length = std::max (pianoRoll.snapResolution, snappedEnd - createNoteStartBeat);

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
    // Update cursor based on position
    bool overResizeEdge = false;
    for (const auto& noteRect : noteRectangles)
    {
        if (noteRect.bounds.contains (e.position))
        {
            float rightEdge = noteRect.bounds.getRight();
            if (std::abs (e.position.x - rightEdge) < resizeEdgeWidth)
            {
                overResizeEdge = true;
                break;
            }
        }
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

bool PianoRollComponent::NoteGridComponent::keyPressed (const juce::KeyPress& key)
{
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

    return false;
}

void PianoRollComponent::NoteGridComponent::rebuildNoteRectangles()
{
    noteRectangles.clear();

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
            (float) PianoKeyboardSidebar::rowHeight
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
    return (127 - pitch) * PianoKeyboardSidebar::rowHeight;
}

int PianoRollComponent::NoteGridComponent::yToPitch (int y) const
{
    return 127 - (y / PianoKeyboardSidebar::rowHeight);
}

double PianoRollComponent::NoteGridComponent::snapBeat (double beat) const
{
    if (! pianoRoll.snapEnabled)
        return beat;

    double resolution = pianoRoll.snapResolution;
    return std::round (beat / resolution) * resolution;
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
    snapBox.addItem ("Snap: 1/4", 2);
    snapBox.addItem ("Snap: 1/8", 3);
    snapBox.addItem ("Snap: 1/16", 4);
    snapBox.setSelectedId (2, juce::dontSendNotification);

    gridBox.addItem ("Grid: 1/4", 1);
    gridBox.addItem ("Grid: 1/8", 2);
    gridBox.addItem ("Grid: 1/16", 3);
    gridBox.setSelectedId (1, juce::dontSendNotification);

    snapBox.onChange = [this]
    {
        int id = snapBox.getSelectedId();
        if (id == 1)
        {
            pianoRoll.snapEnabled = false;
        }
        else
        {
            pianoRoll.snapEnabled = true;
            switch (id)
            {
                case 2: pianoRoll.snapResolution = 0.25; break;  // 1/4 beat
                case 3: pianoRoll.snapResolution = 0.125; break; // 1/8 beat
                case 4: pianoRoll.snapResolution = 0.0625; break; // 1/16 beat
                default: break;
            }
        }
        if (pianoRoll.noteGrid)
            pianoRoll.noteGrid->repaint();
    };

    gridBox.onChange = [this]
    {
        int id = gridBox.getSelectedId();
        switch (id)
        {
            case 1: pianoRoll.snapResolution = 0.25; break;
            case 2: pianoRoll.snapResolution = 0.125; break;
            case 3: pianoRoll.snapResolution = 0.0625; break;
            default: break;
        }
        if (pianoRoll.noteGrid)
            pianoRoll.noteGrid->repaint();
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
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomOutButton);
}

void PianoRollComponent::Toolbar::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::xs);

    snapLabel.setBounds (bounds.removeFromLeft (44));
    bounds.removeFromLeft (waive::Spacing::xs);
    snapBox.setBounds (bounds.removeFromLeft (100));
    bounds.removeFromLeft (waive::Spacing::md);

    gridLabel.setBounds (bounds.removeFromLeft (38));
    bounds.removeFromLeft (waive::Spacing::xs);
    gridBox.setBounds (bounds.removeFromLeft (100));
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
