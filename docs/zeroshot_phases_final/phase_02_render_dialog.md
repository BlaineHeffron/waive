# Phase 02: Render Dialog & Export Format Support

## Objective
Replace the bare FileChooser render workflow with a proper RenderDialog component that supports multiple audio formats (WAV, FLAC, OGG Vorbis), sample rate/bit depth selection, normalize option, and a progress bar during rendering.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Current Export Implementation
- `engine/src/CommandHandler.cpp` lines 1072-1207: `export_mixdown` and `export_stems` commands
- WAV only, hardcoded via `te::Renderer::renderToFile()`
- `gui/src/MainComponent.cpp` lines 509-528: Menu item "File → Render..." opens a plain FileChooser for output path, then calls the command
- No format selection, no quality options, no progress feedback

### JUCE Audio Format Support
JUCE natively supports writing:
- WAV (always available)
- AIFF (always available)
- FLAC (via `juce::FlacAudioFormat`)
- OGG Vorbis (via `juce::OggVorbisAudioFormat`)

MP3 writing is NOT available in JUCE (patent licensing). Do NOT attempt MP3 export.

### Tracktion Renderer API
`te::Renderer::renderToFile()` renders to a `juce::File` using a `juce::AudioFormat*`. The format determines the output codec. The method signature accepts an `AudioFormat` pointer, so we can pass any supported format.

Key API:
```cpp
te::Renderer::renderToFile (
    const juce::String& taskDescription,
    const juce::File& outputFile,
    te::Edit& edit,
    te::TimeRange range,
    const juce::BigInteger& tracksToDo,
    bool usePlugins,
    juce::Array<te::Clip*> clips,
    bool useThread
);
```

## Implementation Tasks

### 1. Create RenderDialog component

Create `gui/src/ui/RenderDialog.h` and `gui/src/ui/RenderDialog.cpp`.

**RenderDialog** is a `juce::DialogWindow` content component with:

#### Format Selection
- ComboBox with options: WAV, FLAC, OGG Vorbis
- Default: WAV
- File extension updates automatically (.wav, .flac, .ogg)

#### Quality Options
- **Sample Rate** ComboBox: 44100, 48000, 88200, 96000 (default: match project)
- **Bit Depth** ComboBox: 16, 24, 32 (default: 24) — only for WAV/FLAC
- **OGG Quality** Slider: 0.0-1.0 (default: 0.6) — only visible when OGG selected

#### Range Selection
- **Render Range** ComboBox: "Entire Project", "Loop Region", "Custom"
- Start/End time editors (visible only for "Custom")
- When "Loop Region" selected, reads loop range from edit

#### Output Options
- **Normalize** ToggleButton: normalize to -0.1dBFS after render
- **Mixdown / Stems** RadioButtons: single file vs per-track export
- **Output Path** with Browse button (FileChooser)

#### Render Button
- "Render" button at bottom
- When clicked, triggers the render with a progress bar

### 2. Implement format-aware rendering

In the dialog's render callback:

```cpp
std::unique_ptr<juce::AudioFormat> format;
if (selectedFormat == "WAV")
    format = std::make_unique<juce::WavAudioFormat>();
else if (selectedFormat == "FLAC")
    format = std::make_unique<juce::FlacAudioFormat>();
else if (selectedFormat == "OGG")
    format = std::make_unique<juce::OggVorbisAudioFormat>();
```

For the actual render, use Tracktion's render API. The rendering should happen on a background thread. Show progress using a `juce::ProgressBar` or `juce::AlertWindow::showMessageBoxAsync` on completion.

For stems mode, iterate tracks and render each to `output_dir/01_TrackName.ext`.

### 3. Add progress feedback

During render:
- Disable all dialog controls
- Show an indeterminate progress bar (Tracktion's render API doesn't expose per-sample progress easily)
- On completion, show success message with file path and size
- On failure, show error message

### 4. Wire into MainComponent

Replace the current render handler in `gui/src/MainComponent.cpp` (the `cmdRender` case in `perform()`) to open the new RenderDialog instead of a bare FileChooser.

### 5. Add to CMakeLists.txt

Add `RenderDialog.h` and `RenderDialog.cpp` to `gui/CMakeLists.txt` under the UI components section.

## Files Expected To Change
- `gui/src/ui/RenderDialog.h` (create)
- `gui/src/ui/RenderDialog.cpp` (create)
- `gui/src/MainComponent.cpp` (replace render handler)
- `gui/CMakeLists.txt` (add new files)

## Validation

```bash
# Build must succeed
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# All tests must still pass
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- RenderDialog opens from File → Render
- Format selection works (WAV, FLAC, OGG)
- Sample rate and bit depth options available
- Mixdown vs stems toggle works
- Progress feedback during render
- All existing tests still pass
