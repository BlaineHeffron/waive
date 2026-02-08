#include "RenderDialog.h"
#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "WaiveSpacing.h"
#include "WaiveLookAndFeel.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

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
    sampleRateCombo.setSelectedId (2); // default 48kHz
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
    addAndMakeVisible (stemsToggle);

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
    setSize (600, 520);
}

RenderDialog::~RenderDialog() = default;

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

void RenderDialog::browseForOutputPath()
{
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

    auto outputFile = juce::File (outputPathEditor.getText());
    if (outputFile.getParentDirectory() == juce::File() || !outputFile.getParentDirectory().exists())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                 "Invalid Path",
                                                 "Output directory does not exist.");
        return;
    }

    rendering = true;
    renderButton.setEnabled (false);
    formatCombo.setEnabled (false);
    sampleRateCombo.setEnabled (false);
    bitDepthCombo.setEnabled (false);
    oggQualitySlider.setEnabled (false);
    rangeCombo.setEnabled (false);
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
        te::Edit* editPtr; // Safe to use pointer if we never delete Edit during render
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
    bool doStems = stemsToggle.getToggleState();
    bool normalize = normalizeToggle.getToggleState();
    double normalizeLevel = -0.1; // -0.1 dBFS

    RenderData data { formatId, sampleRate, bitDepth, oggQuality, range, tracksMask,
                      doStems, outputFile, normalize, normalizeLevel, &edit };

    juce::Thread::launch ([this, data]()
    {
        auto& editRef = *data.editPtr;
        bool success = false;

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
            auto outputDir = data.outputFile.getParentDirectory();
            auto ext = data.outputFile.getFileExtension();

            success = true;
            for (int i = 0; i < tracksToRender.size(); ++i)
            {
                juce::BigInteger singleMask;
                singleMask.setBit (tracksToRender[i]->getIndexInEditTrackList());

                auto stemFile = outputDir.getChildFile (juce::String::formatted ("%02d_%s%s",
                                                                                  i + 1,
                                                                                  tracksToRender[i]->getName().toRawUTF8(),
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
        }

        // Normalize if requested
        juce::File finalFile = data.outputFile;
        if (success && data.normalize)
        {
            juce::AudioFormatManager formatManager;
            formatManager.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (finalFile));
            if (reader)
            {
                // Find peak across all channels
                const int blockSize = 8192;
                juce::AudioBuffer<float> buffer ((int)reader->numChannels, blockSize);
                float maxPeak = 0.0f;

                for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += blockSize)
                {
                    auto numToRead = juce::jmin (blockSize, (int)(reader->lengthInSamples - pos));
                    reader->read (&buffer, 0, numToRead, pos, true, true);

                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    {
                        auto mag = buffer.getMagnitude (ch, 0, numToRead);
                        maxPeak = juce::jmax (maxPeak, mag);
                    }
                }

                // Apply normalization gain if peak exists
                if (maxPeak > 0.0001f)
                {
                    float targetLevel = juce::Decibels::decibelsToGain ((float)data.normalizeLevel);
                    float gain = targetLevel / maxPeak;

                    // Re-read and write with gain applied
                    reader.reset();
                    reader.reset (formatManager.createReaderFor (finalFile));

                    juce::File tempFile = finalFile.getSiblingFile (finalFile.getFileNameWithoutExtension() + "_temp" + finalFile.getFileExtension());

                    std::unique_ptr<juce::AudioFormat> writeFormat;
                    switch (data.formatId)
                    {
                        case 1: writeFormat = std::make_unique<juce::WavAudioFormat>(); break;
                        case 2: writeFormat = std::make_unique<juce::FlacAudioFormat>(); break;
                        case 3: writeFormat = std::make_unique<juce::OggVorbisAudioFormat>(); break;
                        default: writeFormat = std::make_unique<juce::WavAudioFormat>(); break;
                    }

                    std::unique_ptr<juce::FileOutputStream> outStream = std::make_unique<juce::FileOutputStream> (tempFile);
                    if (outStream->openedOk())
                    {
                        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")
                        std::unique_ptr<juce::AudioFormatWriter> writer (writeFormat->createWriterFor (
                            outStream.release(), data.sampleRate, reader->numChannels, data.bitDepth,
                            reader->metadataValues, (data.formatId == 3) ? (int)(data.oggQuality * 10) : 0));
                        JUCE_END_IGNORE_WARNINGS_GCC_LIKE

                        if (writer)
                        {
                            for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += blockSize)
                            {
                                auto numToRead = juce::jmin (blockSize, (int)(reader->lengthInSamples - pos));
                                reader->read (&buffer, 0, numToRead, pos, true, true);
                                buffer.applyGain (0, numToRead, gain);
                                writer->writeFromAudioSampleBuffer (buffer, 0, numToRead);
                            }

                            writer.reset();
                            reader.reset();

                            // Replace original with normalized
                            if (tempFile.moveFileTo (finalFile))
                            {
                                // Success - temp file replaced original
                            }
                            else
                            {
                                // Restore original by deleting temp
                                tempFile.deleteFile();
                            }
                        }
                    }
                }
            }
        }

        juce::MessageManager::callAsync ([this, success, finalFile]
        {
            progressBar.setVisible (false);

            if (success)
            {
                auto size = finalFile.getSize();
                auto sizeStr = size < 1024 * 1024
                               ? juce::String (size / 1024) + " KB"
                               : juce::String (size / (1024 * 1024)) + " MB";

                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                                                         "Render Complete",
                                                         "File: " + finalFile.getFullPathName() + "\nSize: " + sizeStr);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                         "Render Failed",
                                                         "Failed to render audio.");
            }

            resetControls();
        });
    });
}

void RenderDialog::resetControls()
{
    rendering = false;
    renderButton.setEnabled (true);
    formatCombo.setEnabled (true);
    sampleRateCombo.setEnabled (true);
    bitDepthCombo.setEnabled (true);
    oggQualitySlider.setEnabled (true);
    rangeCombo.setEnabled (true);
    normalizeToggle.setEnabled (true);
    mixdownToggle.setEnabled (true);
    stemsToggle.setEnabled (true);
    outputPathEditor.setEnabled (true);
    browseButton.setEnabled (true);
}
