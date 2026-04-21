#include "LibraryComponent.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

//==============================================================================
LibraryComponent::LibraryComponent (EditSession& session)
    : editSession (session),
      fileFilter ("*.wav;*.aiff;*.flac;*.mp3;*.ogg", "*", "Audio Files"),
      directoryList (&fileFilter, scanThread)
{
    scanThread.startThread (juce::Thread::Priority::background);

    auto defaultLibraryDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! defaultLibraryDir.isDirectory())
        defaultLibraryDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    directoryList.setDirectory (defaultLibraryDir, true, true);

    fileTree = std::make_unique<juce::FileTreeComponent> (directoryList);
    fileTree->addListener (this);
    fileTree->setDragAndDropDescription ("LibraryFile");
    fileTree->setTitle ("File Browser");
    fileTree->setDescription ("Browse and drag audio files");
    fileTree->setTooltip ("Browse and drag audio files into the timeline");
    addAndMakeVisible (fileTree.get());

    targetTrackLabel.setText ("Import To:", juce::dontSendNotification);
    targetTrackLabel.setTitle ("Import Target Label");
    targetTrackLabel.setDescription ("Label for the library import target track selector");
    addAndMakeVisible (targetTrackLabel);

    targetTrackCombo.setTitle ("Import Target Track");
    targetTrackCombo.setDescription ("Choose which track receives library imports");
    targetTrackCombo.setTooltip ("Choose the track used for double-click import");
    targetTrackCombo.setWantsKeyboardFocus (true);
    addAndMakeVisible (targetTrackCombo);

    goUpButton.setButtonText ("..");
    goUpButton.onClick = [this] { goUp(); };
    goUpButton.setTitle ("Go Up");
    goUpButton.setDescription ("Navigate to parent directory");
    goUpButton.setTooltip ("Navigate to parent directory");
    goUpButton.setWantsKeyboardFocus (true);
    addAndMakeVisible (goUpButton);

    addFavButton.setButtonText ("+");
    addFavButton.onClick = [this] { addCurrentToFavorites(); };
    addFavButton.setTitle ("Add Favorite");
    addFavButton.setDescription ("Add current directory to favorites");
    addFavButton.setTooltip ("Add current directory to favorites");
    addFavButton.setWantsKeyboardFocus (true);
    addAndMakeVisible (addFavButton);

    favoritesCombo.setTextWhenNothingSelected ("Favorites");
    favoritesCombo.onChange = [this]
    {
        int idx = favoritesCombo.getSelectedItemIndex();
        if (idx >= 0 && idx < favoritesPaths.size())
            setRoot (juce::File (favoritesPaths[idx]));
    };
    favoritesCombo.setTitle ("Favorites");
    favoritesCombo.setDescription ("Navigate to favorite directory");
    favoritesCombo.setTooltip ("Jump to a favorite directory");
    favoritesCombo.setWantsKeyboardFocus (true);
    addAndMakeVisible (favoritesCombo);

    loadFavorites();
    refreshTargetTrackList();
}

LibraryComponent::~LibraryComponent()
{
    fileTree->removeListener (this);
    scanThread.stopThread (1000);
}

void LibraryComponent::paint (juce::Graphics& g)
{
    if (directoryList.getNumFiles() == 0)
    {
        g.setFont (waive::Fonts::caption());
        auto* pal = waive::getWaivePalette (*this);
        g.setColour (pal ? pal->textMuted : juce::Colour (0xff808080));
        g.drawText ("No files in directory", getLocalBounds(), juce::Justification::centred, true);
    }
}

void LibraryComponent::resized()
{
    refreshTargetTrackList();
    auto bounds = getLocalBounds().reduced (waive::Spacing::sm);

    auto topRow = bounds.removeFromTop (waive::Spacing::controlHeightDefault);
    goUpButton.setBounds (topRow.removeFromLeft (waive::Spacing::controlHeightLarge));
    topRow.removeFromLeft (waive::Spacing::xs);
    addFavButton.setBounds (topRow.removeFromRight (waive::Spacing::controlHeightLarge));
    topRow.removeFromRight (waive::Spacing::xs);
    favoritesCombo.setBounds (topRow);

    bounds.removeFromTop (waive::Spacing::xs);
    auto targetRow = bounds.removeFromTop (waive::Spacing::controlHeightDefault);
    targetTrackLabel.setBounds (targetRow.removeFromLeft (80));
    targetRow.removeFromLeft (waive::Spacing::xs);
    targetTrackCombo.setBounds (targetRow);

    bounds.removeFromTop (waive::Spacing::xs);
    fileTree->setBounds (bounds);
}

bool LibraryComponent::selectTargetTrackForTesting (int trackIndex)
{
    refreshTargetTrackList();

    if (! juce::isPositiveAndBelow (trackIndex, targetTrackCombo.getNumItems()))
        return false;

    targetTrackCombo.setSelectedItemIndex (trackIndex, juce::dontSendNotification);
    return true;
}

int LibraryComponent::getSelectedTargetTrackIndexForTesting() const
{
    return targetTrackCombo.getSelectedItemIndex();
}

void LibraryComponent::fileDoubleClicked (const juce::File& file)
{
    refreshTargetTrackList();

    if (file.isDirectory())
    {
        setRoot (file);
        return;
    }

    if (! file.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Cannot Load File", "The selected file could not be loaded.");
        return;
    }

    auto& edit = editSession.getEdit();
    auto* track = getTargetTrack();
    if (track == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Cannot Load File", "No target track is available. Add a track first.");
        return;
    }
    auto transportPos = edit.getTransport().getPosition().inSeconds();

    te::AudioFile audioFile (edit.engine, file);
    auto duration = audioFile.getLength();

    if (duration <= 0.0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Cannot Load File", "The selected file format is not supported or could not be read.");
        return;
    }

    editSession.performEdit ("Insert Audio Clip", [&] (te::Edit&)
    {
        track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                               { { te::TimePosition::fromSeconds (transportPos),
                                   te::TimePosition::fromSeconds (transportPos + duration) },
                                 te::TimeDuration() },
                               false);
    });
}

void LibraryComponent::goUp()
{
    auto current = directoryList.getDirectory();
    auto parent = current.getParentDirectory();
    if (parent != current)
        setRoot (parent);
}

void LibraryComponent::setRoot (const juce::File& dir)
{
    if (dir.isDirectory())
        directoryList.setDirectory (dir, true, true);
}

void LibraryComponent::addCurrentToFavorites()
{
    auto dir = directoryList.getDirectory();
    if (! dir.isDirectory())
        return;

    auto path = dir.getFullPathName();
    if (favoritesPaths.contains (path))
        return;

    favoritesPaths.add (path);
    favoritesCombo.addItem (dir.getFileName(), favoritesPaths.size());
    saveFavorites();
}

void LibraryComponent::loadFavorites()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Waive";
    opts.filenameSuffix       = ".settings";
    opts.osxLibrarySubFolder = "Application Support/Waive";

    juce::ApplicationProperties props;
    props.setStorageParameters (opts);

    if (auto* settings = props.getUserSettings())
    {
        favoritesPaths.clear();
        favoritesPaths.addTokens (settings->getValue ("libraryFavorites"), "|", "");
        favoritesPaths.removeEmptyStrings();

        favoritesCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < favoritesPaths.size(); ++i)
        {
            juce::File f (favoritesPaths[i]);
            favoritesCombo.addItem (f.getFileName(), i + 1);
        }
    }
}

void LibraryComponent::saveFavorites()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Waive";
    opts.filenameSuffix       = ".settings";
    opts.osxLibrarySubFolder = "Application Support/Waive";

    juce::ApplicationProperties props;
    props.setStorageParameters (opts);

    if (auto* settings = props.getUserSettings())
    {
        settings->setValue ("libraryFavorites", favoritesPaths.joinIntoString ("|"));
        settings->saveIfNeeded();
    }
}

void LibraryComponent::refreshTargetTrackList()
{
    auto selectedTrackName = targetTrackCombo.getText();
    targetTrackCombo.clear (juce::dontSendNotification);

    auto tracks = te::getAudioTracks (editSession.getEdit());
    int selectedItemIndex = -1;
    int itemId = 1;

    for (auto* track : tracks)
    {
        if (track == nullptr)
            continue;

        auto trackName = track->getName().isNotEmpty()
                           ? track->getName()
                           : "Track " + juce::String (itemId);
        targetTrackCombo.addItem (trackName, itemId);

        if (trackName == selectedTrackName)
            selectedItemIndex = itemId - 1;

        ++itemId;
    }

    targetTrackCombo.setEnabled (targetTrackCombo.getNumItems() > 0);
    if (selectedItemIndex >= 0)
        targetTrackCombo.setSelectedItemIndex (selectedItemIndex, juce::dontSendNotification);
    else if (targetTrackCombo.getNumItems() > 0)
        targetTrackCombo.setSelectedItemIndex (0, juce::dontSendNotification);
}

te::AudioTrack* LibraryComponent::getTargetTrack() const
{
    auto selectedIndex = targetTrackCombo.getSelectedItemIndex();
    auto tracks = te::getAudioTracks (editSession.getEdit());

    if (! juce::isPositiveAndBelow (selectedIndex, tracks.size()))
        return nullptr;

    return tracks.getUnchecked (selectedIndex);
}
