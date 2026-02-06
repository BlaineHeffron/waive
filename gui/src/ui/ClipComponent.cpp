#include "ClipComponent.h"
#include "TimelineComponent.h"
#include "EditSession.h"
#include "SelectionManager.h"
#include "ClipEditActions.h"

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

    // Background
    auto bgColour = selected ? juce::Colour (0xff4477aa) : juce::Colour (0xff3a5a3a);
    g.setColour (bgColour);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Waveform
    if (thumbnail != nullptr && thumbnail->isFullyLoaded())
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        auto waveArea = bounds.reduced (2.0f, 14.0f);
        thumbnail->drawChannels (g, waveArea.toNearestInt(),
                                 { te::TimePosition(), te::TimePosition::fromSeconds (
                                     clip.getPosition().getLength().inSeconds()) },
                                 1.0f);
    }

    // Clip name
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (clip.getName(), bounds.reduced (4.0f, 1.0f).removeFromTop (14.0f),
                juce::Justification::centredLeft, true);

    // Border
    g.setColour (selected ? juce::Colours::white : juce::Colours::grey);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Trim handles
    if (isMouseOver())
    {
        g.setColour (juce::Colours::white.withAlpha (0.3f));
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
            waive::moveClip (session, clip, newStart);
            break;
        }
        case TrimLeft:
        {
            double newStart = juce::jmax (0.0, dragOriginalStart + delta);
            newStart = juce::jmin (newStart, dragOriginalEnd - 0.01);
            waive::trimClipLeft (session, clip, newStart);
            break;
        }
        case TrimRight:
        {
            double newEnd = juce::jmax (dragOriginalStart + 0.01, dragOriginalEnd + delta);
            waive::trimClipRight (session, clip, newEnd);
            break;
        }
    }
}

void ClipComponent::mouseUp (const juce::MouseEvent&)
{
    dragMode = None;
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

    auto& session = timeline.getEditSession();

    menu.showMenuAsync ({}, [this, &session] (int result)
    {
        switch (result)
        {
            case 1: waive::duplicateClip (session, clip); break;
            case 2:
            {
                auto playPos = clip.edit.getTransport().getPosition().inSeconds();
                waive::splitClipAtPosition (session, clip, playPos);
                break;
            }
            case 3:
            {
                juce::Array<te::Clip*> clips;
                clips.add (&clip);
                waive::deleteClips (session, clips);
                break;
            }
            default: break;
        }
    });
}
