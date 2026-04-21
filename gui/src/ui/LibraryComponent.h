#pragma once

#include <JuceHeader.h>
#include "../edit/EditSession.h"

//==============================================================================
/** File browser for audio files. Double-click inserts a clip on the selected target track. */
class LibraryComponent : public juce::Component,
                         public juce::FileBrowserListener
{
public:
    explicit LibraryComponent (EditSession& session);
    ~LibraryComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool selectTargetTrackForTesting (int trackIndex);
    int getSelectedTargetTrackIndexForTesting() const;

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
    void refreshTargetTrackList();
    tracktion::engine::AudioTrack* getTargetTrack() const;

    EditSession& editSession;

    juce::TimeSliceThread scanThread { "LibraryScan" };
    juce::WildcardFileFilter fileFilter;
    juce::DirectoryContentsList directoryList;
    std::unique_ptr<juce::FileTreeComponent> fileTree;

    juce::Label targetTrackLabel;
    juce::ComboBox targetTrackCombo;
    juce::ComboBox favoritesCombo;
    juce::TextButton addFavButton;
    juce::TextButton goUpButton;

    juce::StringArray favoritesPaths;
};
