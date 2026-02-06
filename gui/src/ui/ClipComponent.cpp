#include "ClipComponent.h"
#include "TimelineComponent.h"
#include "EditSession.h"
#include "SelectionManager.h"
#include "ClipEditActions.h"
#include "WaiveLookAndFeel.h"

//==============================================================================
ClipComponent::ClipComponent (te::Clip& c, TimelineComponent& tl)
    : clip (c), timeline (tl)
{
    // Create waveform thumbnail for audio clips
    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        thumbnail = std::make_unique<te::SmartThumbnail> (
            clip.edit.engine, te::AudioFile (clip.edit.engine, waveClip->getSourceFileReference().getFile()),
            *this, &clip.edit);
    }
}

ClipComponent::~ClipComponent() = default;

void ClipComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    bool selected = timeline.getSelectionManager().isSelected (&clip);

    // Check if this clip is in preview mode
    bool inPreview = false;
    if (clip.itemID.isValid())
    {
        for (auto previewID : timeline.previewClipIDs)
        {
            if (previewID == clip.itemID)
            {
                inPreview = true;
                break;
            }
        }
    }

    // Use preview highlight if in preview mode, otherwise use selection
    bool highlighted = inPreview || selected;
    auto* pal = waive::getWaivePalette (*this);

    // Background
    auto bgColour = highlighted ? (pal ? pal->clipSelected : juce::Colour (0xff4477aa))
                                : (pal ? pal->clipDefault : juce::Colour (0xff3a5a3a));
    g.setColour (bgColour);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Waveform
    if (thumbnail != nullptr && thumbnail->isFullyLoaded())
    {
        g.setColour (pal ? pal->waveform : juce::Colours::white.withAlpha (0.8f));
        auto waveArea = bounds.reduced (2.0f, 14.0f);
        thumbnail->drawChannels (g, waveArea.toNearestInt(),
                                 { te::TimePosition(), te::TimePosition::fromSeconds (
                                     clip.getPosition().getLength().inSeconds()) },
                                 1.0f);
    }

    // Clip name
    g.setColour (pal ? pal->textOnPrimary : juce::Colours::white);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (clip.getName(), bounds.reduced (4.0f, 1.0f).removeFromTop (14.0f),
                juce::Justification::centredLeft, true);

    // Border
    g.setColour (highlighted ? (pal ? pal->selectionBorder : juce::Colours::white)
                             : (pal ? pal->border : juce::Colours::grey));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Trim handles
    if (isMouseOver())
    {
        g.setColour (pal ? pal->trimHandle : juce::Colours::white.withAlpha (0.3f));
        g.fillRect (bounds.removeFromLeft ((float) trimZoneWidth));
        g.fillRect (getLocalBounds().toFloat().removeFromRight ((float) trimZoneWidth));
    }
}

void ClipComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        timeline.getSelectionManager().selectClip (&clip);
        showContextMenu();
        return;
    }

    // Select
    timeline.getSelectionManager().selectClip (&clip, e.mods.isShiftDown());

    // Determine drag mode
    if (isLeftTrimZone (e.x))
        dragMode = TrimLeft;
    else if (isRightTrimZone (e.x))
        dragMode = TrimRight;
    else
        dragMode = Move;

    auto pos = clip.getPosition();
    dragOriginalStart = pos.getStart().inSeconds();
    dragOriginalEnd = pos.getEnd().inSeconds();
    dragStartTime = timeline.xToTime (e.getScreenX() - getScreenX() + getX());
}

void ClipComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == None)
        return;

    double currentTime = timeline.xToTime (e.getScreenX() - getScreenX() + getX());
    double delta = currentTime - dragStartTime;

    auto& session = timeline.getEditSession();

    switch (dragMode)
    {
        case None:
            break;
        case Move:
        {
            double newStart = juce::jmax (0.0, dragOriginalStart + delta);
            newStart = timeline.snapTimeToGrid (newStart);
            waive::moveClip (session, clip, newStart);
            break;
        }
        case TrimLeft:
        {
            double newStart = juce::jmax (0.0, dragOriginalStart + delta);
            newStart = timeline.snapTimeToGrid (newStart);
            newStart = juce::jmin (newStart, dragOriginalEnd - 0.01);
            waive::trimClipLeft (session, clip, newStart);
            break;
        }
        case TrimRight:
        {
            double newEnd = juce::jmax (dragOriginalStart + 0.01, dragOriginalEnd + delta);
            newEnd = timeline.snapTimeToGrid (newEnd);
            newEnd = juce::jmax (dragOriginalStart + 0.01, newEnd);
            waive::trimClipRight (session, clip, newEnd);
            break;
        }
    }
}

void ClipComponent::mouseUp (const juce::MouseEvent&)
{
    dragMode = None;
    timeline.getEditSession().endCoalescedTransaction();
}

void ClipComponent::mouseMove (const juce::MouseEvent& e)
{
    if (isLeftTrimZone (e.x) || isRightTrimZone (e.x))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ClipComponent::updatePosition()
{
    auto pos = clip.getPosition();
    int x = timeline.timeToX (pos.getStart().inSeconds());
    int w = (int) (pos.getLength().inSeconds() * timeline.getPixelsPerSecond());
    setBounds (x, 0, juce::jmax (4, w), getParentHeight());
}

//==============================================================================
bool ClipComponent::isLeftTrimZone (int x) const
{
    return x < trimZoneWidth;
}

bool ClipComponent::isRightTrimZone (int x) const
{
    return x > getWidth() - trimZoneWidth;
}

void ClipComponent::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Duplicate");
    menu.addItem (2, "Split at Playhead");
    menu.addSeparator();
    menu.addItem (3, "Delete");

    juce::Component::SafePointer<ClipComponent> safeThis (this);
    auto& session = timeline.getEditSession();
    auto clipID = clip.itemID;

    menu.showMenuAsync ({}, [safeThis, &session, clipID] (int result)
    {
        if (! safeThis)
            return;

        auto* resolvedClip = te::findClipForID (session.getEdit(), clipID);
        if (resolvedClip == nullptr)
            return;

        switch (result)
        {
            case 1: waive::duplicateClip (session, *resolvedClip); break;
            case 2:
            {
                auto playPos = resolvedClip->edit.getTransport().getPosition().inSeconds();
                waive::splitClipAtPosition (session, *resolvedClip, playPos);
                break;
            }
            case 3:
            {
                juce::Array<te::Clip*> clips;
                clips.add (resolvedClip);
                waive::deleteClips (session, clips);
                break;
            }
            default: break;
        }
    });
}
