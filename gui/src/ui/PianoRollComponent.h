#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include <set>

namespace te = tracktion;

class EditSession;

//==============================================================================
/** MIDI Piano Roll Editor with note grid, velocity lane, and piano keyboard. */
class PianoRollComponent : public juce::Component
{
public:
    PianoRollComponent (te::MidiClip& clip, EditSession& editSession);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==============================================================================
    /** Vertical piano keyboard sidebar */
    class PianoKeyboardSidebar : public juce::Component
    {
    public:
        PianoKeyboardSidebar();
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;

        static constexpr int width = 48;
        static constexpr int rowHeight = 12;
    };

    //==============================================================================
    /** Main note grid with editing capabilities */
    class NoteGridComponent : public juce::Component
    {
    public:
        NoteGridComponent (te::MidiClip& clip, EditSession& session, PianoRollComponent& parent);
        ~NoteGridComponent() override;

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        bool keyPressed (const juce::KeyPress& key) override;

        void rebuildNoteRectangles();

        double beatToX (double beat) const;
        double xToBeat (int x) const;
        int pitchToY (int pitch) const;
        int yToPitch (int y) const;
        double snapBeat (double beat) const;
        double getPixelsPerBeat() const { return pianoRoll.pixelsPerBeat; }

    private:
        struct NoteRectangle
        {
            te::MidiNote* note;
            juce::Rectangle<float> bounds;
        };

        enum class DragMode { None, CreateNote, SelectLasso, MoveNotes, ResizeNote };

        te::MidiClip& midiClip;
        EditSession& editSession;
        PianoRollComponent& pianoRoll;

        std::vector<NoteRectangle> noteRectangles;
        std::set<te::MidiNote*> selectedNotes;

        DragMode dragMode = DragMode::None;
        juce::Point<int> dragStart;
        juce::Rectangle<int> lassoRect;
        double createNoteStartBeat = 0.0;
        int createNotePitch = 60;
        te::MidiNote* resizeNote = nullptr;
        double noteOriginalStart = 0.0;
        double noteOriginalLength = 0.0;
        juce::Point<double> noteDragOffset;

        static constexpr int resizeEdgeWidth = 8;
    };

    //==============================================================================
    /** Bottom velocity lane for editing note velocities */
    class VelocityLane : public juce::Component
    {
    public:
        VelocityLane (te::MidiClip& clip, EditSession& session, NoteGridComponent& grid);

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;

        void updateVisibleNotes();

        static constexpr int height = 60;

    private:
        te::MidiClip& midiClip;
        EditSession& editSession;
        NoteGridComponent& noteGrid;

        struct VelocityBar
        {
            te::MidiNote* note;
            juce::Rectangle<float> bounds;
        };

        std::vector<VelocityBar> velocityBars;
        te::MidiNote* draggedNote = nullptr;
        int dragStartVelocity = 0;
    };

    //==============================================================================
    /** Toolbar with snap/grid/zoom controls */
    class Toolbar : public juce::Component
    {
    public:
        Toolbar (PianoRollComponent& parent);

        void resized() override;

        juce::ComboBox& getSnapBox() { return snapBox; }
        juce::ComboBox& getGridBox() { return gridBox; }
        juce::TextButton& getZoomInButton() { return zoomInButton; }
        juce::TextButton& getZoomOutButton() { return zoomOutButton; }

        static constexpr int height = 32;

    private:
        PianoRollComponent& pianoRoll;

        juce::Label snapLabel { {}, "Snap:" };
        juce::ComboBox snapBox;
        juce::Label gridLabel { {}, "Grid:" };
        juce::ComboBox gridBox;
        juce::TextButton zoomInButton { "+" };
        juce::TextButton zoomOutButton { "-" };
    };

    //==============================================================================
    te::MidiClip& midiClip;
    EditSession& editSession;

    std::unique_ptr<Toolbar> toolbar;
    std::unique_ptr<PianoKeyboardSidebar> keyboard;
    std::unique_ptr<juce::Viewport> viewport;
    std::unique_ptr<NoteGridComponent> noteGrid;
    std::unique_ptr<VelocityLane> velocityLane;

    double pixelsPerBeat = 60.0;
    double snapResolution = 0.25; // quarter beat
    bool snapEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
