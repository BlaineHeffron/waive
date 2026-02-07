#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

//==============================================================================
/** Owns a te::Edit, centralises all mutations, and provides undo/redo. */
class EditSession
{
public:
    explicit EditSession (te::Engine& engine);
    ~EditSession();

    te::Engine& getEngine()  { return engine; }
    te::Edit& getEdit();

    //==============================================================================
    /** Listener interface for edit lifecycle events. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void editAboutToChange() {}
        virtual void editChanged() {}
        virtual void editStateChanged() {}
    };

    void addListener (Listener* l)       { listeners.add (l); }
    void removeListener (Listener* l)    { listeners.remove (l); }

    //==============================================================================
    /** Swap the internal edit with a new one. Notifies listeners. */
    void replaceEdit (std::unique_ptr<te::Edit> newEdit);

    /** Load an edit from a .tracktionedit file. */
    void loadFromFile (const juce::File& file);

    /** Create a fresh empty edit with 1 audio track. */
    void createNew();

    //==============================================================================
    /** Flush edit state to its backing file. */
    void flushState();

    /** Has the edit changed since the last save? */
    bool hasChangedSinceSaved() const;

    /** Reset the changed-since-saved flag. */
    void resetChangedStatus();

    //==============================================================================
    /** Execute a mutation wrapped in an undo transaction.
        Returns true if the lambda ran without throwing. */
    bool performEdit (const juce::String& actionName,
                      std::function<void (te::Edit&)> mutation);

    /** Coalescing overload — skips beginNewTransaction when the previous
        transaction has the same name. Useful for slider drags. */
    bool performEdit (const juce::String& actionName,
                      bool coalesce,
                      std::function<void (te::Edit&)> mutation);

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void endCoalescedTransaction();

    juce::String getUndoDescription() const;
    juce::String getRedoDescription() const;

private:
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    juce::String lastTransactionName;
    juce::ListenerList<Listener> listeners;
};
