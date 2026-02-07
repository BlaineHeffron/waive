#include "TrackLaneComponent.h"
#include "ClipComponent.h"
#include "TimelineComponent.h"
#include "WaiveLookAndFeel.h"

#include <cmath>

namespace
{
float toNormalised (float value, const juce::Range<float>& range)
{
    if (range.getLength() <= 0.0f)
        return 0.0f;

    return juce::jlimit (0.0f, 1.0f,
                         (value - range.getStart()) / range.getLength());
}
}

//==============================================================================
TrackLaneComponent::TrackLaneComponent (te::AudioTrack& t, TimelineComponent& tl)
    : track (t), timeline (tl)
{
    headerLabel.setText (track.getName(), juce::dontSendNotification);
    headerLabel.setJustificationType (juce::Justification::centredLeft);
    headerLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
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
    addAndMakeVisible (automationParamCombo);

    refreshAutomationParams();
    updateClips();
    startTimerHz (5);
}

TrackLaneComponent::~TrackLaneComponent()
{
    stopTimer();
}

void TrackLaneComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    auto* pal = waive::getWaivePalette (*this);

    // Track header background
    auto headerBounds = bounds.removeFromLeft (TimelineComponent::trackHeaderWidth);
    auto headerColour = pal ? pal->surfaceBg : juce::Colour (0xff2a2a2a);
    if (isHeaderHovered)
        headerColour = headerColour.brighter (0.1f);
    g.setColour (headerColour);
    g.fillRect (headerBounds);

    // Lane background
    g.setColour (pal ? pal->panelBg : juce::Colour (0xff222222));
    g.fillRect (bounds);

    // Grid lines (clip + automation area) - use cache
    const auto currentScroll = timeline.getScrollOffsetSeconds();
    const auto currentPPS = timeline.getPixelsPerSecond();

    if (std::abs (currentScroll - cachedScrollOffsetForGrid) > 0.001 ||
        std::abs (currentPPS - cachedPixelsPerSecondForGrid) > 0.001)
    {
        // Recalculate grid lines
        juce::Array<double> majorLines, minorLines;
        const auto startTime = currentScroll;
        const auto endTime = startTime + bounds.getWidth() / currentPPS;
        timeline.getGridLineTimes (startTime, endTime, majorLines, minorLines);

        cachedGridLines.clear();
        for (auto t : minorLines)
            cachedGridLines.push_back (timeline.timeToX (t) | 0x80000000);  // Mark minor with high bit
        for (auto t : majorLines)
            cachedGridLines.push_back (timeline.timeToX (t));

        cachedScrollOffsetForGrid = currentScroll;
        cachedPixelsPerSecondForGrid = currentPPS;
    }

    // Draw cached grid lines
    for (auto x : cachedGridLines)
    {
        const bool isMinor = (x & 0x80000000) != 0;
        const int xPos = x & 0x7FFFFFFF;
        g.setColour (isMinor ? (pal ? pal->gridMinor : juce::Colours::grey.withAlpha (0.12f))
                              : (pal ? pal->gridMajor : juce::Colours::lightgrey.withAlpha (0.2f)));
        g.drawVerticalLine (xPos, 0.0f, (float) getHeight());
    }

    // Automation lane background
    auto automationBounds = getAutomationBounds();
    g.setColour (pal ? pal->insetBg : juce::Colour (0xff1f1f1f));
    g.fillRect (automationBounds);
    g.setColour (pal ? pal->borderSubtle : juce::Colour (0xff303030));
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

            g.setColour (pal ? pal->automationCurve : juce::Colour (0xff77b7ff));
            g.strokePath (path, juce::PathStrokeType (1.5f));

            for (int i = 0; i < curve.getNumPoints(); ++i)
            {
                const auto ptTime = curve.getPointTime (i).inSeconds();
                const auto x = (float) timeline.timeToX (ptTime);
                const auto y = (float) automationYForNormalisedValue (toNormalised (curve.getPointValue (i), valueRange));
                g.setColour (pal ? pal->automationPoint : juce::Colours::white.withAlpha (0.9f));
                g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
            }
        }
    }

    // Bottom separator
    g.setColour (pal ? pal->border : juce::Colour (0xff3a3a3a));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TrackLaneComponent::resized()
{
    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromLeft (TimelineComponent::trackHeaderWidth).reduced (4);
    headerLabel.setBounds (headerBounds.removeFromTop (20));
    headerBounds.removeFromTop (2);
    automationParamCombo.setBounds (headerBounds.removeFromTop (20));

    layoutClipComponents();
}

void TrackLaneComponent::updateClips()
{
    clipComponents.clear();
    removeAllChildren();
    addAndMakeVisible (headerLabel);
    addAndMakeVisible (automationParamCombo);

    for (auto* clip : track.getClips())
    {
        auto cc = std::make_unique<ClipComponent> (*clip, timeline);
        addAndMakeVisible (cc.get());
        clipComponents.push_back (std::move (cc));
    }

    lastClipCount = track.getClips().size();
    layoutDirty = true;
    resized();
}

void TrackLaneComponent::timerCallback()
{
    const int currentCount = track.getClips().size();
    const int automatableCount = track.getAllAutomatableParams().size();

    if (currentCount != lastClipCount)
        updateClips();

    if (automatableCount != lastAutomatableParamCount)
        refreshAutomationParams();

    const double currentScroll = timeline.getScrollOffsetSeconds();
    const double currentPPS = timeline.getPixelsPerSecond();

    const bool viewportChanged = (std::abs (currentScroll - lastScrollOffset) > 0.001) ||
                                  (std::abs (currentPPS - lastPixelsPerSecond) > 0.001);

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
    // Check for right-click on header
    auto headerBounds = getLocalBounds().removeFromLeft (TimelineComponent::trackHeaderWidth);
    if (e.mods.isPopupMenu() && headerBounds.contains (e.getPosition()))
    {
        showTrackContextMenu();
        return;
    }

    if (! getAutomationBounds().contains (e.getPosition()))
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
    if (draggingAutomationPointIndex < 0)
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
    bool nowInHeader = headerBounds.contains (e.getPosition());
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
    lastAutomatableParamCount = track.getAllAutomatableParams().size();

    const auto previousId = automationParamCombo.getSelectedId();

    automationParamCombo.clear (juce::dontSendNotification);
    automationParamCombo.addItem ("Automation: none", 1);

    automationParams.clear();
    for (auto* param : track.getAllAutomatableParams())
    {
        if (param == nullptr || param->getPlugin() == nullptr)
            continue;

        automationParams.add (param);
        const auto itemId = automationParams.size() + 1;
        automationParamCombo.addItem (param->getPluginAndParamName(), itemId);
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
    return bounds.removeFromTop (getHeight() - automationLaneHeight);
}

juce::Rectangle<int> TrackLaneComponent::getAutomationBounds() const
{
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
    menu.addItem (1, "Rename Track");
    menu.addSeparator();
    menu.addItem (5, "Delete Track");

    juce::Component::SafePointer<TrackLaneComponent> safeThis (this);

    menu.showMenuAsync ({}, [safeThis] (int result)
    {
        if (! safeThis)
            return;

        auto& trk = safeThis->track;
        auto& tl = safeThis->timeline;

        switch (result)
        {
            case 1: // Rename
                safeThis->headerLabel.showEditor();
                break;

            case 5: // Delete
            {
                bool hasClips = trk.getClips().size() > 0;
                if (hasClips)
                {
                    // Capture track itemID to safely locate track in async callback
                    auto trackID = trk.itemID;
                    juce::AlertWindow::showOkCancelBox (
                        juce::MessageBoxIconType::WarningIcon,
                        "Delete Track",
                        "This track contains " + juce::String (trk.getClips().size()) + " clip(s). Are you sure?",
                        "Delete", "Cancel",
                        nullptr,
                        juce::ModalCallbackFunction::create ([safeThis, trackID] (int choice)
                        {
                            if (choice == 1 && safeThis)
                            {
                                auto& edit = safeThis->timeline.getEditSession().getEdit();
                                for (auto* t : edit.getTrackList())
                                {
                                    if (t->itemID == trackID)
                                    {
                                        safeThis->timeline.getEditSession().performEdit ("Delete Track", [t] (te::Edit&)
                                        {
                                            t->edit.deleteTrack (t);
                                        });
                                        break;
                                    }
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
                break;
        }
    });
}
