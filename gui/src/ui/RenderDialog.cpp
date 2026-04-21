#include "RenderDialog.h"
#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "WaiveSpacing.h"
#include "WaiveLookAndFeel.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>

namespace te = tracktion;

namespace
{

std::unique_ptr<te::Edit> createSnapshotEditForRender (te::Engine& engine,
                                                       const juce::ValueTree& state,
                                                       const juce::File& editFile)
{
    auto projectItemId = te::ProjectItemID::fromProperty (state, te::IDs::projectID);
    if (! projectItemId.isValid())
        projectItemId = te::ProjectItemID::createNewID (0);

    return te::Edit::createEdit (te::Edit::Options
    {
        engine,
        state.createCopy(),
        projectItemId,
        te::Edit::forEditing,
        nullptr,
        te::Edit::getDefaultNumUndoLevels(),
        [editFile] { return editFile; },
        {},
        0
    });
}

double getProjectSampleRate (te::Edit& edit)
{
    if (auto* device = edit.engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();

    return edit.engine.getDeviceManager().getSampleRate();
}

juce::String sanitiseStemFileComponent (const juce::String& name)
{
    auto safe = name.replaceCharacters ("\\/:*?\"<>|", "________");
    safe = safe.trim();
    return safe.isEmpty() ? "Track" : safe;
}

bool normaliseRenderedFile (const juce::File& targetFile,
                            int formatId,
                            double sampleRate,
                            int bitDepth,
                            double oggQuality,
                            double normalizeLevelDb)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (targetFile));
    if (reader == nullptr)
        return false;

    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, blockSize);
    float maxPeak = 0.0f;

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += blockSize)
    {
        auto numToRead = juce::jmin (blockSize, (int) (reader->lengthInSamples - pos));
        reader->read (&buffer, 0, numToRead, pos, true, true);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            maxPeak = juce::jmax (maxPeak, buffer.getMagnitude (ch, 0, numToRead));
    }

    if (maxPeak <= 0.0001f)
        return true;

    const float targetLevel = juce::Decibels::decibelsToGain ((float) normalizeLevelDb);
    const float gain = targetLevel / maxPeak;

    reader.reset (formatManager.createReaderFor (targetFile));
    if (reader == nullptr)
        return false;

    const auto tempFile = targetFile.getSiblingFile (targetFile.getFileNameWithoutExtension()
                                                     + "_temp"
                                                     + targetFile.getFileExtension());

    std::unique_ptr<juce::AudioFormat> writeFormat;
    switch (formatId)
    {
        case 1: writeFormat = std::make_unique<juce::WavAudioFormat>(); break;
        case 2: writeFormat = std::make_unique<juce::FlacAudioFormat>(); break;
        case 3: writeFormat = std::make_unique<juce::OggVorbisAudioFormat>(); break;
        default: writeFormat = std::make_unique<juce::WavAudioFormat>(); break;
    }

    auto outStream = std::make_unique<juce::FileOutputStream> (tempFile);
    if (! outStream->openedOk())
        return false;

    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")
    std::unique_ptr<juce::AudioFormatWriter> writer (writeFormat->createWriterFor (
        outStream.release(), sampleRate, reader->numChannels, bitDepth,
        reader->metadataValues, (formatId == 3) ? (int) (oggQuality * 10) : 0));
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE

    if (writer == nullptr)
        return false;

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += blockSize)
    {
        auto numToRead = juce::jmin (blockSize, (int) (reader->lengthInSamples - pos));
        reader->read (&buffer, 0, numToRead, pos, true, true);
        buffer.applyGain (0, numToRead, gain);
        writer->writeFromAudioSampleBuffer (buffer, 0, numToRead);
    }

    writer.reset();
    reader.reset();

    if (targetFile.existsAsFile() && ! targetFile.deleteFile())
    {
        tempFile.deleteFile();
        return false;
    }

    if (! tempFile.moveFileTo (targetFile))
    {
        tempFile.deleteFile();
        return false;
    }

    return true;
}

}

//==============================================================================
RenderDialog::RenderDialog (EditSession& session, UndoableCommandHandler& handler)
    : editSession (session), commandHandler (handler), progressBar (progressValue)
{
    // Format
    formatLabel.setText ("Format:", juce::dontSendNotification);
    addAndMakeVisible (formatLabel);

    formatCombo.addItem ("WAV", 1);
    formatCombo.addItem ("FLAC", 2);
    formatCombo.addItem ("OGG Vorbis", 3);
    formatCombo.setSelectedId (1);
    formatCombo.onChange = [this] { updateFileExtension(); updateFormatOptions(); };
    addAndMakeVisible (formatCombo);

    // Sample rate
    sampleRateLabel.setText ("Sample Rate:", juce::dontSendNotification);
    addAndMakeVisible (sampleRateLabel);

    sampleRateCombo.addItem ("44100 Hz", 1);
    sampleRateCombo.addItem ("48000 Hz", 2);
    sampleRateCombo.addItem ("88200 Hz", 3);
    sampleRateCombo.addItem ("96000 Hz", 4);
    const auto projectSampleRate = getProjectSampleRate (editSession.getEdit());
    if (std::abs (projectSampleRate - 44100.0) < 1.0)
        sampleRateCombo.setSelectedId (1);
    else if (std::abs (projectSampleRate - 48000.0) < 1.0)
        sampleRateCombo.setSelectedId (2);
    else if (std::abs (projectSampleRate - 88200.0) < 1.0)
        sampleRateCombo.setSelectedId (3);
    else if (std::abs (projectSampleRate - 96000.0) < 1.0)
        sampleRateCombo.setSelectedId (4);
    else
        sampleRateCombo.setSelectedId (2);
    addAndMakeVisible (sampleRateCombo);

    // Bit depth
    bitDepthLabel.setText ("Bit Depth:", juce::dontSendNotification);
    addAndMakeVisible (bitDepthLabel);

    bitDepthCombo.addItem ("16-bit", 1);
    bitDepthCombo.addItem ("24-bit", 2);
    bitDepthCombo.addItem ("32-bit float", 3);
    bitDepthCombo.setSelectedId (2); // default 24-bit
    addAndMakeVisible (bitDepthCombo);

    // OGG quality
    oggQualityLabel.setText ("OGG Quality:", juce::dontSendNotification);
    addAndMakeVisible (oggQualityLabel);

    oggQualitySlider.setRange (0.0, 1.0, 0.01);
    oggQualitySlider.setValue (0.6);
    oggQualitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    addAndMakeVisible (oggQualitySlider);

    // Range
    rangeLabel.setText ("Range:", juce::dontSendNotification);
    addAndMakeVisible (rangeLabel);

    rangeCombo.addItem ("Entire Project", 1);
    rangeCombo.addItem ("Loop Region", 2);
    rangeCombo.addItem ("Custom", 3);
    rangeCombo.setSelectedId (1);
    rangeCombo.onChange = [this]
    {
        bool custom = rangeCombo.getSelectedId() == 3;
        startLabel.setVisible (custom);
        startEditor.setVisible (custom);
        endLabel.setVisible (custom);
        endEditor.setVisible (custom);
    };
    addAndMakeVisible (rangeCombo);

    // Custom range editors
    startLabel.setText ("Start:", juce::dontSendNotification);
    startEditor.setText ("0.0");
    endLabel.setText ("End:", juce::dontSendNotification);
    endEditor.setText ("10.0");
    addAndMakeVisible (startLabel);
    addAndMakeVisible (startEditor);
    addAndMakeVisible (endLabel);
    addAndMakeVisible (endEditor);
    startLabel.setVisible (false);
    startEditor.setVisible (false);
    endLabel.setVisible (false);
    endEditor.setVisible (false);

    // Normalize
    normalizeToggle.setButtonText ("Normalize to -0.1 dBFS");
    addAndMakeVisible (normalizeToggle);

    // Mixdown vs stems
    mixdownToggle.setButtonText ("Mixdown (single file)");
    mixdownToggle.setRadioGroupId (1);
    mixdownToggle.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (mixdownToggle);

    stemsToggle.setButtonText ("Stems (one file per track)");
    stemsToggle.setRadioGroupId (1);
    stemsToggle.onClick = [this] { updateOutputMode(); };
    addAndMakeVisible (stemsToggle);
    mixdownToggle.onClick = [this] { updateOutputMode(); };

    // Output path
    outputPathLabel.setText ("Output:", juce::dontSendNotification);
    addAndMakeVisible (outputPathLabel);

    outputPathEditor.setText (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                  .getChildFile ("Untitled.wav").getFullPathName());
    addAndMakeVisible (outputPathEditor);

    browseButton.setButtonText ("Browse...");
    browseButton.onClick = [this] { browseForOutputPath(); };
    addAndMakeVisible (browseButton);

    // Render button
    renderButton.setButtonText ("Render");
    renderButton.onClick = [this] { performRender(); };
    addAndMakeVisible (renderButton);

    // Progress bar
    addAndMakeVisible (progressBar);
    progressBar.setVisible (false);

    updateFormatOptions();
    updateOutputMode();
    setSize (600, 520);
}

RenderDialog::~RenderDialog()
{
    if (renderThread.joinable())
        renderThread.join();
}

int RenderDialog::getSelectedFormatForTesting() const
{
    return formatCombo.getSelectedId();
}

void RenderDialog::selectFormatForTesting (int formatId)
{
    formatCombo.setSelectedId (formatId, juce::sendNotificationSync);
}

int RenderDialog::getSelectedRangeForTesting() const
{
    return rangeCombo.getSelectedId();
}

void RenderDialog::selectRangeForTesting (int rangeId)
{
    rangeCombo.setSelectedId (rangeId, juce::sendNotificationSync);
}

bool RenderDialog::isBitDepthVisibleForTesting() const
{
    return bitDepthCombo.isVisible();
}

bool RenderDialog::isOggQualityVisibleForTesting() const
{
    return oggQualitySlider.isVisible();
}

bool RenderDialog::isCustomRangeVisibleForTesting() const
{
    return startEditor.isVisible() && endEditor.isVisible();
}

void RenderDialog::setStemsModeForTesting (bool shouldRenderStems)
{
    if (shouldRenderStems != stemsToggle.getToggleState())
        (shouldRenderStems ? stemsToggle : mixdownToggle).triggerClick();
}

juce::String RenderDialog::getOutputPathForTesting() const
{
    return outputPathEditor.getText();
}

juce::String RenderDialog::getOutputLabelTextForTesting() const
{
    return outputPathLabel.getText();
}

void RenderDialog::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::md);

    auto row = [&bounds] (int height)
    {
        auto r = bounds.removeFromTop (height);
        bounds.removeFromTop (waive::Spacing::sm);
        return r;
    };

    // Format
    auto formatRow = row (waive::Spacing::controlHeightDefault);
    formatLabel.setBounds (formatRow.removeFromLeft (120));
    formatRow.removeFromLeft (waive::Spacing::sm);
    formatCombo.setBounds (formatRow);

    // Sample rate
    auto srRow = row (waive::Spacing::controlHeightDefault);
    sampleRateLabel.setBounds (srRow.removeFromLeft (120));
    srRow.removeFromLeft (waive::Spacing::sm);
    sampleRateCombo.setBounds (srRow);

    // Bit depth (conditional)
    auto bdRow = row (waive::Spacing::controlHeightDefault);
    bitDepthLabel.setBounds (bdRow.removeFromLeft (120));
    bdRow.removeFromLeft (waive::Spacing::sm);
    bitDepthCombo.setBounds (bdRow);

    // OGG quality (conditional)
    auto oggRow = row (waive::Spacing::controlHeightDefault);
    oggQualityLabel.setBounds (oggRow.removeFromLeft (120));
    oggRow.removeFromLeft (waive::Spacing::sm);
    oggQualitySlider.setBounds (oggRow);

    // Range
    auto rangeRow = row (waive::Spacing::controlHeightDefault);
    rangeLabel.setBounds (rangeRow.removeFromLeft (120));
    rangeRow.removeFromLeft (waive::Spacing::sm);
    rangeCombo.setBounds (rangeRow);

    // Custom range (conditional)
    auto startRow = row (waive::Spacing::controlHeightDefault);
    startLabel.setBounds (startRow.removeFromLeft (120));
    startRow.removeFromLeft (waive::Spacing::sm);
    startEditor.setBounds (startRow);

    auto endRow = row (waive::Spacing::controlHeightDefault);
    endLabel.setBounds (endRow.removeFromLeft (120));
    endRow.removeFromLeft (waive::Spacing::sm);
    endEditor.setBounds (endRow);

    // Normalize
    row (waive::Spacing::controlHeightDefault).removeFromLeft (120); // spacer
    normalizeToggle.setBounds (row (waive::Spacing::controlHeightDefault));

    // Mixdown/stems
    mixdownToggle.setBounds (row (waive::Spacing::controlHeightDefault));
    stemsToggle.setBounds (row (waive::Spacing::controlHeightDefault));

    // Output path
    auto pathRow = row (waive::Spacing::controlHeightDefault);
    outputPathLabel.setBounds (pathRow.removeFromLeft (120));
    pathRow.removeFromLeft (waive::Spacing::sm);
    browseButton.setBounds (pathRow.removeFromRight (100));
    pathRow.removeFromRight (waive::Spacing::sm);
    outputPathEditor.setBounds (pathRow);

    // Render button
    bounds.removeFromTop (waive::Spacing::md);
    renderButton.setBounds (row (waive::Spacing::controlHeightLarge));

    // Progress
    progressBar.setBounds (row (waive::Spacing::controlHeightDefault));
}

void RenderDialog::updateFileExtension()
{
    if (stemsToggle.getToggleState())
        return;

    auto path = outputPathEditor.getText();
    auto file = juce::File (path);
    auto baseName = file.getFileNameWithoutExtension();
    auto parent = file.getParentDirectory();

    juce::String newExt;
    switch (formatCombo.getSelectedId())
    {
        case 1: newExt = ".wav"; break;
        case 2: newExt = ".flac"; break;
        case 3: newExt = ".ogg"; break;
        default: newExt = ".wav"; break;
    }

    outputPathEditor.setText (parent.getChildFile (baseName + newExt).getFullPathName());
}

void RenderDialog::updateFormatOptions()
{
    bool isOgg = formatCombo.getSelectedId() == 3;

    bitDepthLabel.setVisible (!isOgg);
    bitDepthCombo.setVisible (!isOgg);

    oggQualityLabel.setVisible (isOgg);
    oggQualitySlider.setVisible (isOgg);
}

void RenderDialog::updateOutputMode()
{
    const bool stemsMode = stemsToggle.getToggleState();
    outputPathLabel.setText (stemsMode ? "Output Dir:" : "Output:", juce::dontSendNotification);

    auto currentPath = outputPathEditor.getText().trim();
    auto currentFile = currentPath.isNotEmpty() ? juce::File (currentPath) : juce::File();

    if (stemsMode)
    {
        auto outputDir = currentFile;
        if (outputDir == juce::File() || (! outputDir.isDirectory() && outputDir.getFileExtension().isNotEmpty()))
            outputDir = currentFile.getParentDirectory();

        if (outputDir == juce::File())
            outputDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        outputPathEditor.setText (outputDir.getFullPathName(), juce::dontSendNotification);
        return;
    }

    auto parentDir = currentFile;
    if (! parentDir.isDirectory())
        parentDir = currentFile.getParentDirectory();

    if (parentDir == juce::File())
        parentDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    auto fileName = currentFile.getFileName();
    if (fileName.isEmpty() || currentFile.getFileExtension().isEmpty())
        fileName = "Untitled.wav";

    outputPathEditor.setText (parentDir.getChildFile (fileName).getFullPathName(), juce::dontSendNotification);
    updateFileExtension();
}

void RenderDialog::browseForOutputPath()
{
    if (stemsToggle.getToggleState())
    {
        auto startDir = juce::File (outputPathEditor.getText());
        if (startDir == juce::File() || ! startDir.exists())
            startDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        juce::FileChooser chooser ("Choose Stem Output Directory...", startDir);
        if (chooser.browseForDirectory())
            outputPathEditor.setText (chooser.getResult().getFullPathName());
        return;
    }

    juce::String wildcards;
    switch (formatCombo.getSelectedId())
    {
        case 1: wildcards = "*.wav"; break;
        case 2: wildcards = "*.flac"; break;
        case 3: wildcards = "*.ogg"; break;
        default: wildcards = "*.wav"; break;
    }

    juce::FileChooser chooser ("Save Render As...", juce::File (outputPathEditor.getText()), wildcards);
    if (chooser.browseForFileToSave (true))
    {
        outputPathEditor.setText (chooser.getResult().getFullPathName());
    }
}

void RenderDialog::performRender()
{
    if (rendering)
        return;

    const bool doStems = stemsToggle.getToggleState();
    auto outputTarget = juce::File (outputPathEditor.getText());
    auto outputDirectory = doStems ? outputTarget : outputTarget.getParentDirectory();

    if (outputDirectory == juce::File() || ! outputDirectory.exists())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                 "Invalid Path",
                                                 doStems ? "Output directory does not exist."
                                                         : "Output file directory does not exist.");
        return;
    }

    rendering = true;
    progressValue = -1.0;
    renderButton.setEnabled (false);
    formatCombo.setEnabled (false);
    sampleRateCombo.setEnabled (false);
    bitDepthCombo.setEnabled (false);
    oggQualitySlider.setEnabled (false);
    rangeCombo.setEnabled (false);
    startEditor.setEnabled (false);
    endEditor.setEnabled (false);
    normalizeToggle.setEnabled (false);
    mixdownToggle.setEnabled (false);
    stemsToggle.setEnabled (false);
    outputPathEditor.setEnabled (false);
    browseButton.setEnabled (false);
    progressBar.setVisible (true);

    // Capture all needed data BEFORE launching thread (no main-thread object access from thread)
    struct RenderData
    {
        int formatId;
        double sampleRate;
        int bitDepth;
        double oggQuality;
        te::TimeRange range;
        juce::BigInteger tracksMask;
        bool doStems;
        juce::File outputFile;
        bool normalize;
        double normalizeLevel;
        juce::ValueTree editState;
        juce::File editFile;
        te::Engine* enginePtr;
    };

    // Determine format
    int formatId = formatCombo.getSelectedId();

    // Sample rate
    double sampleRate = 48000.0;
    switch (sampleRateCombo.getSelectedId())
    {
        case 1: sampleRate = 44100.0; break;
        case 2: sampleRate = 48000.0; break;
        case 3: sampleRate = 88200.0; break;
        case 4: sampleRate = 96000.0; break;
    }

    // Bit depth
    int bitDepth = 24;
    switch (bitDepthCombo.getSelectedId())
    {
        case 1: bitDepth = 16; break;
        case 2: bitDepth = 24; break;
        case 3: bitDepth = 32; break; // float
    }

    double oggQuality = oggQualitySlider.getValue();

    // Range
    auto& edit = editSession.getEdit();
    te::TimeRange range (te::TimePosition::fromSeconds (0.0), edit.getLength());

    if (rangeCombo.getSelectedId() == 2) // Loop region
    {
        auto& transport = edit.getTransport();
        range = te::TimeRange (transport.loopPoint1, transport.loopPoint2);
    }
    else if (rangeCombo.getSelectedId() == 3) // Custom
    {
        double start = startEditor.getText().getDoubleValue();
        double end = endEditor.getText().getDoubleValue();

        if (end <= start)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                      "Invalid Time Range",
                                                      "End time must be greater than start time.",
                                                      "OK",
                                                      this);
            resetControls();
            return;
        }

        range = te::TimeRange (te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (end));
    }

    // Track mask using consistent indexing
    juce::BigInteger tracksMask;
    auto tracks = te::getAudioTracks (edit);
    for (int i = 0; i < tracks.size(); ++i)
        tracksMask.setBit (tracks[i]->getIndexInEditTrackList());

    // Mixdown vs stems
    bool normalize = normalizeToggle.getToggleState();
    double normalizeLevel = -0.1; // -0.1 dBFS

    if (renderThread.joinable())
        renderThread.join();

    RenderData data { formatId, sampleRate, bitDepth, oggQuality, range, tracksMask,
                      doStems, outputTarget, normalize, normalizeLevel,
                      edit.state.createCopy(),
                      te::EditFileOperations (edit).getEditFile(),
                      &edit.engine };

    auto safeThis = juce::Component::SafePointer<RenderDialog> (this);
    renderThread = std::thread ([safeThis, data]() mutable
    {
        auto snapshotEdit = createSnapshotEditForRender (*data.enginePtr, data.editState, data.editFile);
        if (snapshotEdit == nullptr)
        {
            juce::MessageManager::callAsync ([safeThis]
            {
                if (safeThis == nullptr)
                    return;

                safeThis->progressValue = 0.0;
                safeThis->progressBar.setVisible (false);
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                         "Render Failed",
                                                         "Failed to create a render snapshot.");
                safeThis->resetControls();
            });
            return;
        }

        auto& editRef = *snapshotEdit;
        bool success = false;
        juce::Array<juce::File> renderedFiles;

        // Create AudioFormat pointer
        std::unique_ptr<juce::AudioFormat> format;
        switch (data.formatId)
        {
            case 1: format = std::make_unique<juce::WavAudioFormat>(); break;
            case 2: format = std::make_unique<juce::FlacAudioFormat>(); break;
            case 3: format = std::make_unique<juce::OggVorbisAudioFormat>(); break;
            default: format = std::make_unique<juce::WavAudioFormat>(); break;
        }

        if (data.doStems)
        {
            // Render stems
            auto tracksToRender = te::getAudioTracks (editRef);
            auto outputDir = data.outputFile;
            auto ext = data.outputFile.getFileExtension();
            if (ext.isEmpty())
            {
                switch (data.formatId)
                {
                    case 2: ext = ".flac"; break;
                    case 3: ext = ".ogg"; break;
                    default: ext = ".wav"; break;
                }
            }

            success = true;
            for (int i = 0; i < tracksToRender.size(); ++i)
            {
                juce::BigInteger singleMask;
                singleMask.setBit (tracksToRender[i]->getIndexInEditTrackList());

                auto stemFile = outputDir.getChildFile (juce::String::formatted ("%02d_%s%s",
                                                                                  i + 1,
                                                                                  sanitiseStemFileComponent (tracksToRender[i]->getName()).toRawUTF8(),
                                                                                  ext.toRawUTF8()));

                // Create Parameters with format settings
                te::Renderer::Parameters params (editRef);
                params.destFile = stemFile;
                params.audioFormat = format.get();
                params.bitDepth = data.bitDepth;
                params.sampleRateForAudio = data.sampleRate;
                params.quality = (data.formatId == 3) ? (int)(data.oggQuality * 10) : 0;
                params.time = data.range;
                params.tracksToDo = singleMask;
                params.usePlugins = true;
                params.useMasterPlugins = true;

                auto result = te::Renderer::renderToFile ("Rendering stem " + tracksToRender[i]->getName(), params);
                if (result == juce::File())
                {
                    success = false;
                    break; // Stop rendering on first failure
                }

                renderedFiles.add (result);
            }
        }
        else
        {
            // Mixdown
            te::Renderer::Parameters params (editRef);
            params.destFile = data.outputFile;
            params.audioFormat = format.get();
            params.bitDepth = data.bitDepth;
            params.sampleRateForAudio = data.sampleRate;
            params.quality = (data.formatId == 3) ? (int)(data.oggQuality * 10) : 0;
            params.time = data.range;
            params.tracksToDo = data.tracksMask;
            params.usePlugins = true;
            params.useMasterPlugins = true;

            auto result = te::Renderer::renderToFile ("Rendering mixdown", params);
            success = (result != juce::File());
            if (success)
                renderedFiles.add (result);
        }

        if (success && data.normalize)
        {
            for (auto& renderedFile : renderedFiles)
            {
                if (! normaliseRenderedFile (renderedFile,
                                             data.formatId,
                                             data.sampleRate,
                                             data.bitDepth,
                                             data.oggQuality,
                                             data.normalizeLevel))
                {
                    success = false;
                    break;
                }
            }
        }

        const auto finalFile = renderedFiles.isEmpty() ? data.outputFile : renderedFiles.getFirst();
        const auto renderedCount = renderedFiles.size();
        const auto outputDir = data.doStems ? data.outputFile.getFullPathName()
                                            : finalFile.getParentDirectory().getFullPathName();
        juce::MessageManager::callAsync ([safeThis, success, finalFile, renderedCount, outputDir]
        {
            if (safeThis == nullptr)
                return;

            safeThis->progressValue = 0.0;
            safeThis->progressBar.setVisible (false);

            if (success)
            {
                juce::String message;
                if (renderedCount > 1)
                {
                    message = "Rendered " + juce::String (renderedCount)
                              + " stem files to:\n" + outputDir;
                }
                else
                {
                    auto size = finalFile.getSize();
                    auto sizeStr = size < 1024 * 1024
                                   ? juce::String (size / 1024) + " KB"
                                   : juce::String (size / (1024 * 1024)) + " MB";
                    message = "File: " + finalFile.getFullPathName() + "\nSize: " + sizeStr;
                }

                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                                                         "Render Complete",
                                                         message);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                         "Render Failed",
                                                         "Failed to render audio.");
            }

            safeThis->resetControls();
        });
    });
}

void RenderDialog::resetControls()
{
    rendering = false;
    progressValue = 0.0;
    progressBar.setVisible (false);
    renderButton.setEnabled (true);
    formatCombo.setEnabled (true);
    sampleRateCombo.setEnabled (true);
    bitDepthCombo.setEnabled (true);
    oggQualitySlider.setEnabled (true);
    rangeCombo.setEnabled (true);
    startEditor.setEnabled (true);
    endEditor.setEnabled (true);
    normalizeToggle.setEnabled (true);
    mixdownToggle.setEnabled (true);
    stemsToggle.setEnabled (true);
    outputPathEditor.setEnabled (true);
    browseButton.setEnabled (true);
}
