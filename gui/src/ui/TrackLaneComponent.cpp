#include "TrackLaneComponent.h"
#include "ClipComponent.h"
#include "TimelineComponent.h"
#include "WaiveFonts.h"
#include "WaiveLookAndFeel.h"
#include "WaiveSpacing.h"

#include <cmath>
#include <functional>

namespace
{
float toNormalised (float value, const juce::Range<float>& range)
{
    if (range.getLength() <= 0.0f)
        return 0.0f;

    return juce::jlimit (0.0f, 1.0f,
                         (value - range.getStart()) / range.getLength());
}

bool isTrackInsideFolderSubtree (te::Track& candidateFolder, te::Track& trackToMove)
{
    for (auto* parent = candidateFolder.getParentFolderTrack(); parent != nullptr; parent = parent->getParentFolderTrack())
        if (parent == &trackToMove)
            return true;

    return false;
}
}

//==============================================================================
TrackLaneComponent::TrackLaneComponent (te::Track& t, TimelineComponent& tl, int trackIdx, int indentDepth)
    : track (t),
      audioTrack (dynamic_cast<te::AudioTrack*> (&t)),
      folderTrack (dynamic_cast<te::FolderTrack*> (&t)),
      timeline (tl),
      trackIndex (trackIdx),
      depth (indentDepth)
{
    headerLabel.setText (track.getName(), juce::dontSendNotification);
    headerLabel.setJustificationType (juce::Justification::centredLeft);
    headerLabel.setEditable (false, true, false);
    headerLabel.onTextChange = [this]
    {
        timeline.getEditSession().performEdit ("Rename Track", [this] (te::Edit&)
        {
            track.setName (headerLabel.getText());
        });
    };
    addAndMakeVisible (headerLabel);

    automationParamCombo.setTextWhenNothingSelected ("Automation: none");
    automationParamCombo.onChange = [this] { repaint(); };
    automationParamCombo.setTooltip ("Select automation parameter to display");
    addChildComponent (automationParamCombo);

    folderToggleButton.onClick = [this]
    {
        if (folderTrack != nullptr)
            timeline.toggleFolderCollapsed (track.itemID);
    };
    addChildComponent (folderToggleButton);

    folderSoloButton.onClick = [this]
    {
        if (folderTrack == nullptr)
            return;

        const auto newState = folderSoloButton.getToggleState();
        timeline.getEditSession().performEdit ("Toggle Folder Solo", [this, newState] (te::Edit&)
        {
            std::function<void (te::FolderTrack&)> applySolo = [&] (te::FolderTrack& folder)
            {
                folder.setSolo (newState);
                for (auto* child : folder.getAllSubTracks (false))
                {
                    if (child == nullptr)
                        continue;

                    child->setSolo (newState);
                    if (auto* childFolder = dynamic_cast<te::FolderTrack*> (child))
                        applySolo (*childFolder);
                }
            };

            applySolo (*folderTrack);
        });
    };
    addChildComponent (folderSoloButton);

    folderMuteButton.onClick = [this]
    {
        if (folderTrack == nullptr)
            return;

        const auto newState = folderMuteButton.getToggleState();
        timeline.getEditSession().performEdit ("Toggle Folder Mute", [this, newState] (te::Edit&)
        {
            std::function<void (te::FolderTrack&)> applyMute = [&] (te::FolderTrack& folder)
            {
                folder.setMute (newState);
                for (auto* child : folder.getAllSubTracks (false))
                {
                    if (child == nullptr)
                        continue;

                    child->setMute (newState);
                    if (auto* childFolder = dynamic_cast<te::FolderTrack*> (child))
                        applyMute (*childFolder);
                }
            };

            applyMute (*folderTrack);
        });
    };
    addChildComponent (folderMuteButton);

    folderVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    folderVolumeSlider.setRange (-60.0, 6.0, 0.1);
    folderVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    folderVolumeSlider.onValueChange = [this]
    {
        if (folderTrack == nullptr)
            return;

        const auto db = (float) folderVolumeSlider.getValue();
        timeline.getEditSession().performEdit ("Set Folder Volume", true, [this, db] (te::Edit&)
        {
            auto plugins = folderTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
            if (! plugins.isEmpty())
                plugins.getFirst()->volParam->setParameter (te::decibelsToVolumeFaderPosition (db),
                                                            juce::sendNotification);
        });
    };
    folderVolumeSlider.onDragEnd = [this] { timeline.getEditSession().endCoalescedTransaction(); };
    addChildComponent (folderVolumeSlider);

    if (audioTrack != nullptr)
    {
        automationParamCombo.setVisible (true);
        refreshAutomationParams();
        updateClips();
    }
    else if (folderTrack != nullptr)
    {
        folderToggleButton.setVisible (true);
        folderSoloButton.setVisible (true);
        folderMuteButton.setVisible (true);
        folderVolumeSlider.setVisible (true);
    }

    setTitle (track.getName());
    setDescription ("Track lane - contains clips and automation for " + track.getName());
    setWantsKeyboardFocus (true);
}

TrackLaneComponent::~TrackLaneComponent() = default;

void TrackLaneComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto* pal = waive::getWaivePalette (*this);

    auto headerBounds = bounds.removeFromLeft (TimelineComponent::trackHeaderWidth);
    auto headerColour = pal ? pal->surfaceBg : juce::Colour (0xff2a2a2a);
    if (isHeaderHovered)
        headerColour = headerColour.brighter (0.1f);
    g.setColour (headerColour);
    g.fillRect (headerBounds);

    if (pal)
    {
        const juce::Colour* trackColors[] = {
            &pal->trackColor1, &pal->trackColor2, &pal->trackColor3, &pal->trackColor4,
            &pal->trackColor5, &pal->trackColor6, &pal->trackColor7, &pal->trackColor8,
            &pal->trackColor9, &pal->trackColor10, &pal->trackColor11, &pal->trackColor12
        };
        g.setColour (*trackColors[track.getIndexInEditTrackList() % 12]);
        g.fillRect (0, 0, 4, getHeight());
    }

    if (depth > 0)
    {
        g.setColour ((pal ? pal->borderSubtle : juce::Colours::darkgrey).withAlpha (0.6f));
        for (int i = 0; i < depth; ++i)
            g.drawVerticalLine (8 + i * 20, 0.0f, (float) getHeight());
    }

    g.setColour (pal ? pal->panelBg : juce::Colour (0xff1a1a1a));
    g.fillRect (bounds);

    if (folderTrack != nullptr)
    {
        auto contentBounds = bounds.reduced (0, 8);
        g.setColour (pal ? pal->insetBg : juce::Colour (0xff202020));
        g.fillRect (contentBounds);
        g.setColour (pal ? pal->borderSubtle : juce::Colour (0xff444444));
        g.drawRect (contentBounds);
        g.setFont (waive::Fonts::caption());
        g.setColour (pal ? pal->textMuted : juce::Colour (0xffa0a0a0));
        g.drawText ("Folder track", contentBounds.withTrimmedLeft (12),
                    juce::Justification::centredLeft, true);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        return;
    }

    const auto currentScroll = timeline.getScrollOffsetSeconds();
    const auto currentPPS = timeline.getPixelsPerSecond();

    if (std::abs (currentScroll - cachedScrollOffsetForGrid) > 0.001
        || std::abs (currentPPS - cachedPixelsPerSecondForGrid) > 0.001)
    {
        juce::Array<double> majorLines, minorLines;
        const auto startTime = currentScroll;
        const auto endTime = startTime + bounds.getWidth() / currentPPS;
        timeline.getGridLineTimes (startTime, endTime, majorLines, minorLines);

        cachedGridLines.clear();
        for (auto t : minorLines)
            cachedGridLines.push_back ({ timeline.timeToX (t), true });
        for (auto t : majorLines)
            cachedGridLines.push_back ({ timeline.timeToX (t), false });

        cachedScrollOffsetForGrid = currentScroll;
        cachedPixelsPerSecondForGrid = currentPPS;
    }

    juce::Path majorPath, minorPath;
    for (const auto& gridLine : cachedGridLines)
    {
        const float xf = (float) gridLine.x;
        if (gridLine.isMinor)
            minorPath.addLineSegment ({ xf, 0.0f, xf, (float) getHeight() }, 1.0f);
        else
            majorPath.addLineSegment ({ xf, 0.0f, xf, (float) getHeight() }, 1.0f);
    }

    g.setColour (pal ? pal->gridMinor : juce::Colour (0xff333333));
    g.fillPath (minorPath);
    g.setColour (pal ? pal->gridMajor : juce::Colour (0xff555555));
    g.fillPath (majorPath);

    auto automationBounds = getAutomationBounds();
    g.setColour (pal ? pal->insetBg : juce::Colour (0xff202020));
    g.fillRect (automationBounds);
    g.setColour (pal ? pal->borderSubtle : juce::Colour (0xff444444));
    g.drawRect (automationBounds);

    if (auto* param = getSelectedAutomationParameter())
    {
        auto& curve = param->getCurve();
        const auto valueRange = param->getValueRange();

        if (curve.getNumPoints() > 0)
        {
            juce::Path path;
            for (int i = 0; i < curve.getNumPoints(); ++i)
            {
                const auto ptTime = curve.getPointTime (i).inSeconds();
                const auto x = (float) timeline.timeToX (ptTime);
                const auto y = (float) automationYForNormalisedValue (toNormalised (curve.getPointValue (i), valueRange));

                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }

            g.setColour (pal ? pal->automationCurve : juce::Colour (0xff4477aa));
            g.strokePath (path, juce::PathStrokeType (1.5f));

            for (int i = 0; i < curve.getNumPoints(); ++i)
            {
                const auto ptTime = curve.getPointTime (i).inSeconds();
                const auto x = (float) timeline.timeToX (ptTime);
                const auto y = (float) automationYForNormalisedValue (toNormalised (curve.getPointValue (i), valueRange));
                g.setColour (pal ? pal->automationPoint : juce::Colour (0xe6ffffff));
                g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
            }
        }
    }

    g.setColour (pal ? pal->border : juce::Colour (0xff555555));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TrackLaneComponent::resized()
{
    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromLeft (TimelineComponent::trackHeaderWidth).reduced (waive::Spacing::xs);
    headerBounds.removeFromLeft (depth * 20);

    if (folderTrack != nullptr)
    {
        folderToggleButton.setBounds (headerBounds.removeFromLeft (20));
        headerBounds.removeFromLeft (waive::Spacing::xxs);
        folderSoloButton.setBounds (headerBounds.removeFromLeft (22));
        headerBounds.removeFromLeft (waive::Spacing::xxs);
        folderMuteButton.setBounds (headerBounds.removeFromLeft (22));
        headerBounds.removeFromLeft (waive::Spacing::xs);
        headerLabel.setBounds (headerBounds.removeFromTop (waive::Spacing::controlHeightSmall));
        headerBounds.removeFromTop (waive::Spacing::xs);
        folderVolumeSlider.setBounds (headerBounds.removeFromTop (18));
    }
    else
    {
        headerLabel.setBounds (headerBounds.removeFromTop (waive::Spacing::controlHeightSmall));
        headerBounds.removeFromTop (waive::Spacing::xxs);
        automationParamCombo.setBounds (headerBounds.removeFromTop (waive::Spacing::controlHeightSmall));
    }

    layoutClipComponents();
}

void TrackLaneComponent::updateClips()
{
    if (audioTrack == nullptr)
        return;

    clipComponents.clear();
    removeAllChildren();
    addAndMakeVisible (headerLabel);
    addAndMakeVisible (automationParamCombo);

    for (auto* clip : audioTrack->getClips())
    {
        auto cc = std::make_unique<ClipComponent> (*clip, timeline);
        addAndMakeVisible (cc.get());
        clipComponents.push_back (std::move (cc));
    }

    lastClipCount = audioTrack->getClips().size();
    layoutDirty = true;
    resized();
}

void TrackLaneComponent::pollState()
{
    headerLabel.setText (track.getName(), juce::dontSendNotification);

    if (folderTrack != nullptr)
    {
        folderToggleButton.setButtonText (timeline.isFolderCollapsed (track.itemID) ? ">" : "v");
        folderSoloButton.setToggleState (folderTrack->isSolo (false), juce::dontSendNotification);
        folderMuteButton.setToggleState (folderTrack->isMuted (false), juce::dontSendNotification);

        auto plugins = folderTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
        if (! plugins.isEmpty())
            folderVolumeSlider.setValue (te::volumeFaderPositionToDB (plugins.getFirst()->volParam->getCurrentValue()),
                                         juce::dontSendNotification);
        repaint();
        return;
    }

    const int currentCount = audioTrack->getClips().size();
    const int automatableCount = audioTrack->getAllAutomatableParams().size();

    if (currentCount != lastClipCount)
        updateClips();

    if (automatableCount != lastAutomatableParamCount)
        refreshAutomationParams();

    const double currentScroll = timeline.getScrollOffsetSeconds();
    const double currentPPS = timeline.getPixelsPerSecond();
    const bool viewportChanged = (std::abs (currentScroll - lastScrollOffset) > 0.001)
                              || (std::abs (currentPPS - lastPixelsPerSecond) > 0.001);

    if (layoutDirty || viewportChanged)
    {
        layoutClipComponents();
        layoutDirty = false;
        lastScrollOffset = currentScroll;
        lastPixelsPerSecond = currentPPS;
        repaint();
    }
}

void TrackLaneComponent::mouseDown (const juce::MouseEvent& e)
{
    auto headerBounds = getLocalBounds().removeFromLeft (TimelineComponent::trackHeaderWidth);
    if (e.mods.isPopupMenu() && headerBounds.contains (e.getPosition()))
    {
        showTrackContextMenu();
        return;
    }

    if (audioTrack == nullptr || ! getAutomationBounds().contains (e.getPosition()))
        return;

    auto* param = getSelectedAutomationParameter();
    if (param == nullptr)
        return;

    auto& curve = param->getCurve();
    draggingAutomationPointIndex = findNearbyAutomationPoint (curve, *param, e.x, e.y);

    if (draggingAutomationPointIndex >= 0)
        return;

    const auto snappedTime = timeline.snapTimeToGrid (timeline.xToTime (e.x));
    const auto normalised = normalisedFromAutomationY (e.y);
    const auto value = juce::jmap (normalised, 0.0f, 1.0f,
                                   param->getValueRange().getStart(),
                                   param->getValueRange().getEnd());

    timeline.getEditSession().performEdit ("Add Automation Point", [&] (te::Edit& edit)
    {
        draggingAutomationPointIndex = curve.addPoint (te::TimePosition::fromSeconds (snappedTime),
                                                       value, 0.0f, &edit.getUndoManager());
    });
}

void TrackLaneComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (audioTrack == nullptr || draggingAutomationPointIndex < 0)
        return;

    auto* param = getSelectedAutomationParameter();
    if (param == nullptr)
        return;

    auto& curve = param->getCurve();
    if (! juce::isPositiveAndBelow (draggingAutomationPointIndex, curve.getNumPoints()))
        return;

    const auto snappedTime = timeline.snapTimeToGrid (timeline.xToTime (e.x));
    const auto normalised = normalisedFromAutomationY (e.y);
    const auto value = juce::jmap (normalised, 0.0f, 1.0f,
                                   param->getValueRange().getStart(),
                                   param->getValueRange().getEnd());

    timeline.getEditSession().performEdit ("Move Automation Point", true, [&] (te::Edit& edit)
    {
        draggingAutomationPointIndex = curve.movePoint (*param, draggingAutomationPointIndex,
                                                        te::TimePosition::fromSeconds (snappedTime),
                                                        value, false, &edit.getUndoManager());
    });
}

void TrackLaneComponent::mouseUp (const juce::MouseEvent&)
{
    if (draggingAutomationPointIndex >= 0)
        timeline.getEditSession().endCoalescedTransaction();

    draggingAutomationPointIndex = -1;
}

void TrackLaneComponent::mouseMove (const juce::MouseEvent& e)
{
    auto headerBounds = getLocalBounds().removeFromLeft (TimelineComponent::trackHeaderWidth);
    const bool nowInHeader = headerBounds.contains (e.getPosition());
    if (nowInHeader != isHeaderHovered)
    {
        isHeaderHovered = nowInHeader;
        repaint();
    }
}

void TrackLaneComponent::mouseEnter (const juce::MouseEvent& e)
{
    auto headerBounds = getLocalBounds().removeFromLeft (TimelineComponent::trackHeaderWidth);
    isHeaderHovered = headerBounds.contains (e.getPosition());
    repaint();
}

void TrackLaneComponent::mouseExit (const juce::MouseEvent&)
{
    if (isHeaderHovered)
    {
        isHeaderHovered = false;
        repaint();
    }
}

void TrackLaneComponent::refreshAutomationParams()
{
    if (audioTrack == nullptr)
        return;

    lastAutomatableParamCount = audioTrack->getAllAutomatableParams().size();

    const auto previousId = automationParamCombo.getSelectedId();

    automationParamCombo.clear (juce::dontSendNotification);
    automationParamCombo.addItem ("Automation: none", 1);

    automationParams.clear();
    for (auto* param : audioTrack->getAllAutomatableParams())
    {
        if (param == nullptr || param->getPlugin() == nullptr)
            continue;

        automationParams.add (param);
        automationParamCombo.addItem (param->getPluginAndParamName(), automationParams.size() + 1);
    }

    bool hasPrevious = false;
    for (int i = 0; i < automationParamCombo.getNumItems(); ++i)
    {
        if (automationParamCombo.getItemId (i) == previousId)
        {
            hasPrevious = true;
            break;
        }
    }

    if (hasPrevious)
        automationParamCombo.setSelectedId (previousId, juce::dontSendNotification);
    else if (automationParamCombo.getNumItems() > 1)
        automationParamCombo.setSelectedId (2, juce::dontSendNotification);
    else
        automationParamCombo.setSelectedId (1, juce::dontSendNotification);
}

void TrackLaneComponent::layoutClipComponents()
{
    if (audioTrack == nullptr)
        return;

    auto clipBounds = getClipLaneBounds();
    const int clipHeight = juce::jmax (10, clipBounds.getHeight() - 2);

    for (auto& cc : clipComponents)
    {
        auto pos = cc->getClip().getPosition();
        const int x = timeline.timeToX (pos.getStart().inSeconds());
        const int w = (int) (pos.getLength().inSeconds() * timeline.getPixelsPerSecond());
        cc->setBounds (x, clipBounds.getY(), juce::jmax (4, w), clipHeight);
    }
}

juce::Rectangle<int> TrackLaneComponent::getClipLaneBounds() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromLeft (TimelineComponent::trackHeaderWidth);
    if (audioTrack == nullptr)
        return bounds;
    return bounds.removeFromTop (getHeight() - automationLaneHeight);
}

juce::Rectangle<int> TrackLaneComponent::getAutomationBounds() const
{
    if (audioTrack == nullptr)
        return {};

    auto bounds = getLocalBounds();
    bounds.removeFromLeft (TimelineComponent::trackHeaderWidth);
    return bounds.removeFromBottom (automationLaneHeight);
}

te::AutomatableParameter* TrackLaneComponent::getSelectedAutomationParameter() const
{
    const auto itemId = automationParamCombo.getSelectedId();
    if (itemId < 2)
        return nullptr;

    const int index = itemId - 2;
    if (! juce::isPositiveAndBelow (index, automationParams.size()))
        return nullptr;

    return automationParams.getUnchecked (index);
}

float TrackLaneComponent::normalisedFromAutomationY (int y) const
{
    auto bounds = getAutomationBounds();
    const auto norm = 1.0f - ((float) (y - bounds.getY()) / (float) juce::jmax (1, bounds.getHeight()));
    return juce::jlimit (0.0f, 1.0f, norm);
}

int TrackLaneComponent::automationYForNormalisedValue (float normalised) const
{
    auto bounds = getAutomationBounds();
    return bounds.getY() + (int) ((1.0f - juce::jlimit (0.0f, 1.0f, normalised)) * (float) bounds.getHeight());
}

int TrackLaneComponent::findNearbyAutomationPoint (te::AutomationCurve& curve, te::AutomatableParameter& param, int mouseX, int mouseY) const
{
    constexpr int xThreshold = 6;
    constexpr int yThreshold = 8;

    const auto valueRange = param.getValueRange();
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const int x = timeline.timeToX (curve.getPointTime (i).inSeconds());
        const int y = automationYForNormalisedValue (toNormalised (curve.getPointValue (i), valueRange));
        if (std::abs (x - mouseX) <= xThreshold && std::abs (y - mouseY) <= yThreshold)
            return i;
    }

    return -1;
}

void TrackLaneComponent::showTrackContextMenu()
{
    juce::PopupMenu menu;
    menu.addItem (menuRenameTrack, "Rename Track");

    juce::PopupMenu moveToFolderMenu;
    int folderMenuIndex = 0;
    for (auto* candidate : timeline.getEditSession().getEdit().getTrackList())
    {
        auto* candidateFolder = dynamic_cast<te::FolderTrack*> (candidate);
        if (candidateFolder == nullptr)
            continue;

        if (candidateFolder == &track || isTrackInsideFolderSubtree (*candidateFolder, track))
            continue;

        moveToFolderMenu.addItem (menuMoveToFolderBase + folderMenuIndex, candidateFolder->getName());
        ++folderMenuIndex;
    }

    if (folderMenuIndex > 0)
        menu.addSubMenu ("Move to Folder", moveToFolderMenu);

    if (track.getParentFolderTrack() != nullptr)
        menu.addItem (menuRemoveFromFolder, "Remove from Folder");

    menu.addSeparator();
    menu.addItem (menuDeleteTrack, "Delete Track");

    juce::Component::SafePointer<TrackLaneComponent> safeThis (this);

    menu.showMenuAsync ({}, [safeThis] (int result)
    {
        if (! safeThis)
            return;

        auto& trk = safeThis->track;
        auto& tl = safeThis->timeline;

        switch (result)
        {
            case menuRenameTrack:
                safeThis->headerLabel.showEditor();
                break;

            case menuRemoveFromFolder:
                tl.getEditSession().performEdit ("Remove From Folder", [&trk] (te::Edit& edit)
                {
                    if (auto* parentFolder = trk.getParentFolderTrack())
                        edit.moveTrack (&trk, te::TrackInsertPoint (nullptr, parentFolder));
                });
                break;

            case menuDeleteTrack:
            {
                const auto clipCount = safeThis->audioTrack != nullptr ? safeThis->audioTrack->getClips().size() : 0;
                const bool hasClips = clipCount > 0;

                if (hasClips)
                {
                    auto trackID = trk.itemID;
                    juce::AlertWindow::showOkCancelBox (
                        juce::MessageBoxIconType::WarningIcon,
                        "Delete Track",
                        "This track contains " + juce::String (clipCount) + " clip(s). Are you sure?",
                        "Delete", "Cancel",
                        nullptr,
                        juce::ModalCallbackFunction::create ([safeThis, trackID] (int choice)
                        {
                            if (choice != 1 || ! safeThis)
                                return;

                            auto& edit = safeThis->timeline.getEditSession().getEdit();
                            for (auto* candidate : edit.getTrackList())
                            {
                                if (candidate != nullptr && candidate->itemID == trackID)
                                {
                                    safeThis->timeline.getEditSession().performEdit ("Delete Track", [candidate] (te::Edit&)
                                    {
                                        candidate->edit.deleteTrack (candidate);
                                    });
                                    break;
                                }
                            }
                        })
                    );
                }
                else
                {
                    tl.getEditSession().performEdit ("Delete Track", [&trk] (te::Edit&)
                    {
                        trk.edit.deleteTrack (&trk);
                    });
                }
                break;
            }
            default:
                if (result >= menuMoveToFolderBase && result < menuRemoveFromFolder)
                {
                    const auto folderOffset = result - menuMoveToFolderBase;
                    int folderIndex = 0;
                    te::FolderTrack* destinationFolder = nullptr;

                    for (auto* candidate : tl.getEditSession().getEdit().getTrackList())
                    {
                        auto* candidateFolder = dynamic_cast<te::FolderTrack*> (candidate);
                        if (candidateFolder == nullptr)
                            continue;

                        if (candidateFolder == &trk || isTrackInsideFolderSubtree (*candidateFolder, trk))
                            continue;

                        if (folderIndex == folderOffset)
                        {
                            destinationFolder = candidateFolder;
                            break;
                        }

                        ++folderIndex;
                    }

                    if (destinationFolder != nullptr)
                    {
                        tl.getEditSession().performEdit ("Move Track To Folder", [&trk, destinationFolder] (te::Edit& edit)
                        {
                            edit.moveTrack (&trk, te::TrackInsertPoint (destinationFolder, nullptr));
                        });
                    }
                }
                break;
        }
    });
}

juce::Colour TrackLaneComponent::getTrackColorForTesting() const
{
    auto* pal = waive::getWaivePalette (const_cast<TrackLaneComponent&> (*this));
    if (! pal)
        return juce::Colours::grey;

    const juce::Colour* trackColors[] = {
        &pal->trackColor1, &pal->trackColor2, &pal->trackColor3, &pal->trackColor4,
        &pal->trackColor5, &pal->trackColor6, &pal->trackColor7, &pal->trackColor8,
        &pal->trackColor9, &pal->trackColor10, &pal->trackColor11, &pal->trackColor12
    };

    return *trackColors[track.getIndexInEditTrackList() % 12];
}
