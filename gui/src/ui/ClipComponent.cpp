#include "ClipComponent.h"
#include "TimelineComponent.h"
#include "TrackLaneComponent.h"
#include "EditSession.h"
#include "SelectionManager.h"
#include "WaiveFonts.h"
#include "ClipEditActions.h"
#include "WaiveLookAndFeel.h"
#include "WaiveSpacing.h"

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

    setTitle (clip.getName());
    setDescription ("Audio or MIDI clip - drag to move, drag edges to trim, drag fade handles to adjust fades");
    setWantsKeyboardFocus (true);

    // Cache track index for paint optimization
    updateTrackIndex();
}

ClipComponent::~ClipComponent() = default;

void ClipComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    bool selected = timeline.getSelectionManager().isSelected (&clip);

    // Check if this clip is in preview mode
    bool inPreview = false;
    if (clip.itemID.isValid())
        inPreview = timeline.previewClipIDs.count (clip.itemID) > 0;

    // Use preview highlight if in preview mode, otherwise use selection
    bool highlighted = inPreview || selected;
    auto* pal = waive::getWaivePalette (*this);

    // Get track color using cached index
    juce::Colour trackColor = pal ? pal->textMuted : juce::Colour (0xff808080);
    if (pal)
    {
        const juce::Colour* trackColors[] = {
            &pal->trackColor1, &pal->trackColor2, &pal->trackColor3, &pal->trackColor4,
            &pal->trackColor5, &pal->trackColor6, &pal->trackColor7, &pal->trackColor8,
            &pal->trackColor9, &pal->trackColor10, &pal->trackColor11, &pal->trackColor12
        };
        trackColor = *trackColors[cachedTrackIndex % 12];
    }

    // Background with 20% track color blend
    juce::Colour bgColour = highlighted
        ? (pal ? pal->clipSelected : juce::Colour (0xff4477aa))
        : (pal ? pal->clipDefault : juce::Colour (0xff3a5a3a));
    auto blendedBgColour = bgColour.interpolatedWith (trackColor, 0.2f);
    g.setColour (blendedBgColour);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Waveform
    if (thumbnail != nullptr && thumbnail->isFullyLoaded())
    {
        g.setColour (pal ? pal->waveform : juce::Colour (0xccffffff));
        auto waveArea = bounds.reduced ((float) waive::Spacing::xxs, 14.0f);
        thumbnail->drawChannels (g, waveArea.toNearestInt(),
                                 { te::TimePosition(), te::TimePosition::fromSeconds (
                                     clip.getPosition().getLength().inSeconds()) },
                                 1.0f);
    }

    // Fade overlays
    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        const double fadeInSec = waveClip->getFadeIn().inSeconds();
        const double fadeOutSec = waveClip->getFadeOut().inSeconds();
        const double clipLengthSec = clip.getPosition().getLength().inSeconds();

        g.setColour (pal ? pal->panelBg.withAlpha (0.3f) : juce::Colour (0x4d000000));

        // Fade-in triangle
        if (fadeInSec > 0.001)
        {
            const float fadeInWidth = juce::jmin ((float) (fadeInSec / clipLengthSec * bounds.getWidth()), bounds.getWidth() * 0.5f);
            juce::Path fadeIn;
            fadeIn.addTriangle (bounds.getX(), bounds.getY(),
                                bounds.getX(), bounds.getBottom(),
                                bounds.getX() + fadeInWidth, bounds.getY());
            g.fillPath (fadeIn);
        }

        // Fade-out triangle
        if (fadeOutSec > 0.001)
        {
            const float fadeOutWidth = juce::jmin ((float) (fadeOutSec / clipLengthSec * bounds.getWidth()), bounds.getWidth() * 0.5f);
            juce::Path fadeOut;
            fadeOut.addTriangle (bounds.getRight(), bounds.getY(),
                                 bounds.getRight() - fadeOutWidth, bounds.getY(),
                                 bounds.getRight(), bounds.getBottom());
            g.fillPath (fadeOut);
        }
    }

    // Clip name
    g.setColour (pal ? pal->textOnPrimary : juce::Colour (0xffffffff));
    g.setFont (waive::Fonts::caption());
    g.drawText (clip.getName(), bounds.reduced ((float) waive::Spacing::xs, 1.0f).removeFromTop (14.0f),
                juce::Justification::centredLeft, true);

    // Border (2px for selected clips for clear distinction)
    g.setColour (highlighted ? (pal ? pal->selectionBorder : juce::Colour (0xffffffff))
                             : (pal ? pal->border : juce::Colour (0xff555555)));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, highlighted ? 2.0f : 1.0f);

    // Hover border
    if (isHovered && ! highlighted)
    {
        g.setColour (pal ? pal->primary.brighter() : juce::Colour (0x804477aa));
        g.drawRoundedRectangle (bounds.reduced (1.0f), 4.0f, 2.0f);
    }

    // Trim handles
    if (isMouseOver())
    {
        g.setColour (pal ? pal->trimHandle : juce::Colour (0x4dffffff));
        g.fillRect (bounds.removeFromLeft ((float) trimZoneWidth));
        g.fillRect (getLocalBounds().toFloat().removeFromRight ((float) trimZoneWidth));
    }

    // Ghost drag outline
    if (ghostDragBounds.has_value())
    {
        auto ghostBounds = ghostDragBounds.value();
        g.setColour (pal ? pal->primary.withAlpha (0.3f) : juce::Colour (0x4d4477aa));
        g.drawRoundedRectangle (ghostBounds, 4.0f, 2.0f);

        // Draw snap line if snapping is enabled
        if (timeline.isSnapEnabled())
        {
            g.setColour (pal ? pal->primary : juce::Colour (0xff4477aa));
            g.drawLine (ghostBounds.getX(), 0.0f, ghostBounds.getX(), (float) getParentComponent()->getHeight(), 1.0f);
        }
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
    if (isFadeInZone (e.x, e.y))
        dragMode = FadeIn;
    else if (isFadeOutZone (e.x, e.y))
        dragMode = FadeOut;
    else if (isLeftTrimZone (e.x))
        dragMode = TrimLeft;
    else if (isRightTrimZone (e.x))
        dragMode = TrimRight;
    else
        dragMode = Move;

    auto pos = clip.getPosition();
    dragOriginalStart = pos.getStart().inSeconds();
    dragOriginalEnd = pos.getEnd().inSeconds();
    dragStartTime = timeline.xToTime (e.getScreenX() - getScreenX() + getX());

    // Store initial fade values
    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        dragStartFadeIn = waveClip->getFadeIn().inSeconds();
        dragStartFadeOut = waveClip->getFadeOut().inSeconds();
    }
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

            // Calculate ghost drag position (parent-relative coords)
            double clipLength = dragOriginalEnd - dragOriginalStart;
            int ghostX = timeline.timeToX (newStart);
            int ghostW = (int) (clipLength * timeline.getPixelsPerSecond());
            ghostDragBounds = juce::Rectangle<float> (
                (float) ghostX - TimelineComponent::trackHeaderWidth,
                getY() * 1.0f, ghostW * 1.0f, getHeight() * 1.0f);
            repaint();

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
        case FadeIn:
        {
            if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
            {
                double newFadeIn = juce::jmax (0.0, dragStartFadeIn + delta);
                const double clipLength = clip.getPosition().getLength().inSeconds();
                newFadeIn = juce::jlimit (0.0, clipLength * 0.5, newFadeIn);
                session.performEdit ("Adjust Fade", true, [waveClip, newFadeIn] (te::Edit&)
                {
                    waveClip->setFadeIn (te::TimeDuration::fromSeconds (newFadeIn));
                });
            }
            break;
        }
        case FadeOut:
        {
            if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
            {
                double newFadeOut = juce::jmax (0.0, dragStartFadeOut - delta);
                const double clipLength = clip.getPosition().getLength().inSeconds();
                newFadeOut = juce::jlimit (0.0, clipLength * 0.5, newFadeOut);
                session.performEdit ("Adjust Fade", true, [waveClip, newFadeOut] (te::Edit&)
                {
                    waveClip->setFadeOut (te::TimeDuration::fromSeconds (newFadeOut));
                });
            }
            break;
        }
    }
}

void ClipComponent::mouseUp (const juce::MouseEvent&)
{
    dragMode = None;
    ghostDragBounds.reset();
    repaint();
    timeline.getEditSession().endCoalescedTransaction();
}

void ClipComponent::mouseMove (const juce::MouseEvent& e)
{
    if (isFadeInZone (e.x, e.y))
        setMouseCursor (juce::MouseCursor::TopLeftCornerResizeCursor);
    else if (isFadeOutZone (e.x, e.y))
        setMouseCursor (juce::MouseCursor::TopRightCornerResizeCursor);
    else if (isLeftTrimZone (e.x) || isRightTrimZone (e.x))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ClipComponent::mouseEnter (const juce::MouseEvent&)
{
    isHovered = true;
    repaint();
}

void ClipComponent::mouseExit (const juce::MouseEvent&)
{
    isHovered = false;
    repaint();
}

void ClipComponent::updatePosition()
{
    auto pos = clip.getPosition();
    int x = timeline.timeToX (pos.getStart().inSeconds());
    int w = (int) (pos.getLength().inSeconds() * timeline.getPixelsPerSecond());
    setBounds (x, 0, juce::jmax (4, w), getParentHeight());
}

void ClipComponent::updateTrackIndex()
{
    cachedTrackIndex = 0;
    if (auto* parentLane = dynamic_cast<TrackLaneComponent*> (getParentComponent()))
    {
        auto& edit = timeline.getEditSession().getEdit();
        auto tracks = te::getAudioTracks (edit);
        for (int i = 0; i < tracks.size(); ++i)
        {
            if (tracks[i] == &parentLane->getTrack())
            {
                cachedTrackIndex = i;
                break;
            }
        }
    }
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

bool ClipComponent::isFadeInZone (int x, int y) const
{
    return x < fadeZoneSize && y < fadeZoneSize;
}

bool ClipComponent::isFadeOutZone (int x, int y) const
{
    return x > getWidth() - fadeZoneSize && y < fadeZoneSize;
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
