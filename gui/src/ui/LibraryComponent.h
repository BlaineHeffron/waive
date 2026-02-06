#pragma once

#include <JuceHeader.h>

class EditSession;

//==============================================================================
/** File browser for audio files. Double-click inserts a clip on the first track. */
class LibraryComponent : public juce::Component,
                         public juce::FileBrowserListener
{
public:
    explicit LibraryComponent (EditSession& session);
    ~LibraryComponent() override;

    void resized() override;

    // FileBrowserListener
    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File& file) override;
    void browserRootChanged (const juce::File&) override {}

private:
    void goUp();
    void setRoot (const juce::File& dir);
    void addCurrentToFavorites();
    void loadFavorites();
    void saveFavorites();

    EditSession& editSession;

    juce::TimeSliceThread scanThread { "LibraryScan" };
    juce::WildcardFileFilter fileFilter;
    juce::DirectoryContentsList directoryList;
    std::unique_ptr<juce::FileTreeComponent> fileTree;

    juce::ComboBox favoritesCombo;
    juce::TextButton addFavButton;
    juce::TextButton goUpButton;

    juce::StringArray favoritesPaths;
};
