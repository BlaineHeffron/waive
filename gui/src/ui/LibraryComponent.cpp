#include "LibraryComponent.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

//==============================================================================
LibraryComponent::LibraryComponent (EditSession& session)
    : editSession (session),
      fileFilter ("*.wav;*.aiff;*.flac;*.mp3;*.ogg", "*", "Audio Files"),
      directoryList (&fileFilter, scanThread)
{
    scanThread.startThread (juce::Thread::Priority::background);

    auto homeDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    directoryList.setDirectory (homeDir, true, true);

    fileTree = std::make_unique<juce::FileTreeComponent> (directoryList);
    fileTree->addListener (this);
    fileTree->setDragAndDropDescription ("LibraryFile");
    fileTree->setTitle ("File Browser");
    fileTree->setDescription ("Browse and drag audio files");
    addAndMakeVisible (fileTree.get());

    goUpButton.setButtonText ("..");
    goUpButton.onClick = [this] { goUp(); };
    goUpButton.setTitle ("Go Up");
    goUpButton.setDescription ("Navigate to parent directory");
    goUpButton.setWantsKeyboardFocus (true);
    addAndMakeVisible (goUpButton);

    addFavButton.setButtonText ("+");
    addFavButton.onClick = [this] { addCurrentToFavorites(); };
    addFavButton.setTitle ("Add Favorite");
    addFavButton.setDescription ("Add current directory to favorites");
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
    favoritesCombo.setWantsKeyboardFocus (true);
    addAndMakeVisible (favoritesCombo);

    loadFavorites();
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
        g.setFont (waive::Fonts::body());
        if (auto* pal = waive::getWaivePalette (*this))
            g.setColour (pal->textMuted);
        else
            g.setColour (juce::Colours::grey);
        g.drawText ("Click '+ Folder' to add a media directory", getLocalBounds(), juce::Justification::centred, true);
    }
}

void LibraryComponent::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    auto topRow = bounds.removeFromTop (28);
    goUpButton.setBounds (topRow.removeFromLeft (32));
    topRow.removeFromLeft (4);
    addFavButton.setBounds (topRow.removeFromRight (32));
    topRow.removeFromRight (4);
    favoritesCombo.setBounds (topRow);

    bounds.removeFromTop (4);
    fileTree->setBounds (bounds);
}

void LibraryComponent::fileDoubleClicked (const juce::File& file)
{
    if (file.isDirectory())
    {
        setRoot (file);
        return;
    }

    if (! file.existsAsFile())
        return;

    auto& edit = editSession.getEdit();
    auto tracks = te::getAudioTracks (edit);
    if (tracks.isEmpty())
        return;

    auto* track = tracks.getFirst();
    auto transportPos = edit.getTransport().getPosition().inSeconds();

    te::AudioFile audioFile (edit.engine, file);
    auto duration = audioFile.getLength();

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
