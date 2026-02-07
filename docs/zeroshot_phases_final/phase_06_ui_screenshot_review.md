# Phase 06: Automated UI Screenshot & Review Pipeline

## Objective
Add a `--screenshot <output-dir>` mode to the Waive app that captures compressed screenshots of every major UI area, plus a bash script that runs the app headlessly, compresses the images, and sends them to the Claude API for automated UI/UX review.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail with `#error "Incorrect use of JUCE cpp file"` when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- The `waive::ColourPalette` struct has NO member named `error`. Use `danger` for error colors.
- Available fonts: `header()`, `subheader()`, `body()`, `label()`, `caption()`, `mono()`, `meter()`.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### Main.cpp (gui/src/Main.cpp)
- `WaiveApplication` is a `juce::JUCEApplication` subclass
- `initialise(const juce::String& commandLine)` creates engine, edit, window
- `MainWindow` calls `centreWithSize(1200, 800)`, limits 1024x600..4096x2160
- `MainWindow::setContentOwned(new MainComponent(...), true)` — MainComponent is the content
- Shutdown tears down in reverse order: mainWindow → aiAgent → toolRegistry → ... → engine → lookAndFeel

### MainComponent (gui/src/MainComponent.h/.cpp)
- Owns a `juce::TabbedComponent tabs` with 5 tabs: Session, Library, Plugins, Console, Tool Log
- Tab content components: `sessionComponent`, `libraryComponent`, `pluginBrowser`, `console`, `toolLog`
- Public test helpers already exist: `getSessionComponentForTesting()`, `getToolSidebarForTesting()`, `getLibraryComponentForTesting()`, `getPluginBrowserForTesting()`
- `resized()`: menuBar takes 24px from top, tabs fill the rest

### SessionComponent (gui/src/ui/SessionComponent.h/.cpp)
- Contains: transport toolbar, TimelineComponent, MixerComponent, ToolSidebarComponent, ChatPanelComponent
- `toggleToolSidebar()` / `toggleChatPanel()` show/hide panels
- `getToolSidebar()` returns `ToolSidebarComponent*`
- `getChatPanelForTesting()` returns `ChatPanelComponent*`
- `getTimeline()` returns `TimelineComponent&`

### JUCE Screenshot APIs
- `juce::Component::createComponentSnapshot(juce::Rectangle<int> areaToGrab, bool clipImageToComponentBounds = true, float scaleFactor = 1.0f)` — returns `juce::Image`
- `juce::JPEGImageFormat` — has `setQuality(float quality)` (0.0–1.0), `writeImageToStream(Image, OutputStream)`
- `juce::PNGImageFormat` — lossless alternative
- `juce::Image::rescaled(int newWidth, int newHeight, Graphics::ResamplingQuality)` — resize image
- `juce::FileOutputStream` — write to file

### CommandHandler (engine/src/CommandHandler.h/.cpp)
- `handleCommand(const juce::String& json)` dispatches `{"action":"..."}` commands
- Has `add_track`, `set_tempo`, `set_time_signature` etc.
- Returns JSON with `"status":"ok"` or error

## Implementation Tasks

### 1. Create `gui/src/util/ScreenshotCapture.h` — Screenshot utility header

```cpp
#pragma once

#include <JuceHeader.h>

class MainComponent;

namespace waive
{

/** Captures screenshots of all major UI areas and writes them as compressed JPEGs. */
class ScreenshotCapture
{
public:
    struct Options
    {
        juce::File outputDir;
        int maxWidth = 1400;       // Max pixel width (maintains aspect ratio)
        float jpegQuality = 0.70f; // 0.0–1.0
    };

    /** Capture all areas of the given MainComponent. Returns number of images written. */
    static int captureAll (MainComponent& main, const Options& opts);

private:
    static bool saveImage (const juce::Image& img, const juce::File& path, const Options& opts);
    static juce::Image resizeIfNeeded (const juce::Image& img, int maxWidth);
};

} // namespace waive
```

### 2. Create `gui/src/util/ScreenshotCapture.cpp` — Screenshot utility implementation

```cpp
#include "ScreenshotCapture.h"
#include "MainComponent.h"
#include "SessionComponent.h"
#include "TimelineComponent.h"
#include "MixerComponent.h"
#include "ToolSidebarComponent.h"
#include "ChatPanelComponent.h"
#include "LibraryComponent.h"
#include "PluginBrowserComponent.h"
#include "ConsoleComponent.h"
#include "ToolLogComponent.h"

namespace waive
{

juce::Image ScreenshotCapture::resizeIfNeeded (const juce::Image& img, int maxWidth)
{
    if (img.getWidth() <= maxWidth)
        return img;

    float scale = static_cast<float> (maxWidth) / static_cast<float> (img.getWidth());
    int newH = juce::roundToInt (img.getHeight() * scale);
    return img.rescaled (maxWidth, newH, juce::Graphics::highResamplingQuality);
}

bool ScreenshotCapture::saveImage (const juce::Image& img, const juce::File& path,
                                    const Options& opts)
{
    auto resized = resizeIfNeeded (img, opts.maxWidth);

    juce::JPEGImageFormat jpeg;
    jpeg.setQuality (opts.jpegQuality);

    path.getParentDirectory().createDirectory();
    juce::FileOutputStream stream (path);

    if (stream.failedToOpen())
        return false;

    return jpeg.writeImageToStream (resized, stream);
}

int ScreenshotCapture::captureAll (MainComponent& main, const Options& opts)
{
    int count = 0;
    auto grabAndSave = [&] (juce::Component& comp, const juce::String& name) -> bool
    {
        if (comp.getWidth() <= 0 || comp.getHeight() <= 0)
            return false;

        auto img = comp.createComponentSnapshot (comp.getLocalBounds(), true, 1.0f);
        if (saveImage (img, opts.outputDir.getChildFile (name + ".jpg"), opts))
        {
            ++count;
            return true;
        }
        return false;
    };

    // 1. Full window
    grabAndSave (main, "01_full_window");

    // 2. Session tab components
    auto& session = main.getSessionComponentForTesting();

    // Ensure tool sidebar is visible for its screenshot, then hide after
    // (the toggle methods just flip visibility)
    grabAndSave (session, "02_session_full");
    grabAndSave (session.getTimeline(), "03_timeline");

    // Mixer — access via the SessionComponent's public API
    // The mixer is a child of SessionComponent
    if (auto* mixer = session.findChildWithID ("mixer"))
        grabAndSave (*mixer, "04_mixer");
    else
    {
        // Fallback: find the MixerComponent child by type
        for (int i = 0; i < session.getNumChildComponents(); ++i)
        {
            if (auto* child = session.getChildComponent (i))
            {
                if (child->getName() == "MixerComponent"
                    || dynamic_cast<MixerComponent*> (child) != nullptr)
                {
                    grabAndSave (*child, "04_mixer");
                    break;
                }
            }
        }
    }

    // Tool sidebar (open it, capture, close it)
    auto* sidebar = session.getToolSidebar();
    bool sidebarWasVisible = sidebar != nullptr && sidebar->isVisible();
    if (sidebar != nullptr && ! sidebarWasVisible)
        session.toggleToolSidebar();
    if (sidebar != nullptr && sidebar->isVisible())
        grabAndSave (*sidebar, "05_tool_sidebar");
    if (sidebar != nullptr && ! sidebarWasVisible)
        session.toggleToolSidebar(); // restore

    // Chat panel (open it, capture, close it)
    auto* chat = session.getChatPanelForTesting();
    bool chatWasVisible = chat != nullptr && chat->isVisible();
    if (chat != nullptr && ! chatWasVisible)
        session.toggleChatPanel();
    if (chat != nullptr && chat->isVisible())
        grabAndSave (*chat, "06_chat_panel");
    if (chat != nullptr && ! chatWasVisible)
        session.toggleChatPanel(); // restore

    // 3. Other tabs — switch to each, capture, switch back
    // The TabbedComponent shows tab content when selected.
    // We need to find the tabs and switch.
    // MainComponent has a `tabs` member but it's private.
    // Instead, capture via the test helper accessors which return the owned components.

    grabAndSave (main.getLibraryComponentForTesting(), "07_library");
    grabAndSave (main.getPluginBrowserForTesting(), "08_plugin_browser");

    return count;
}

} // namespace waive
```

Note: The `MixerComponent` access may need adjustment. If `SessionComponent` does not expose the mixer via a public getter or component ID, add a simple accessor:

In `gui/src/ui/SessionComponent.h`, add alongside the existing test helpers:
```cpp
MixerComponent& getMixerForTesting();
```

In `gui/src/ui/SessionComponent.cpp`:
```cpp
MixerComponent& SessionComponent::getMixerForTesting() { return *mixer; }
```

Then update `ScreenshotCapture.cpp` to use `session.getMixerForTesting()` instead of the `findChildWithID` / dynamic_cast fallback.

### 3. Modify `gui/src/Main.cpp` — Add `--screenshot` mode

In `WaiveApplication`, add private members:
```cpp
bool screenshotMode = false;
juce::File screenshotOutputDir;
```

In `initialise()`, before the `if (te::PluginManager::startChildProcessPluginScan(...))` line, parse the command line:
```cpp
auto args = juce::StringArray::fromTokens (commandLine, true);

int ssIdx = args.indexOf ("--screenshot");
if (ssIdx >= 0 && ssIdx + 1 < args.size())
{
    screenshotMode = true;
    screenshotOutputDir = juce::File (args[ssIdx + 1]);
    // Remove these args so plugin scan doesn't see them
    args.remove (ssIdx + 1);
    args.remove (ssIdx);
}
```

At the end of `initialise()`, after the window is created and visible, if in screenshot mode schedule a deferred capture:
```cpp
if (screenshotMode)
{
    // Defer to let the UI render fully
    juce::Timer::callAfterDelay (1500, [this]()
    {
        if (mainWindow == nullptr) return;

        auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
        if (mc == nullptr) return;

        // Optionally seed demo content so screenshots aren't empty
        seedDemoContent();

        // Give demo content a moment to render
        juce::Timer::callAfterDelay (500, [this, mc]()
        {
            waive::ScreenshotCapture::Options opts;
            opts.outputDir = screenshotOutputDir;
            opts.maxWidth = 1400;
            opts.jpegQuality = 0.70f;

            int captured = waive::ScreenshotCapture::captureAll (*mc, opts);
            DBG ("Screenshot mode: captured " + juce::String (captured) + " images to "
                 + screenshotOutputDir.getFullPathName());

            quit();
        });
    });
}
```

Add `#include "ScreenshotCapture.h"` at the top of Main.cpp.

Add a `seedDemoContent()` private method to create some tracks so the UI is not empty:
```cpp
void seedDemoContent()
{
    if (! editSession) return;
    auto& edit = editSession->getEdit();

    // Ensure at least 4 tracks exist
    int numTracks = static_cast<int> (te::getAudioTracks (edit).size());
    if (numTracks < 4)
        edit.ensureNumberOfAudioTracks (4);

    // Name them so the mixer looks populated
    auto tracks = te::getAudioTracks (edit);
    juce::StringArray names = { "Drums", "Bass", "Guitar", "Vocals" };
    for (int i = 0; i < juce::jmin (tracks.size(), names.size()); ++i)
        tracks[i]->setName (names[i]);

    // Set a reasonable tempo
    edit.tempoSequence.getTempo (0)->setBpm (120.0);
}
```

### 4. Register new source files in `gui/CMakeLists.txt`

Add to the `# Utilities` section:
```cmake
    src/util/ScreenshotCapture.h
    src/util/ScreenshotCapture.cpp
```

### 5. Create `scripts/ui_review.sh` — Headless screenshot + AI review script

Create this script at `scripts/ui_review.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# ui_review.sh — Capture Waive screenshots and send to Claude for UI/UX review
#
# Usage:
#   ./scripts/ui_review.sh [--skip-build] [--quality 0.70] [--max-width 1400]
#
# Requirements:
#   - xvfb-run (apt install xvfb)
#   - ANTHROPIC_API_KEY environment variable
#   - ImageMagick (optional, for additional compression)
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
BINARY="$BUILD_DIR/gui/Waive_artefacts/Release/Waive"
OUTPUT_DIR="$PROJECT_DIR/screenshots"
REVIEW_FILE="$OUTPUT_DIR/review.md"

SKIP_BUILD=false
JPEG_QUALITY="0.70"
MAX_WIDTH="1400"

# ── Parse args ──────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build)  SKIP_BUILD=true; shift ;;
        --quality)     JPEG_QUALITY="$2"; shift 2 ;;
        --max-width)   MAX_WIDTH="$2"; shift 2 ;;
        *)             echo "Unknown arg: $1"; exit 1 ;;
    esac
done

log() { echo "[$(date '+%H:%M:%S')] $*"; }

# ── Build ───────────────────────────────────────────────────────────────────
if [[ "$SKIP_BUILD" == "false" ]]; then
    log "Building Waive..."
    cmake -B "$BUILD_DIR" "$PROJECT_DIR" 2>&1 | tail -3
    cmake --build "$BUILD_DIR" --target Waive -j$(($(nproc)/2)) 2>&1 | tail -5
    log "Build complete."
fi

if [[ ! -x "$BINARY" ]]; then
    log "ERROR: Binary not found at $BINARY"
    exit 1
fi

# ── Capture screenshots ────────────────────────────────────────────────────
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

log "Capturing screenshots under xvfb..."
xvfb-run -a -s "-screen 0 1920x1080x24" \
    "$BINARY" --screenshot "$OUTPUT_DIR" 2>&1 | tail -5 || true

# Count captured images
IMG_COUNT=$(find "$OUTPUT_DIR" -name "*.jpg" | wc -l)
log "Captured $IMG_COUNT screenshots."

if [[ "$IMG_COUNT" -eq 0 ]]; then
    log "ERROR: No screenshots were captured."
    exit 1
fi

# ── Optional: further compress with ImageMagick ────────────────────────────
if command -v convert &>/dev/null; then
    log "Compressing with ImageMagick (quality ${JPEG_QUALITY}, max-width ${MAX_WIDTH})..."
    for img in "$OUTPUT_DIR"/*.jpg; do
        convert "$img" -resize "${MAX_WIDTH}x>" -quality "$(echo "$JPEG_QUALITY * 100" | bc | cut -d. -f1)" "$img"
    done
    log "Compression complete."
fi

# ── Report file sizes ──────────────────────────────────────────────────────
log "Screenshot sizes:"
for img in "$OUTPUT_DIR"/*.jpg; do
    size=$(du -h "$img" | cut -f1)
    echo "  $(basename "$img"): $size"
done

# ── Send to Claude API for review ──────────────────────────────────────────
if [[ -z "${ANTHROPIC_API_KEY:-}" ]]; then
    log "ANTHROPIC_API_KEY not set — skipping AI review."
    log "Screenshots saved to: $OUTPUT_DIR"
    exit 0
fi

log "Sending screenshots to Claude for UI/UX review..."

# Build the content array with all images
CONTENT='[]'
for img in "$OUTPUT_DIR"/*.jpg; do
    base64_data=$(base64 -w0 "$img")
    filename=$(basename "$img" .jpg)
    # Add a text block naming the image, then the image block
    CONTENT=$(echo "$CONTENT" | jq --arg name "$filename" --arg data "$base64_data" \
        '. + [
            {"type": "text", "text": ("--- Screenshot: " + $name + " ---")},
            {"type": "image", "source": {"type": "base64", "media_type": "image/jpeg", "data": $data}}
        ]')
done

# Add the review prompt at the end
REVIEW_PROMPT='You are reviewing the UI/UX of "Waive", a desktop DAW (Digital Audio Workstation) built with JUCE. The app uses a dark theme with a custom color palette.

For each screenshot, analyze:
1. **Layout & Spacing** — Are elements well-aligned? Is spacing consistent? Are there crowded or empty areas?
2. **Visual Hierarchy** — Is it clear what is most important? Do headers, labels, and controls have appropriate weight?
3. **Color & Contrast** — Is text readable? Are interactive elements distinguishable? Does the palette feel cohesive?
4. **Component Design** — Do buttons, sliders, meters, and panels look polished? Any rough edges?
5. **Overall Impressions** — Does the app look professional? What are the top 3 improvements you would make?

For each issue, be specific: name the component, describe the problem, and suggest a concrete fix (including approximate pixel values, colors, or layout changes where applicable).

Output your review as structured Markdown with a section per screenshot, then a final "Priority Fixes" section ranking the top 5 actionable improvements.'

CONTENT=$(echo "$CONTENT" | jq --arg prompt "$REVIEW_PROMPT" \
    '. + [{"type": "text", "text": $prompt}]')

# Build the full API request
REQUEST=$(jq -n \
    --arg model "claude-sonnet-4-5-20250929" \
    --argjson content "$CONTENT" \
    '{
        model: $model,
        max_tokens: 4096,
        messages: [{role: "user", content: $content}]
    }')

RESPONSE=$(curl -s https://api.anthropic.com/v1/messages \
    -H "Content-Type: application/json" \
    -H "x-api-key: $ANTHROPIC_API_KEY" \
    -H "anthropic-version: 2023-06-01" \
    -d "$REQUEST")

# Extract text from response
REVIEW=$(echo "$RESPONSE" | jq -r '.content[0].text // .error.message // "Failed to parse response"')

echo "$REVIEW" > "$REVIEW_FILE"
log "Review saved to: $REVIEW_FILE"
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "$REVIEW"
echo "═══════════════════════════════════════════════════════════════"
```

Make it executable: `chmod +x scripts/ui_review.sh`

### 6. Add `getMixerForTesting()` accessor to SessionComponent

In `gui/src/ui/SessionComponent.h`, add in the public test-helpers section alongside the existing helpers:
```cpp
class MixerComponent; // forward-declare if not already
// ... existing test helpers ...
MixerComponent& getMixerForTesting();
```

In `gui/src/ui/SessionComponent.cpp`, implement:
```cpp
MixerComponent& SessionComponent::getMixerForTesting()
{
    return *mixer;
}
```

Then update `ScreenshotCapture.cpp` to use:
```cpp
grabAndSave (session.getMixerForTesting(), "04_mixer");
```
instead of the `findChildWithID` / `dynamic_cast` fallback logic.

## Files Expected To Change
- `gui/src/util/ScreenshotCapture.h` (NEW)
- `gui/src/util/ScreenshotCapture.cpp` (NEW)
- `gui/src/Main.cpp`
- `gui/src/ui/SessionComponent.h`
- `gui/src/ui/SessionComponent.cpp`
- `gui/CMakeLists.txt`
- `scripts/ui_review.sh` (NEW)

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

Also verify screenshot mode works:
```bash
xvfb-run -a -s "-screen 0 1920x1080x24" build/gui/Waive_artefacts/Release/Waive --screenshot /tmp/waive_screenshots
ls -la /tmp/waive_screenshots/
```

## Exit Criteria
- `Waive --screenshot <dir>` captures at least 7 JPEG images (full window, session, timeline, mixer, tool sidebar, chat panel, library, plugin browser) and exits cleanly.
- Each JPEG is resized to at most 1400px wide and compressed at 70% quality (typically 100–300KB per image).
- `scripts/ui_review.sh` builds the app, runs it under xvfb, captures screenshots, and (if ANTHROPIC_API_KEY is set) sends them to Claude and saves a Markdown review.
- `scripts/ui_review.sh --skip-build` works when the binary is already built.
- Build compiles with no errors.
- Existing tests still pass (`ctest --test-dir build --output-on-failure`).
