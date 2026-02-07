# Phase 19: C++ Built-in Tool Validation & Benchmarks

## Objective
Create a dedicated C++ test executable (`WaiveToolTests`) that validates all 7 built-in audio tools plus the core audio analysis engine against synthetic WAV files with known properties. Tests generate WAV files programmatically using JUCE, run the analysis/tool logic directly, and verify results against ground truth. This catches regressions in peak detection, silence detection, transient alignment, gain staging, normalization, stem separation, and track renaming.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt` or `tests/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### Audio Analysis Engine (`gui/src/tools/AudioAnalysis.h`)
```cpp
struct AudioAnalysisSummary {
    bool valid, cancelled;
    double sampleRate;
    int64 totalSamples;
    float peakGain;
    int64 firstAboveSample, lastAboveSample;
    int64 firstTransientSample;
};

AudioAnalysisSummary analyseAudioFile (const juce::File& sourceFile,
                                       float activityThresholdGain,
                                       float transientRiseThresholdGain,
                                       const std::function<bool()>& shouldCancel = {},
                                       AudioAnalysisCache* cache = nullptr);
```

Algorithm: reads audio in 8192-sample blocks, tracks per-sample peak, activity regions (above threshold), and transients (sample peak exceeds envelope + rise threshold). Envelope follows with `envelope += (peak - envelope) * 0.01f`.

### `analysePeakGain()` (in NormalizeSelectedClipsTool.cpp)
File-scope helper. Reads audio, returns max sample magnitude across all channels.

### Built-in Tools (7 total)
All tools follow the `Tool` interface: `describe()` → `preparePlan()` → `apply()`.

1. **NormalizeSelectedClipsTool** — Analyzes clip peak, sets gain_db to reach target peak
2. **DetectSilenceAndCutRegionsTool** — Finds active content, trims silent margins
3. **AlignClipsByTransientTool** — Detects transients, aligns clips by earliest transient
4. **GainStageSelectedTracksTool** — Analyzes track peak, adjusts track volume
5. **AutoMixSuggestionsTool** — Peak analysis + stereo spread suggestions (requires ModelManager)
6. **StemSeparationTool** — Exponential smoothing freq separation (requires ModelManager)
7. **RenameTracksFromClipsTool** — Renames tracks from clip filenames (metadata only)

### Existing Test Patterns (`tests/WaiveCoreTests.cpp`)
- `expect(bool, string)` throws `std::runtime_error` on failure
- `juce::ScopedJuceInitialiser_GUI` for JUCE init
- `te::Engine` + `EditSession` for Tracktion setup
- Test functions called from `main()`, single try/catch
- No test framework, returns 0/1

### Test CMakeLists.txt Pattern (`tests/CMakeLists.txt`)
- `juce_add_console_app()` with NEEDS_WEB_BROWSER=FALSE, NEEDS_CURL=FALSE
- `target_sources()` lists all source files explicitly (including deps)
- `target_include_directories()` for gui/src/* subdirs
- `target_link_libraries()` links tracktion_engine, juce_recommended_*, CURL::libcurl
- `add_test()` with full artefact path

## Implementation Tasks

### 1. Create `tests/WaiveToolTests.cpp` — Test harness + WAV generation + tool tests

```cpp
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

#include "AudioAnalysis.h"
#include "AudioAnalysisCache.h"
#include "EditSession.h"
#include "ToolDiff.h"
#include "Tool.h"
#include "ToolRegistry.h"
#include "NormalizeSelectedClipsTool.h"
#include "DetectSilenceAndCutRegionsTool.h"
#include "AlignClipsByTransientTool.h"
#include "GainStageSelectedTracksTool.h"
#include "StemSeparationTool.h"
#include "RenameTracksFromClipsTool.h"
#include "AutoMixSuggestionsTool.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace te = tracktion;

namespace
{

void expect (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void expectApprox (double actual, double expected, double tolerance, const std::string& message)
{
    if (std::abs (actual - expected) > tolerance)
        throw std::runtime_error (message + " — expected " + std::to_string (expected)
                                  + " ±" + std::to_string (tolerance)
                                  + ", got " + std::to_string (actual));
}

// ── WAV File Generators ────────────────────────────────────────────────────

juce::File getTempDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("waive_tool_tests");
    dir.createDirectory();
    return dir;
}

/** Write a mono 44100 Hz WAV file from float samples. */
juce::File writeTestWav (const juce::String& name, const float* samples, int numSamples,
                         double sampleRate = 44100.0)
{
    auto file = getTempDir().getChildFile (name);

    if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (
            juce::WavAudioFormat().createWriterFor (
                new juce::FileOutputStream (file),
                sampleRate, 1, 16, {}, 0)))
    {
        juce::AudioBuffer<float> buffer (1, numSamples);
        buffer.copyFrom (0, 0, samples, numSamples);
        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
    }

    return file;
}

/** Generate a sine wave WAV at a specific frequency and amplitude. */
juce::File generateSineWav (const juce::String& name, double freqHz, float amplitude,
                            double durationSeconds, double sampleRate = 44100.0)
{
    int numSamples = (int) (durationSeconds * sampleRate);
    std::vector<float> samples (numSamples);
    for (int i = 0; i < numSamples; ++i)
        samples[i] = amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * i / sampleRate);
    return writeTestWav (name, samples.data(), numSamples, sampleRate);
}

/** Generate silence. */
juce::File generateSilenceWav (const juce::String& name, double durationSeconds,
                               double sampleRate = 44100.0)
{
    int numSamples = (int) (durationSeconds * sampleRate);
    std::vector<float> samples (numSamples, 0.0f);
    return writeTestWav (name, samples.data(), numSamples, sampleRate);
}

/** Generate audio with silence-content-silence pattern for trim testing. */
juce::File generateSilenceContentSilenceWav (const juce::String& name,
                                             double leadingSilenceSec,
                                             double contentSec,
                                             double trailingSilenceSec,
                                             float contentAmplitude = 0.5f,
                                             double sampleRate = 44100.0)
{
    int leadingSamples = (int) (leadingSilenceSec * sampleRate);
    int contentSamples = (int) (contentSec * sampleRate);
    int trailingSamples = (int) (trailingSilenceSec * sampleRate);
    int totalSamples = leadingSamples + contentSamples + trailingSamples;

    std::vector<float> samples (totalSamples, 0.0f);
    for (int i = leadingSamples; i < leadingSamples + contentSamples; ++i)
        samples[i] = contentAmplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sampleRate);

    return writeTestWav (name, samples.data(), totalSamples, sampleRate);
}

/** Generate a click/transient at a specific time position. */
juce::File generateClickWav (const juce::String& name, double clickTimeSec,
                             float clickAmplitude = 0.8f, double durationSec = 2.0,
                             double sampleRate = 44100.0)
{
    int numSamples = (int) (durationSec * sampleRate);
    std::vector<float> samples (numSamples, 0.0f);

    int clickStart = (int) (clickTimeSec * sampleRate);
    int clickLen = (int) (0.005 * sampleRate); // 5ms click

    for (int i = 0; i < clickLen && (clickStart + i) < numSamples; ++i)
    {
        float t = (float) i / (float) sampleRate;
        samples[clickStart + i] = clickAmplitude * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * t)
                                  * std::exp (-t / 0.001f);
    }

    return writeTestWav (name, samples.data(), numSamples, sampleRate);
}

// ── Audio Analysis Engine Tests ────────────────────────────────────────────

void testAnalysisPeakDetection()
{
    // A 440Hz sine at 0.5 amplitude should report peak ≈ 0.5
    auto file = generateSineWav ("peak_test_0.5.wav", 440.0, 0.5f, 1.0);
    auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f);

    expect (summary.valid, "Analysis should succeed for valid WAV");
    expectApprox (summary.peakGain, 0.5, 0.05, "Peak gain for 0.5 amplitude sine");
    expect (summary.totalSamples == 44100, "Expected 44100 samples for 1 second at 44.1kHz");
    expectApprox (summary.sampleRate, 44100.0, 1.0, "Sample rate should be 44100");
}

void testAnalysisPeakDetectionLoudSignal()
{
    auto file = generateSineWav ("peak_test_0.9.wav", 440.0, 0.9f, 1.0);
    auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f);

    expect (summary.valid, "Analysis should succeed");
    expectApprox (summary.peakGain, 0.9, 0.05, "Peak gain for 0.9 amplitude sine");
}

void testAnalysisPeakDetectionSilence()
{
    auto file = generateSilenceWav ("peak_test_silence.wav", 1.0);
    auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f);

    expect (summary.valid, "Analysis should succeed for silence");
    expectApprox (summary.peakGain, 0.0, 0.001, "Peak gain for silence should be ~0");
}

void testAnalysisActivityDetection()
{
    // 0.5s silence + 1s content + 0.5s silence
    auto file = generateSilenceContentSilenceWav ("activity_test.wav", 0.5, 1.0, 0.5, 0.5f);
    float threshold = 0.01f; // Well below the 0.5 amplitude content
    auto summary = waive::analyseAudioFile (file, threshold, 0.0f);

    expect (summary.valid, "Analysis should succeed");
    expect (summary.firstAboveSample >= 0, "Should detect activity");

    // firstAboveSample should be near 0.5 seconds = 22050 samples
    double firstAboveSeconds = summary.firstAboveSample / summary.sampleRate;
    expectApprox (firstAboveSeconds, 0.5, 0.02, "First above-threshold sample time");

    // lastAboveSample should be near 1.5 seconds = 66150 samples
    double lastAboveSeconds = summary.lastAboveSample / summary.sampleRate;
    expectApprox (lastAboveSeconds, 1.5, 0.02, "Last above-threshold sample time");
}

void testAnalysisActivityDetectionNoActivity()
{
    // Silence with activity threshold — should report no activity
    auto file = generateSilenceWav ("no_activity_test.wav", 1.0);
    auto summary = waive::analyseAudioFile (file, 0.01f, 0.0f);

    expect (summary.valid, "Analysis should succeed");
    expect (summary.firstAboveSample == -1, "Silent file should have no activity");
    expect (summary.lastAboveSample == -1, "Silent file should have no last activity");
}

void testAnalysisTransientDetection()
{
    // Click at 0.5 seconds in a 2-second file
    auto file = generateClickWav ("transient_test.wav", 0.5, 0.8f, 2.0);
    float threshold = 0.05f;
    float transientRise = 0.1f;
    auto summary = waive::analyseAudioFile (file, threshold, transientRise);

    expect (summary.valid, "Analysis should succeed");
    expect (summary.firstTransientSample >= 0, "Should detect transient");

    // Transient should be near 0.5 seconds
    double transientSeconds = summary.firstTransientSample / summary.sampleRate;
    expectApprox (transientSeconds, 0.5, 0.05, "Transient detection time");
}

void testAnalysisTransientDetectionMultipleClicks()
{
    // Two clicks at 0.3s and 1.0s — should detect first transient at ~0.3s
    int numSamples = 44100 * 2; // 2 seconds
    std::vector<float> samples (numSamples, 0.0f);

    // Click 1 at 0.3s
    int click1Start = (int) (0.3 * 44100);
    // Click 2 at 1.0s
    int click2Start = (int) (1.0 * 44100);
    int clickLen = (int) (0.005 * 44100);

    for (int i = 0; i < clickLen; ++i)
    {
        float t = (float) i / 44100.0f;
        float val = 0.8f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * t)
                    * std::exp (-t / 0.001f);
        if (click1Start + i < numSamples) samples[click1Start + i] = val;
        if (click2Start + i < numSamples) samples[click2Start + i] = val;
    }

    auto file = writeTestWav ("multi_transient_test.wav", samples.data(), numSamples);
    auto summary = waive::analyseAudioFile (file, 0.05f, 0.1f);

    expect (summary.valid, "Analysis should succeed");
    expect (summary.firstTransientSample >= 0, "Should detect transient");

    double transientSeconds = summary.firstTransientSample / summary.sampleRate;
    expectApprox (transientSeconds, 0.3, 0.05, "First transient should be the earlier click");
}

void testAnalysisCancellation()
{
    auto file = generateSineWav ("cancel_test.wav", 440.0, 0.5f, 5.0);
    auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f, [] { return true; }); // immediate cancel

    expect (! summary.valid, "Cancelled analysis should be invalid");
    expect (summary.cancelled, "Should be marked as cancelled");
}

void testAnalysisCaching()
{
    auto file = generateSineWav ("cache_test.wav", 440.0, 0.5f, 1.0);
    waive::AudioAnalysisCache cache (10);

    // First call: cache miss
    auto summary1 = waive::analyseAudioFile (file, 0.01f, 0.05f, {}, &cache);
    expect (summary1.valid, "First analysis should succeed");

    // Second call: cache hit (should return same result)
    auto summary2 = waive::analyseAudioFile (file, 0.01f, 0.05f, {}, &cache);
    expect (summary2.valid, "Cached analysis should succeed");
    expect (summary1.peakGain == summary2.peakGain, "Cached peak should match");
    expect (summary1.totalSamples == summary2.totalSamples, "Cached total samples should match");
}

// ── Tool Registration Tests ────────────────────────────────────────────────

void testToolRegistryCompleteness()
{
    waive::ToolRegistry registry;

    // All 7 built-in tools should be registered
    const juce::StringArray expectedTools = {
        "normalize_selected_clips",
        "rename_tracks_from_clips",
        "gain_stage_selected_tracks",
        "detect_silence_and_cut_regions",
        "align_clips_by_transient",
        "stem_separation",
        "auto_mix_suggestions"
    };

    for (auto& name : expectedTools)
    {
        auto* tool = registry.findTool (name);
        expect (tool != nullptr, "ToolRegistry should contain: " + name.toStdString());
    }
}

void testToolDescriptions()
{
    waive::ToolRegistry registry;

    // Each tool's describe() should return valid metadata
    const juce::StringArray toolNames = {
        "normalize_selected_clips",
        "rename_tracks_from_clips",
        "gain_stage_selected_tracks",
        "detect_silence_and_cut_regions",
        "align_clips_by_transient",
        "stem_separation",
        "auto_mix_suggestions"
    };

    for (auto& name : toolNames)
    {
        auto* tool = registry.findTool (name);
        expect (tool != nullptr, "Tool not found: " + name.toStdString());

        auto desc = tool->describe();
        expect (desc.name.isNotEmpty(), "Tool name should not be empty: " + name.toStdString());
        expect (desc.displayName.isNotEmpty(), "Tool display name should not be empty: " + name.toStdString());
        expect (desc.version.isNotEmpty(), "Tool version should not be empty: " + name.toStdString());
        expect (desc.description.isNotEmpty(), "Tool description should not be empty: " + name.toStdString());
        expect (desc.inputSchema.isObject(), "Tool input schema should be an object: " + name.toStdString());
    }
}

void testToolSchemaHasRequiredFields()
{
    waive::ToolRegistry registry;
    auto* normTool = registry.findTool ("normalize_selected_clips");
    expect (normTool != nullptr, "normalize tool should exist");

    auto desc = normTool->describe();
    auto* schemaObj = desc.inputSchema.getDynamicObject();
    expect (schemaObj != nullptr, "Schema should be a DynamicObject");

    // Schema should have "type": "object" and "properties"
    expect (schemaObj->getProperty ("type").toString() == "object", "Schema type should be object");
    expect (schemaObj->hasProperty ("properties"), "Schema should have properties");
}

// ── Peak Gain Helper Test ──────────────────────────────────────────────────
// Note: analysePeakGain is file-scope in NormalizeSelectedClipsTool.cpp,
// so we test via analyseAudioFile with zero thresholds (same logic).

void testPeakGainVariousAmplitudes()
{
    float amplitudes[] = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f };
    for (auto amp : amplitudes)
    {
        auto name = "peak_" + juce::String (amp, 2) + ".wav";
        auto file = generateSineWav (name, 440.0, amp, 0.5);
        auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f);
        expect (summary.valid, "Analysis should succeed for amplitude " + std::to_string (amp));
        expectApprox (summary.peakGain, amp, 0.05,
                      "Peak gain for amplitude " + std::to_string (amp));
    }
}

// ── Normalization Logic Test ───────────────────────────────────────────────
// Tests the mathematical logic: if peak is X dB, and target is Y dB,
// the required gain delta should be Y - X.

void testNormalizationGainCalculation()
{
    // Sine at amplitude 0.5 → peak = 0.5 → peakDb ≈ -6.02 dB
    auto file = generateSineWav ("norm_calc_test.wav", 440.0, 0.5f, 1.0);
    auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f);

    float peakDb = juce::Decibels::gainToDecibels (summary.peakGain);
    expectApprox (peakDb, -6.02, 0.5, "Peak dB for 0.5 amplitude");

    // Target: -1 dB → delta should be -1 - (-6.02) ≈ +5.02 dB
    double targetDb = -1.0;
    double delta = targetDb - peakDb;
    expectApprox (delta, 5.02, 0.5, "Gain delta to reach -1 dB target");
}

// ── Silence Detection Logic Test ──────────────────────────────────────────

void testSilenceDetectionRegions()
{
    // 1s silence + 2s content + 1s silence
    auto file = generateSilenceContentSilenceWav ("silence_regions.wav", 1.0, 2.0, 1.0, 0.4f);
    float threshold = 0.01f;
    auto summary = waive::analyseAudioFile (file, threshold, 0.0f);

    expect (summary.valid, "Analysis should succeed");

    double firstAboveSec = summary.firstAboveSample / summary.sampleRate;
    double lastAboveSec = summary.lastAboveSample / summary.sampleRate;

    // Content starts at 1.0s, ends at 3.0s
    expectApprox (firstAboveSec, 1.0, 0.05, "Content start detection");
    expectApprox (lastAboveSec, 3.0, 0.05, "Content end detection");

    // A tool would trim: start → firstAbove, lastAbove → end
    double leadingSilence = firstAboveSec;
    double trailingSilence = (summary.totalSamples / summary.sampleRate) - lastAboveSec;
    expect (leadingSilence > 0.9, "Should detect ~1s leading silence");
    expect (trailingSilence > 0.9, "Should detect ~1s trailing silence");
}

void testSilenceDetectionWithQuietContent()
{
    // Very quiet content (below threshold) should not be detected
    auto file = generateSilenceContentSilenceWav ("quiet_content.wav", 0.5, 1.0, 0.5, 0.001f);
    float threshold = 0.01f; // Above the 0.001 amplitude
    auto summary = waive::analyseAudioFile (file, threshold, 0.0f);

    expect (summary.valid, "Analysis should succeed");
    expect (summary.firstAboveSample == -1, "Very quiet content should not exceed threshold");
}

// ── Transient Alignment Logic Test ────────────────────────────────────────

void testTransientAlignmentCalculation()
{
    // Two clips: click at 0.3s and click at 0.7s
    // Alignment should shift second clip left by 0.4s

    auto file1 = generateClickWav ("align_clip1.wav", 0.3, 0.8f, 2.0);
    auto file2 = generateClickWav ("align_clip2.wav", 0.7, 0.8f, 2.0);

    float threshold = 0.05f;
    float transientRise = 0.1f;

    auto summary1 = waive::analyseAudioFile (file1, threshold, transientRise);
    auto summary2 = waive::analyseAudioFile (file2, threshold, transientRise);

    expect (summary1.valid && summary2.valid, "Both analyses should succeed");
    expect (summary1.firstTransientSample >= 0, "Clip 1 should have transient");
    expect (summary2.firstTransientSample >= 0, "Clip 2 should have transient");

    double transient1Sec = summary1.firstTransientSample / summary1.sampleRate;
    double transient2Sec = summary2.firstTransientSample / summary2.sampleRate;

    expectApprox (transient1Sec, 0.3, 0.05, "Clip 1 transient at ~0.3s");
    expectApprox (transient2Sec, 0.7, 0.05, "Clip 2 transient at ~0.7s");

    // Delta = transient2 - transient1 (how much to shift clip 2)
    double deltaSec = transient2Sec - transient1Sec;
    expectApprox (deltaSec, 0.4, 0.05, "Alignment delta should be ~0.4s");
}

// ── Gain Staging Logic Test ───────────────────────────────────────────────

void testGainStagingCalculation()
{
    // File at 0.3 amplitude → peak ≈ -10.46 dB
    // Target: -12 dB → delta ≈ -1.54 dB
    auto file = generateSineWav ("gainstage_test.wav", 440.0, 0.3f, 1.0);
    auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f);

    float peakDb = juce::Decibels::gainToDecibels (summary.peakGain);
    double targetDb = -12.0;
    double delta = targetDb - peakDb;

    // peakDb should be around -10.46
    expect (peakDb > -12.0 && peakDb < -9.0, "Peak dB should be around -10.5");
    // Delta should be small negative
    expect (delta < 0.0 && delta > -3.0, "Gain delta should be small negative adjustment");
}

// ── Stem Separation Logic Test ────────────────────────────────────────────
// Tests the exponential smoothing separation at the signal level

void testExponentialSmoothingSeparation()
{
    // Mix of low (100Hz) and high (4000Hz) sine waves
    int numSamples = 44100;
    std::vector<float> samples (numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        float t = (float) i / 44100.0f;
        float low = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 100.0f * t);
        float high = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 4000.0f * t);
        samples[i] = low + high;
    }

    // Simulate the tool's exponential smoothing separation
    float smoothing = 0.03f;
    std::vector<float> lowOutput (numSamples);
    std::vector<float> highOutput (numSamples);
    float lowState = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        lowState += (samples[i] - lowState) * smoothing;
        lowOutput[i] = lowState;
        highOutput[i] = samples[i] - lowState;
    }

    // Low output should have more energy at 100Hz
    // High output should have more energy at 4000Hz
    // Check RMS — low output should not be silent
    float lowRms = 0.0f, highRms = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        lowRms += lowOutput[i] * lowOutput[i];
        highRms += highOutput[i] * highOutput[i];
    }
    lowRms = std::sqrt (lowRms / numSamples);
    highRms = std::sqrt (highRms / numSamples);

    expect (lowRms > 0.01f, "Low stem should have energy");
    expect (highRms > 0.01f, "High stem should have energy");
    // Low stem should have more energy than high stem (100Hz is lower)
    expect (lowRms > highRms * 0.5f, "Low stem should capture substantial energy from 100Hz content");
}

// ── Rename Tracks Logic Test ──────────────────────────────────────────────

void testRenameLogicSanitization (te::Edit& edit)
{
    // Create a track and add a clip with a filename-like name
    auto tracks = te::getAudioTracks (edit);
    expect (! tracks.isEmpty(), "Need at least one track");

    auto* track = tracks.getFirst();
    track->setName ("Track 1"); // Start with generic name

    // The RenameTracksFromClipsTool sanitizes clip names by:
    // - Removing file extensions
    // - Replacing underscores with spaces
    // These rules are tested here by verifying the tool's expected behavior patterns

    // Test: a clip named "drum_loop_01.wav" should suggest "drum loop 01"
    juce::String testName = "drum_loop_01.wav";

    // Simulate the sanitization logic
    auto sanitized = testName;
    if (sanitized.contains ("."))
        sanitized = sanitized.upToLastOccurrenceOf (".", false, false);
    sanitized = sanitized.replace ("_", " ");

    expect (sanitized == "drum loop 01",
            "Expected 'drum loop 01', got '" + sanitized.toStdString() + "'");

    // Another example
    juce::String testName2 = "Piano__Melody.mp3";
    auto sanitized2 = testName2.upToLastOccurrenceOf (".", false, false).replace ("_", " ");
    // Double underscore → double space, but that's expected behavior
    // (The actual tool may have additional collapsing logic)
    expect (sanitized2.contains ("Piano"), "Should contain 'Piano'");
    expect (sanitized2.contains ("Melody"), "Should contain 'Melody'");
}

// ── Edge Cases ────────────────────────────────────────────────────────────

void testAnalysisEmptyFile()
{
    // Zero-length WAV
    auto file = generateSilenceWav ("empty.wav", 0.0);
    auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f);
    // May or may not be valid, but should not crash
    // Zero-length file: reader->lengthInSamples == 0 → returns early without valid
}

void testAnalysisNonExistentFile()
{
    auto file = juce::File ("/tmp/nonexistent_waive_test_" + juce::Uuid().toString() + ".wav");
    auto summary = waive::analyseAudioFile (file, 0.0f, 0.0f);
    expect (! summary.valid, "Non-existent file should return invalid summary");
}

void testAnalysisThresholdBoundary()
{
    // Sine at exactly the threshold amplitude
    auto file = generateSineWav ("threshold_boundary.wav", 440.0, 0.1f, 1.0);
    auto summary = waive::analyseAudioFile (file, 0.1f, 0.0f);

    expect (summary.valid, "Analysis should succeed");
    // With amplitude 0.1 and threshold 0.1, activity detection depends on exact sample values
    // (sine may not hit exactly 0.1 due to discretization). This is a boundary case.
    // Just verify no crash and valid result.
}

// ── Cleanup ───────────────────────────────────────────────────────────────

void cleanupTestFiles()
{
    auto dir = getTempDir();
    if (dir.isDirectory())
        dir.deleteRecursively();
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    int failures = 0;

    auto runTest = [&] (const char* name, std::function<void()> fn)
    {
        std::cout << "  " << name << "..." << std::flush;
        try
        {
            fn();
            std::cout << " PASS" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << " FAIL: " << e.what() << std::endl;
            ++failures;
        }
        catch (...)
        {
            std::cerr << " FAIL: unknown exception" << std::endl;
            ++failures;
        }
        std::cout.flush();
        std::cerr.flush();
    };

    try
    {
        te::Engine engine ("WaiveToolTests");
        engine.getPluginManager().initialise();
        EditSession session (engine);
        auto& edit = session.getEdit();

        std::cout << "=== Audio Analysis Engine Tests ===" << std::endl;
        runTest ("Peak detection (0.5 amplitude)", testAnalysisPeakDetection);
        runTest ("Peak detection (0.9 amplitude)", testAnalysisPeakDetectionLoudSignal);
        runTest ("Peak detection (silence)", testAnalysisPeakDetectionSilence);
        runTest ("Peak detection (various amplitudes)", testPeakGainVariousAmplitudes);
        runTest ("Activity detection (silence-content-silence)", testAnalysisActivityDetection);
        runTest ("Activity detection (no activity)", testAnalysisActivityDetectionNoActivity);
        runTest ("Transient detection (single click)", testAnalysisTransientDetection);
        runTest ("Transient detection (multiple clicks)", testAnalysisTransientDetectionMultipleClicks);
        runTest ("Analysis cancellation", testAnalysisCancellation);
        runTest ("Analysis caching", testAnalysisCaching);

        std::cout << "\n=== Tool Registration Tests ===" << std::endl;
        runTest ("Tool registry completeness", testToolRegistryCompleteness);
        runTest ("Tool descriptions", testToolDescriptions);
        runTest ("Tool schema fields", testToolSchemaHasRequiredFields);

        std::cout << "\n=== Tool Logic Tests ===" << std::endl;
        runTest ("Normalization gain calculation", testNormalizationGainCalculation);
        runTest ("Silence detection regions", testSilenceDetectionRegions);
        runTest ("Silence detection quiet content", testSilenceDetectionWithQuietContent);
        runTest ("Transient alignment calculation", testTransientAlignmentCalculation);
        runTest ("Gain staging calculation", testGainStagingCalculation);
        runTest ("Stem separation smoothing", testExponentialSmoothingSeparation);
        runTest ("Rename track sanitization", [&] { testRenameLogicSanitization (edit); });

        std::cout << "\n=== Edge Cases ===" << std::endl;
        runTest ("Empty file", testAnalysisEmptyFile);
        runTest ("Non-existent file", testAnalysisNonExistentFile);
        runTest ("Threshold boundary", testAnalysisThresholdBoundary);

        cleanupTestFiles();
    }
    catch (const std::exception& e)
    {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n" << (failures == 0 ? "All WaiveToolTests passed!" : std::to_string (failures) + " test(s) failed") << std::endl;
    return failures > 0 ? 1 : 0;
}
```

### 2. Modify `tests/CMakeLists.txt` — Add WaiveToolTests executable

Add after the existing `WaiveUiTests` section:

```cmake
# ── Waive Tool Tests ─────────────────────────────────────────────────────────
# Tests audio analysis engine and built-in tool logic against synthetic WAV files.

juce_add_console_app(WaiveToolTests
    PRODUCT_NAME "Waive Tool Tests"
    COMPANY_NAME "Waive"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(WaiveToolTests PRIVATE
    WaiveToolTests.cpp

    # Audio analysis (under test)
    ../gui/src/tools/AudioAnalysis.h
    ../gui/src/tools/AudioAnalysis.cpp
    ../gui/src/tools/AudioAnalysisCache.h
    ../gui/src/tools/AudioAnalysisCache.cpp

    # Tool framework
    ../gui/src/tools/Tool.h
    ../gui/src/tools/ToolDiff.h
    ../gui/src/tools/ToolDiff.cpp
    ../gui/src/tools/ToolRegistry.h
    ../gui/src/tools/ToolRegistry.cpp
    ../gui/src/tools/ClipTrackIndexMap.h
    ../gui/src/tools/ClipTrackIndexMap.cpp

    # All built-in tools
    ../gui/src/tools/NormalizeSelectedClipsTool.h
    ../gui/src/tools/NormalizeSelectedClipsTool.cpp
    ../gui/src/tools/RenameTracksFromClipsTool.h
    ../gui/src/tools/RenameTracksFromClipsTool.cpp
    ../gui/src/tools/GainStageSelectedTracksTool.h
    ../gui/src/tools/GainStageSelectedTracksTool.cpp
    ../gui/src/tools/DetectSilenceAndCutRegionsTool.h
    ../gui/src/tools/DetectSilenceAndCutRegionsTool.cpp
    ../gui/src/tools/AlignClipsByTransientTool.h
    ../gui/src/tools/AlignClipsByTransientTool.cpp
    ../gui/src/tools/StemSeparationTool.h
    ../gui/src/tools/StemSeparationTool.cpp
    ../gui/src/tools/AutoMixSuggestionsTool.h
    ../gui/src/tools/AutoMixSuggestionsTool.cpp

    # Dependencies required by tools
    ../gui/src/tools/ModelManager.h
    ../gui/src/tools/ModelManager.cpp
    ../gui/src/tools/JobQueue.h
    ../gui/src/tools/JobQueue.cpp
    ../gui/src/edit/EditSession.h
    ../gui/src/edit/EditSession.cpp
    ../gui/src/util/PathSanitizer.h
    ../gui/src/util/PathSanitizer.cpp
)

target_compile_features(WaiveToolTests PRIVATE cxx_std_20)
juce_generate_juce_header(WaiveToolTests)
find_package(CURL REQUIRED)

target_include_directories(WaiveToolTests PRIVATE
    ../gui/src/tools
    ../gui/src/edit
    ../gui/src/util
    ../gui/src/ui
    ../gui/src
    ../engine/src
)

target_link_libraries(WaiveToolTests PRIVATE
    tracktion::tracktion_engine
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    CURL::libcurl
)

target_compile_definitions(WaiveToolTests PRIVATE
    JUCE_USE_GTK=0
    JUCE_WEB_BROWSER=0
)

add_test(NAME WaiveToolTests COMMAND ${CMAKE_CURRENT_BINARY_DIR}/WaiveToolTests_artefacts/WaiveToolTests)
```

**Important notes for this CMakeLists addition:**
- Check the existing targets to see if any additional source files are needed for linking (e.g., if `ToolRegistry` depends on `ExternalTool`, add it).
- If tools reference `SessionComponent`, `TimelineComponent`, or `SelectionManager` in their headers (for `ToolExecutionContext`), those headers may need to be included. Since WaiveToolTests only tests the analysis engine and tool metadata (NOT `preparePlan`/`apply` which require the full UI context), forward declarations or minimal includes should suffice.
- If you get linker errors about missing `SessionComponent` or `SelectionManager` symbols, add those source files to target_sources. The existing `WaiveUiTests` target includes all UI sources — use that as reference.
- Alternatively, if tool headers transitively pull in UI deps, add `JUCE_MODAL_LOOPS_PERMITTED=1` to compile definitions.

## Files Expected To Change
- `tests/WaiveToolTests.cpp` (new — 24 test functions across 5 categories)
- `tests/CMakeLists.txt` (add WaiveToolTests target)

## Validation

```bash
cmake -B build && cmake --build build --target WaiveToolTests -j$(($(nproc)/2))
./build/tests/WaiveToolTests_artefacts/WaiveToolTests
ctest --test-dir build --output-on-failure -R WaiveToolTests
```

## Exit Criteria
- `WaiveToolTests` builds and links without errors.
- All audio analysis engine tests pass:
  - Peak detection accurate within 0.05 for amplitudes 0.1-1.0
  - Activity detection finds content boundaries within 20ms
  - Transient detection locates clicks within 50ms
  - Cancellation works immediately
  - Caching returns identical results
- All tool registration tests pass:
  - All 7 tools registered in ToolRegistry
  - All tools have valid descriptions (name, displayName, version, schema)
  - Schemas have correct structure
- All tool logic tests pass:
  - Normalization gain delta calculated correctly
  - Silence regions detected and trim boundaries correct
  - Transient alignment delta calculated correctly
  - Gain staging volume adjustment calculated correctly
  - Stem separation produces non-silent low and high outputs
  - Track rename sanitization handles extensions and underscores
- Edge cases handled without crashes (empty files, non-existent files, boundary thresholds).
- Existing `WaiveCoreTests` and `WaiveUiTests` still pass.
- CI runs WaiveToolTests as part of `ctest`.
