# Phase 14: Screenshot-Driven UI/UX Polish

## Objective
Use the Phase 6 screenshot capture infrastructure (`--screenshot` mode + `scripts/ui_review.sh`) to visually audit the entire Waive UI, identify visual defects, and implement fixes. This phase is iterative: capture → analyze → fix → re-capture → verify. The goal is a polished, professional-looking DAW that conforms to modern UI/UX best practices.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- The `waive::ColourPalette` struct has NO member named `error`. Use `danger` for error colors.
- Available fonts: `header()`, `subheader()`, `body()`, `label()`, `caption()`, `mono()`, `meter()`.
- Available spacing constants are in `gui/src/theme/WaiveSpacing.h` — use `waive::Spacing::*`.
- Color palette accessed via `getWaivePalette(*this)` — never hardcode colors.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### Screenshot capture system (Phase 6)
- `Waive --screenshot <output-dir>` — launches the app, seeds 4 demo tracks (Drums/Bass/Guitar/Vocals at 120bpm), captures screenshots of all major UI areas as compressed JPEGs, then exits.
- Screenshots captured: `01_full_window.jpg`, `02_session_full.jpg`, `03_timeline.jpg`, `04_mixer.jpg`, `05_tool_sidebar.jpg`, `06_chat_panel.jpg`, `07_library.jpg`, `08_plugin_browser.jpg`
- Run under xvfb for headless: `xvfb-run -a -s "-screen 0 1920x1080x24" build/gui/Waive_artefacts/Release/Waive --screenshot /tmp/screenshots`

### Theme system (gui/src/theme/)
- **WaiveColours.h**: 76-color semantic palette (backgrounds, borders, text, accent, DAW-specific)
- **WaiveFonts.h**: Font hierarchy (header 16, subheader 13 bold, body 13, label 12, caption 11, mono 12, meter 10)
- **WaiveSpacing.h**: General spacing (xxs 2 to xl 24) + UI dimension constants (controlHeightDefault 28, toolbarRowHeight 36, etc.)
- **WaiveLookAndFeel.h/.cpp**: Custom drawing for buttons, sliders, tabs, combos, toggles, text editors, progress bars. Has hover (15% brighter) and pressed (15% darker) states. Focus ring: 2px borderFocused.

### Key components to review visually
1. **Full window** (01) — overall proportions, tab bar appearance, menu bar
2. **Session view** (02) — transport toolbar layout, proportions of timeline vs mixer
3. **Timeline** (03) — ruler readability, track headers, clip rendering, waveform visibility, selection highlighting, playhead
4. **Mixer** (04) — channel strip alignment, fader/knob sizes, meter readability, label truncation, master strip distinction
5. **Tool sidebar** (05) — model manager section, tool selector, schema form layout, action buttons
6. **Chat panel** (06) — message bubbles, input area, send button, provider selector, spacing
7. **Library** (07) — file browser, search bar, list items, import/open actions
8. **Plugin browser** (08) — plugin list, detail panel, load/scan buttons

### Common UI/UX problems to look for
- **Cramped spacing**: Elements too close together, no breathing room
- **Inconsistent alignment**: Left edges don't align, uneven gaps between groups
- **Poor contrast**: Text hard to read against background, especially captions/labels
- **Missing visual grouping**: Related controls not visually grouped (no section dividers or background differences)
- **Label truncation**: Text cut off without ellipsis, especially in mixer strip names
- **Unclear interactive states**: Buttons that don't look clickable, no visual feedback on hover/press
- **Typography inconsistency**: Wrong font weight/size for context (e.g., body font used where label font should be)
- **Color inconsistency**: Some elements using wrong semantic color (e.g., using textPrimary where textMuted is more appropriate)
- **Empty state**: What does the UI look like with no content? (empty library, no plugins loaded)
- **Overflow/clipping**: Content that extends beyond its container without scroll or truncation

## Implementation Tasks

### 1. Build and capture initial screenshots

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
xvfb-run -a -s "-screen 0 1920x1080x24" build/gui/Waive_artefacts/Release/Waive --screenshot /tmp/waive_screenshots
```

View each screenshot image and note every visual issue.

### 2. Fix transport toolbar layout (SessionComponent)

Review `02_session_full.jpg` and `01_full_window.jpg`. Common toolbar issues:

**Visual grouping**: Add subtle separators or spacing between control groups:
- Playback group (play/stop/record)
- Position/selection display
- Tempo/time signature group
- Marker group

In `SessionComponent::resized()`, add small spacer FlexItems between logical groups:
```cpp
// Between transport buttons and position display
primaryFlex.items.add (juce::FlexItem().withWidth (waive::Spacing::lg)); // larger gap = group separator
```

**Toolbar background**: If the toolbar blends too much with the content below, consider painting a subtle background strip:
```cpp
void SessionComponent::paint (juce::Graphics& g)
{
    auto pal = waive::getWaivePalette (*this);
    if (pal)
    {
        // Toolbar background
        auto toolbarArea = getLocalBounds().removeFromTop (/* toolbar height */);
        g.setColour (pal->surfaceBg);
        g.fillRect (toolbarArea);

        // Subtle bottom border
        g.setColour (pal->borderSubtle);
        g.drawHorizontalLine (toolbarArea.getBottom(), 0.0f, (float) getWidth());
    }
}
```

### 3. Fix timeline visual issues (TimelineComponent)

Review `03_timeline.jpg`. Common issues:

- **Ruler readability**: Ensure tick marks and time labels have enough contrast (`textMuted` may be too faint — try `textSecondary`)
- **Track header alignment**: Name labels should be left-aligned with consistent padding
- **Clip styling**: Clips should have clear boundaries (border or shadow), clip names visible
- **Empty tracks**: Empty tracks should still look intentional, not broken
- **Playhead**: Should be clearly visible (bright accent color, not just 1px line — consider 2px)
- **Selection highlight**: Selected clip should have a clear visual distinction (brighter border + background tint)

### 4. Fix mixer visual issues (MixerComponent / MixerChannelStrip)

Review `04_mixer.jpg`. Common issues:

- **Strip separation**: Ensure clear visual boundary between strips (subtle border or gap)
- **Name label truncation**: Long names should use ellipsis (`juce::Graphics::drawFittedText`)
- **Fader proportions**: Fader should be large enough to be usable; knobs should be clearly visible
- **Meter readability**: dB scale labels should be readable; meter gradient should be smooth
- **Master strip**: Should be visually distinct from track strips (different background shade, label "Master")
- **Button sizing**: Solo/mute buttons should be comfortably tappable, not cramped

### 5. Fix tool sidebar layout (ToolSidebarComponent)

Review `05_tool_sidebar.jpg`. Common issues:

- **Section dividers**: Add visual separation between model manager, tool selector, schema form, and action buttons
- **Schema form spacing**: Form fields should have consistent vertical rhythm
- **Action button row**: Buttons should have uniform width and consistent spacing
- **Scrolling**: If schema form overflows, scroll indicator should be visible
- **Empty state**: When no tool is selected, show helpful placeholder text

In `ToolSidebarComponent::paint()`, add section dividers:
```cpp
// Draw horizontal line between sections
g.setColour (pal->borderSubtle);
g.drawHorizontalLine (sectionBottom, leftMargin, rightEdge);
```

### 6. Fix chat panel layout (ChatPanelComponent)

Review `06_chat_panel.jpg`. Common issues:

- **Message spacing**: Messages should have clear vertical gaps between them
- **User vs AI distinction**: User messages and AI responses should be visually distinct (different background colors or alignment)
- **Input area**: Text input should be clearly bordered, with adequate height for multi-line input
- **Send button**: Should look clickable, with clear hover state
- **Empty state**: "No messages yet" placeholder

### 7. Fix library view (LibraryComponent)

Review `07_library.jpg`. Common issues:

- **Search bar styling**: Should have clear focus state and placeholder text
- **List item hover**: File/folder items should highlight on hover
- **Empty state**: If no files, show helpful message
- **File type indicators**: Different file types (audio, MIDI) should be visually distinguishable

### 8. Fix plugin browser (PluginBrowserComponent)

Review `08_plugin_browser.jpg`. Common issues:

- **Two-panel layout**: Left panel (list) and right panel (details) should have clear separation
- **List item density**: Plugin list items should be evenly spaced with consistent height
- **Detail panel**: Plugin name, manufacturer, format should have proper typography hierarchy
- **Load button**: Should be prominently placed and clearly actionable
- **Scan button**: Should be secondary (less prominent than load)

### 9. Global paint improvements

After fixing individual components, consider these global improvements:

**Rounded corners** on panels/sections:
```cpp
g.setColour (pal->surfaceBg);
g.fillRoundedRectangle (area.toFloat(), 4.0f);
```

**Subtle shadows/borders** to create depth:
```cpp
g.setColour (pal->borderSubtle);
g.drawRoundedRectangle (area.toFloat(), 4.0f, 1.0f);
```

**Consistent section headers**: Use `WaiveFonts::subheader()` with `textSecondary` color for section titles.

### 10. Re-capture and verify

After all fixes:
```bash
cmake --build build --target Waive -j$(($(nproc)/2))
xvfb-run -a -s "-screen 0 1920x1080x24" build/gui/Waive_artefacts/Release/Waive --screenshot /tmp/waive_screenshots_v2
```

View each screenshot again and verify:
- [ ] All spacing is consistent and comfortable
- [ ] Typography hierarchy is clear (headers > subheaders > body > labels > captions)
- [ ] Color usage is consistent (interactive elements use accent, status uses semantic colors)
- [ ] Visual grouping makes the UI scannable at a glance
- [ ] No text truncation without ellipsis
- [ ] All sections have clear boundaries
- [ ] The overall look is professional and modern

If issues remain, iterate: fix → rebuild → re-capture → verify.

## Files Expected To Change
- `gui/src/ui/SessionComponent.cpp` (and possibly .h)
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/MixerComponent.cpp`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/ToolSidebarComponent.cpp`
- `gui/src/ui/SchemaFormComponent.cpp`
- `gui/src/ai/ChatPanelComponent.cpp`
- `gui/src/ui/LibraryComponent.cpp`
- `gui/src/ui/PluginBrowserComponent.cpp`
- `gui/src/ui/ConsoleComponent.cpp`
- `gui/src/ui/ToolLogComponent.cpp`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
xvfb-run -a -s "-screen 0 1920x1080x24" build/gui/Waive_artefacts/Release/Waive --screenshot /tmp/waive_final_check
```

## Exit Criteria
- All screenshots reviewed and visual issues addressed.
- Transport toolbar has clear visual grouping between control sections.
- Timeline ruler, clips, and selection highlighting are clearly readable.
- Mixer channel strips have proper separation, readable meters, non-truncated labels.
- Tool sidebar has visual section dividers between model manager, tool selector, form, and buttons.
- Chat panel messages are well-spaced with clear user/AI distinction.
- Library and plugin browser have consistent list item styling and clear empty states.
- No hardcoded colors — all colors from `getWaivePalette()`.
- Build compiles with no errors.
- Existing tests still pass (`ctest --test-dir build --output-on-failure`).
