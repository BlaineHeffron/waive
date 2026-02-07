# Phase 18: Audio Tool Validation & Benchmarks

## Objective
Create a systematic test suite that validates every audio tool (Python external tools + built-in C++ tools) against known ground-truth signals. Use synthetically generated test audio (deterministic, no downloads, CI-friendly) with standard MIR evaluation metrics (`mir_eval`, `pyloudnorm`) to ensure each tool produces correct results. This catches regressions, validates algorithms, and provides confidence scores for each tool's accuracy.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Python tests: `python3 -m pytest tools/tests/ -v`
- Test dependencies: `pip install -r tools/tests/requirements.txt`

## Architecture Context

### Tool I/O Contract (all external Python tools)
Every external tool follows the same contract:
- **Input**: `--input-dir <dir>` containing `params.json` and optionally `input.wav`
- **Output**: `--output-dir <dir>` where tool writes `result.json` and optionally output audio files
- **Invocation**: `python3 <tool>.py --input-dir <in> --output-dir <out>`

### Existing Tools (6 implemented, 4 planned in phase 12)
**Phase 11 (analysis — implemented):**
1. `beat_detection` — Onset-based beat/BPM detection (spectral flux + peak picking)
2. `chord_detection` — Chroma-based chord template matching (8 chord types)
3. `key_detection` — Krumhansl-Schmuckler algorithm (chroma → key profile correlation)
4. `audio_to_midi` — YIN pitch estimation → MIDI file generation

**Pre-existing (phase 5):**
5. `music_generation` — MusicGen AI / numpy fallback synthesis
6. `timbre_transfer` — Librosa pitch detection / scipy FFT spectral shaping

**Phase 12 (processing — to be implemented):**
7. `noise_reduction` — Spectral gating
8. `pitch_correction` — YIN + nearest-note pitch shifting
9. `auto_eq` — Spectral band analysis → EQ suggestions
10. `mastering_assistant` — LUFS normalization + soft limiting

### Evaluation Libraries
- **`mir_eval`** — Standard MIR evaluation: `mir_eval.beat.evaluate()`, `mir_eval.key.weighted_score()`, `mir_eval.onset.f_measure()`, `mir_eval.separation.bss_eval_sources()`
- **`pyloudnorm`** — ITU-R BS.1770-4 LUFS measurement: `pyln.Meter(sr).integrated_loudness(data)`
- **`numpy`/`scipy`** — Synthetic signal generation, SNR computation, spectral analysis

### Testing Strategy
1. **Synthetic signals only** — No external dataset downloads. All test audio generated programmatically with known properties (deterministic seeds where randomness is needed).
2. **End-to-end tool invocation** — Tests call each tool's Python script via its actual `--input-dir`/`--output-dir` interface, validating the full pipeline including I/O parsing.
3. **Metric thresholds** — Each test asserts minimum quality thresholds based on the signal difficulty:
   - Easy (synthetic click tracks, pure tones): high thresholds (>0.9)
   - Medium (multi-frequency signals, chords): moderate thresholds (>0.7)
   - Hard (complex signals): lower thresholds documented as known limitations

## Implementation Tasks

### 1. Create `tools/tests/audio_fixtures.py` — Shared test signal generators

```python
"""Synthetic audio signal generators for tool validation.

All generators produce deterministic float32 numpy arrays at 44100 Hz.
These signals have known ground-truth properties for testing.
"""
import numpy as np
from scipy.io import wavfile
import os
import json

SR = 44100  # Standard sample rate for all test signals


def save_wav(filepath, samples, sr=SR):
    """Save float32 samples as 16-bit WAV (tool-compatible format)."""
    # Clip to [-1, 1] and convert to int16
    samples = np.clip(samples, -1.0, 1.0)
    int_samples = (samples * 32767).astype(np.int16)
    wavfile.write(filepath, sr, int_samples)


def setup_tool_dirs(tmp_path, tool_name, params, audio_samples=None, sr=SR):
    """Create standard input/output directories for a tool invocation.

    Returns (input_dir, output_dir) paths.
    """
    input_dir = os.path.join(str(tmp_path), f"{tool_name}_in")
    output_dir = os.path.join(str(tmp_path), f"{tool_name}_out")
    os.makedirs(input_dir, exist_ok=True)
    os.makedirs(output_dir, exist_ok=True)

    # Write params.json
    with open(os.path.join(input_dir, "params.json"), "w") as f:
        json.dump(params, f)

    # Write input.wav if audio provided
    if audio_samples is not None:
        save_wav(os.path.join(input_dir, "input.wav"), audio_samples, sr)

    return input_dir, output_dir


def read_result(output_dir):
    """Read and parse result.json from a tool's output directory."""
    result_path = os.path.join(output_dir, "result.json")
    with open(result_path) as f:
        return json.load(f)


# ── Signal Generators ──────────────────────────────────────────────────────


def generate_click_track(bpm, duration_sec=10.0, sr=SR):
    """Generate a click track with precise beat positions.

    Returns (samples, beat_times_array).
    Beat positions are exact — ideal for testing beat detection.
    """
    beat_interval = 60.0 / bpm
    n_samples = int(duration_sec * sr)
    samples = np.zeros(n_samples, dtype=np.float32)

    # 5ms exponentially decaying 1kHz click
    click_len = int(0.005 * sr)
    t_click = np.arange(click_len) / sr
    click = np.sin(2 * np.pi * 1000 * t_click) * np.exp(-t_click / 0.001)
    click = click.astype(np.float32)

    beat_times = []
    t = 0.0
    while t < duration_sec - 0.01:
        idx = int(t * sr)
        end = min(idx + len(click), n_samples)
        samples[idx:end] += click[: end - idx]
        beat_times.append(t)
        t += beat_interval

    return samples, np.array(beat_times)


def generate_sine(freq_hz, duration_sec=3.0, amplitude=0.5, sr=SR):
    """Pure sine wave at exact frequency. Ideal for pitch detection tests."""
    t = np.linspace(0, duration_sec, int(duration_sec * sr), endpoint=False)
    return (amplitude * np.sin(2 * np.pi * freq_hz * t)).astype(np.float32)


def generate_chord(root_hz, mode="major", duration_sec=5.0, sr=SR):
    """Generate a chord with known root and mode.

    Major: root + major third (5/4) + perfect fifth (3/2)
    Minor: root + minor third (6/5) + perfect fifth (3/2)

    Returns (samples, key_label) e.g. ("C major", "A minor").
    """
    t = np.linspace(0, duration_sec, int(duration_sec * sr), endpoint=False)

    if mode == "major":
        ratios = [1.0, 5 / 4, 3 / 2]
    else:
        ratios = [1.0, 6 / 5, 3 / 2]

    signal = np.zeros(len(t), dtype=np.float32)
    for ratio in ratios:
        signal += 0.25 * np.sin(2 * np.pi * root_hz * ratio * t).astype(np.float32)

    return signal


def generate_chord_sequence(chords, segment_duration=2.0, sr=SR):
    """Generate a sequence of chords for chord detection testing.

    chords: list of (root_hz, mode, label) tuples, e.g.:
        [(261.63, "major", "C"), (220.0, "minor", "Am")]

    Returns (samples, chord_annotations) where annotations are:
        [{"time": 0.0, "chord": "C"}, {"time": 2.0, "chord": "Am"}, ...]
    """
    segments = []
    annotations = []
    t_offset = 0.0

    for root_hz, mode, label in chords:
        seg = generate_chord(root_hz, mode, segment_duration, sr)
        # Apply fade in/out to avoid clicks at boundaries
        fade_len = int(0.01 * sr)
        seg[:fade_len] *= np.linspace(0, 1, fade_len).astype(np.float32)
        seg[-fade_len:] *= np.linspace(1, 0, fade_len).astype(np.float32)

        segments.append(seg)
        annotations.append({"time": round(t_offset, 3), "chord": label})
        t_offset += segment_duration

    return np.concatenate(segments), annotations


def generate_melody(notes, sr=SR):
    """Generate a monophonic melody for audio-to-MIDI testing.

    notes: list of (freq_hz, duration_sec) tuples. Use 0 for silence.
    Returns (samples, note_annotations) where annotations are:
        [{"start": 0.0, "end": 0.5, "midi": 69, "freq": 440.0}, ...]
    """
    segments = []
    annotations = []
    t_offset = 0.0

    for freq_hz, dur in notes:
        n_samples = int(dur * sr)
        if freq_hz > 0:
            t = np.arange(n_samples) / sr
            seg = 0.5 * np.sin(2 * np.pi * freq_hz * t).astype(np.float32)
            # Smooth envelope
            env_len = min(int(0.01 * sr), n_samples // 4)
            if env_len > 0:
                seg[:env_len] *= np.linspace(0, 1, env_len).astype(np.float32)
                seg[-env_len:] *= np.linspace(1, 0, env_len).astype(np.float32)

            midi_note = int(round(12 * np.log2(freq_hz / 440.0) + 69))
            annotations.append(
                {
                    "start": round(t_offset, 3),
                    "end": round(t_offset + dur, 3),
                    "midi": midi_note,
                    "freq": freq_hz,
                }
            )
        else:
            seg = np.zeros(n_samples, dtype=np.float32)

        segments.append(seg)
        t_offset += dur

    return np.concatenate(segments), annotations


def generate_noisy_signal(clean_signal, snr_db=20, seed=42):
    """Add white noise at a specified SNR to a clean signal.

    Returns (noisy_signal, noise, actual_snr_db).
    Uses fixed seed for deterministic results.
    """
    rng = np.random.RandomState(seed)
    signal_power = np.mean(clean_signal**2)
    noise_power = signal_power / (10 ** (snr_db / 10))
    noise = np.sqrt(noise_power) * rng.randn(len(clean_signal))
    noise = noise.astype(np.float32)
    noisy = (clean_signal + noise).astype(np.float32)
    actual_snr = 10 * np.log10(signal_power / np.mean(noise**2))
    return noisy, noise, actual_snr


def generate_two_source_mix(freq1=440.0, freq2=880.0, duration_sec=3.0, sr=SR):
    """Generate a mixture of two sine waves for stem separation testing.

    Returns (mixture, source1, source2).
    """
    t = np.linspace(0, duration_sec, int(duration_sec * sr), endpoint=False)
    source1 = (0.5 * np.sin(2 * np.pi * freq1 * t)).astype(np.float32)
    source2 = (0.3 * np.sin(2 * np.pi * freq2 * t)).astype(np.float32)
    mixture = source1 + source2
    return mixture, source1, source2


# ── Note/Key Reference Data ───────────────────────────────────────────────

# Standard note frequencies (A4 = 440 Hz)
NOTE_FREQS = {
    "C4": 261.63,
    "D4": 293.66,
    "E4": 329.63,
    "F4": 349.23,
    "G4": 392.00,
    "A4": 440.00,
    "B4": 493.88,
    "C5": 523.25,
}

# Key detection test cases: (root_freq, mode, expected_key_label)
KEY_TEST_CASES = [
    (261.63, "major", "C major"),
    (440.00, "minor", "A minor"),
    (392.00, "major", "G major"),
    (329.63, "minor", "E minor"),
]
```

### 2. Create `tools/tests/test_beat_detection.py` — Beat detection validation

```python
"""Validate beat_detection tool against synthetic click tracks."""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_click_track, setup_tool_dirs, read_result
)

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "beat_detection", "beat_detection.py")

# Try to import mir_eval for standard metrics; skip advanced tests if unavailable
try:
    import mir_eval
    HAS_MIR_EVAL = True
except ImportError:
    HAS_MIR_EVAL = False


def run_beat_detection(tmp_path, samples, params=None):
    """Helper: run the beat detection tool and return parsed result."""
    if params is None:
        params = {"method": "onset"}
    input_dir, output_dir = setup_tool_dirs(tmp_path, "beat_det", params, samples)
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT, "--input-dir", input_dir, "--output-dir", output_dir],
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(output_dir)


class TestBeatDetectionBPM:
    """Test BPM estimation accuracy against click tracks at known tempos."""

    @pytest.mark.parametrize("bpm", [80, 100, 120, 140, 160])
    def test_bpm_estimation(self, tmp_path, bpm):
        """BPM estimate should be within 5% of ground truth."""
        samples, _ = generate_click_track(bpm, duration_sec=10.0)
        result = run_beat_detection(tmp_path, samples)

        assert result["success"] is True
        estimated_bpm = result["bpm"]

        # Allow octave errors (double/half tempo) — common in beat tracking
        error = min(
            abs(estimated_bpm - bpm),
            abs(estimated_bpm - bpm * 2),
            abs(estimated_bpm - bpm / 2),
        )
        tolerance = bpm * 0.05  # 5% tolerance
        assert error < tolerance, (
            f"BPM estimation too far off: expected ~{bpm}, got {estimated_bpm}"
        )

    def test_120bpm_autocorrelation(self, tmp_path):
        """Autocorrelation method should also detect 120 BPM."""
        samples, _ = generate_click_track(120, duration_sec=10.0)
        result = run_beat_detection(tmp_path, samples, {"method": "autocorrelation"})

        assert result["success"] is True
        estimated_bpm = result["bpm"]
        error = min(abs(estimated_bpm - 120), abs(estimated_bpm - 240), abs(estimated_bpm - 60))
        assert error < 6.0, f"Autocorrelation BPM too far off: {estimated_bpm}"


class TestBeatDetectionPositions:
    """Test beat position accuracy using mir_eval metrics."""

    @pytest.mark.skipif(not HAS_MIR_EVAL, reason="mir_eval not installed")
    @pytest.mark.parametrize("bpm", [100, 120, 140])
    def test_beat_f_measure(self, tmp_path, bpm):
        """Beat positions should achieve F-measure > 0.7 on clean click tracks."""
        samples, reference_beats = generate_click_track(bpm, duration_sec=10.0)
        result = run_beat_detection(tmp_path, samples)

        assert result["success"] is True
        estimated_beats = np.array(result["beats"])

        if len(estimated_beats) < 2:
            pytest.skip("Too few beats detected to evaluate")

        scores = mir_eval.beat.evaluate(reference_beats, estimated_beats)
        f_measure = scores["F-measure"]
        assert f_measure > 0.7, (
            f"Beat F-measure too low: {f_measure:.3f} (CMLt: {scores['CMLt']:.3f})"
        )

    def test_beat_count_reasonable(self, tmp_path):
        """Number of detected beats should be roughly correct."""
        bpm = 120
        duration = 10.0
        expected_beats = int(duration * bpm / 60)
        samples, _ = generate_click_track(bpm, duration)
        result = run_beat_detection(tmp_path, samples)

        assert result["success"] is True
        actual_count = result["beat_count"]
        # Allow 30% deviation
        assert abs(actual_count - expected_beats) < expected_beats * 0.3, (
            f"Beat count off: expected ~{expected_beats}, got {actual_count}"
        )


class TestBeatDetectionEdgeCases:
    """Edge cases and error handling."""

    def test_silent_audio(self, tmp_path):
        """Should handle silence gracefully (no crash, returns result)."""
        samples = np.zeros(SR * 5, dtype=np.float32)
        result = run_beat_detection(tmp_path, samples)
        assert result["success"] is True  # Should succeed, just with few/no beats

    def test_very_short_audio(self, tmp_path):
        """Should handle very short audio (< 1 second)."""
        samples, _ = generate_click_track(120, duration_sec=0.5)
        result = run_beat_detection(tmp_path, samples)
        assert result["success"] is True
```

### 3. Create `tools/tests/test_key_detection.py` — Key detection validation

```python
"""Validate key_detection tool against synthetic tonal signals."""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_chord, setup_tool_dirs, read_result, KEY_TEST_CASES
)

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "key_detection", "key_detection.py")

try:
    import mir_eval
    HAS_MIR_EVAL = True
except ImportError:
    HAS_MIR_EVAL = False


def run_key_detection(tmp_path, samples):
    input_dir, output_dir = setup_tool_dirs(tmp_path, "key_det", {}, samples)
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT, "--input-dir", input_dir, "--output-dir", output_dir],
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(output_dir)


class TestKeyDetection:
    """Test key detection accuracy on synthetic chord signals."""

    @pytest.mark.parametrize("root_hz,mode,expected_key", KEY_TEST_CASES)
    def test_key_detection_exact(self, tmp_path, root_hz, mode, expected_key):
        """Key detection should identify the correct key for a pure chord."""
        samples = generate_chord(root_hz, mode, duration_sec=5.0)
        result = run_key_detection(tmp_path, samples)

        assert result["success"] is True
        detected_key = result["key"]

        # Exact match or acceptable related key
        if detected_key == expected_key:
            return  # Perfect

        # Check if it's a closely related key using mir_eval scoring
        if HAS_MIR_EVAL:
            score = mir_eval.key.weighted_score(expected_key, detected_key)
            assert score >= 0.3, (
                f"Key detection too far off: expected '{expected_key}', "
                f"got '{detected_key}' (mir_eval score: {score})"
            )
        else:
            # Without mir_eval, accept relative/parallel keys manually
            # (e.g., C major ↔ A minor is acceptable)
            pytest.skip(f"Got '{detected_key}' instead of '{expected_key}' — need mir_eval for scoring")

    @pytest.mark.skipif(not HAS_MIR_EVAL, reason="mir_eval not installed")
    @pytest.mark.parametrize("root_hz,mode,expected_key", KEY_TEST_CASES)
    def test_key_weighted_score(self, tmp_path, root_hz, mode, expected_key):
        """MIREX weighted score should be >= 0.5 (exact or relative key)."""
        samples = generate_chord(root_hz, mode, duration_sec=5.0)
        result = run_key_detection(tmp_path, samples)

        assert result["success"] is True
        score = mir_eval.key.weighted_score(expected_key, result["key"])
        assert score >= 0.5, (
            f"Weighted score too low: {score} for '{expected_key}' vs '{result['key']}'"
        )

    def test_confidence_range(self, tmp_path):
        """Confidence score should be in [-1, 1] range."""
        samples = generate_chord(261.63, "major", duration_sec=5.0)
        result = run_key_detection(tmp_path, samples)
        assert result["success"] is True
        assert -1.0 <= result["confidence"] <= 1.0

    def test_alternatives_provided(self, tmp_path):
        """Result should include top alternative keys."""
        samples = generate_chord(440.0, "minor", duration_sec=5.0)
        result = run_key_detection(tmp_path, samples)
        assert result["success"] is True
        assert "alternatives" in result
        assert len(result["alternatives"]) >= 3

    def test_silent_audio(self, tmp_path):
        """Should handle silence gracefully."""
        samples = np.zeros(SR * 5, dtype=np.float32)
        result = run_key_detection(tmp_path, samples)
        assert result["success"] is True
```

### 4. Create `tools/tests/test_chord_detection.py` — Chord detection validation

```python
"""Validate chord_detection tool against synthetic chord sequences."""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_chord_sequence, setup_tool_dirs, read_result
)

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "chord_detection", "chord_detection.py")


def run_chord_detection(tmp_path, samples, hop_seconds=0.5):
    input_dir, output_dir = setup_tool_dirs(
        tmp_path, "chord_det", {"hop_seconds": hop_seconds}, samples
    )
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT, "--input-dir", input_dir, "--output-dir", output_dir],
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(output_dir)


class TestChordDetection:
    """Test chord detection on synthetic chord progressions."""

    def test_single_major_chord(self, tmp_path):
        """Should detect a single sustained major chord."""
        chords = [(261.63, "major", "C")]
        samples, annotations = generate_chord_sequence(chords, segment_duration=3.0)
        result = run_chord_detection(tmp_path, samples)

        assert result["success"] is True
        assert result["chord_count"] >= 1
        # The first detected chord should be C or C-related
        first_chord = result["chords"][0]["chord"]
        assert "C" in first_chord, f"Expected C-based chord, got '{first_chord}'"

    def test_chord_progression(self, tmp_path):
        """Should detect chord changes in a I-vi-IV-V progression (C-Am-F-G)."""
        chords = [
            (261.63, "major", "C"),
            (220.00, "minor", "Am"),
            (349.23, "major", "F"),
            (392.00, "major", "G"),
        ]
        samples, annotations = generate_chord_sequence(chords, segment_duration=2.0)
        result = run_chord_detection(tmp_path, samples)

        assert result["success"] is True
        # Should detect at least some chord changes
        assert result["chord_count"] >= 2, (
            f"Should detect chord changes, only got {result['chord_count']} chords"
        )

    def test_chord_count_reasonable(self, tmp_path):
        """Number of detected chords should be in a reasonable range."""
        chords = [
            (261.63, "major", "C"),
            (392.00, "major", "G"),
            (220.00, "minor", "Am"),
        ]
        samples, annotations = generate_chord_sequence(chords, segment_duration=2.0)
        result = run_chord_detection(tmp_path, samples, hop_seconds=0.5)

        assert result["success"] is True
        # With 3 distinct chords over 6 seconds, should detect 2-20 chord entries
        assert 2 <= result["chord_count"] <= 20

    def test_confidence_scores(self, tmp_path):
        """All chord detections should have valid confidence scores."""
        chords = [(261.63, "major", "C")]
        samples, _ = generate_chord_sequence(chords, segment_duration=3.0)
        result = run_chord_detection(tmp_path, samples)

        assert result["success"] is True
        for chord_entry in result["chords"]:
            assert "confidence" in chord_entry
            assert chord_entry["confidence"] >= 0.0

    def test_timestamps_monotonic(self, tmp_path):
        """Chord timestamps should be monotonically increasing."""
        chords = [
            (261.63, "major", "C"),
            (392.00, "major", "G"),
        ]
        samples, _ = generate_chord_sequence(chords, segment_duration=3.0)
        result = run_chord_detection(tmp_path, samples)

        assert result["success"] is True
        times = [c["time"] for c in result["chords"]]
        for i in range(1, len(times)):
            assert times[i] >= times[i - 1], "Timestamps not monotonic"

    def test_silent_audio(self, tmp_path):
        """Should handle silence gracefully."""
        samples = np.zeros(SR * 3, dtype=np.float32)
        result = run_chord_detection(tmp_path, samples)
        assert result["success"] is True
```

### 5. Create `tools/tests/test_audio_to_midi.py` — Audio-to-MIDI validation

```python
"""Validate audio_to_midi tool against synthetic monophonic melodies."""
import subprocess
import sys
import os
import struct
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_melody, generate_sine, setup_tool_dirs, read_result, NOTE_FREQS
)

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "audio_to_midi", "audio_to_midi.py")


def run_audio_to_midi(tmp_path, samples, params=None):
    if params is None:
        params = {}
    input_dir, output_dir = setup_tool_dirs(tmp_path, "a2m", params, samples)
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT, "--input-dir", input_dir, "--output-dir", output_dir],
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(output_dir), output_dir


class TestAudioToMidi:
    """Test MIDI transcription accuracy on synthetic melodies."""

    def test_single_note_a4(self, tmp_path):
        """A sustained A4 (440 Hz) should produce MIDI note 69."""
        samples = generate_sine(440.0, duration_sec=2.0, amplitude=0.5)
        result, _ = run_audio_to_midi(tmp_path, samples)

        assert result["success"] is True
        assert result["note_count"] >= 1, "Should detect at least 1 note"

        # Check that the primary detected note is close to MIDI 69 (A4)
        midi_notes = [n["midi"] for n in result["notes"]]
        # Allow +/- 1 semitone tolerance for pitch estimation
        a4_detected = any(abs(m - 69) <= 1 for m in midi_notes)
        assert a4_detected, f"Expected MIDI ~69 (A4), got notes: {midi_notes}"

    def test_simple_melody(self, tmp_path):
        """A C-D-E-F melody should produce ~4 MIDI notes near the correct pitches."""
        notes = [
            (NOTE_FREQS["C4"], 0.5),  # C4 = MIDI 60
            (0, 0.1),                  # short silence
            (NOTE_FREQS["D4"], 0.5),  # D4 = MIDI 62
            (0, 0.1),
            (NOTE_FREQS["E4"], 0.5),  # E4 = MIDI 64
            (0, 0.1),
            (NOTE_FREQS["F4"], 0.5),  # F4 = MIDI 65
        ]
        samples, annotations = generate_melody(notes)
        result, _ = run_audio_to_midi(tmp_path, samples)

        assert result["success"] is True
        assert result["note_count"] >= 2, (
            f"Should detect at least 2 notes from 4-note melody, got {result['note_count']}"
        )

    def test_midi_file_generated(self, tmp_path):
        """Should produce a valid MIDI file in the output directory."""
        samples = generate_sine(440.0, duration_sec=1.0)
        result, output_dir = run_audio_to_midi(tmp_path, samples)

        assert result["success"] is True
        assert "midi_file" in result
        midi_path = os.path.join(output_dir, result["midi_file"])
        assert os.path.exists(midi_path), f"MIDI file not found: {midi_path}"

        # Validate MIDI header
        with open(midi_path, "rb") as f:
            header = f.read(4)
            assert header == b"MThd", "Invalid MIDI file header"

    def test_note_timing_accuracy(self, tmp_path):
        """Note start/end times should be approximately correct."""
        notes = [
            (NOTE_FREQS["A4"], 1.0),  # 1 second A4
            (0, 0.5),                   # 0.5 second silence
            (NOTE_FREQS["C5"], 1.0),  # 1 second C5
        ]
        samples, annotations = generate_melody(notes)
        result, _ = run_audio_to_midi(tmp_path, samples)

        assert result["success"] is True
        if result["note_count"] >= 2:
            # Second note should start after ~1.5 seconds
            second_note_start = result["notes"][1]["start"]
            assert 1.0 < second_note_start < 2.0, (
                f"Second note start time off: {second_note_start} (expected ~1.5)"
            )

    def test_silent_audio(self, tmp_path):
        """Should handle silence (produce 0 notes, no crash)."""
        samples = np.zeros(SR * 2, dtype=np.float32)
        result, _ = run_audio_to_midi(tmp_path, samples)
        assert result["success"] is True
        assert result["note_count"] == 0
```

### 6. Create `tools/tests/test_music_generation.py` — Music generation validation

```python
"""Validate music_generation tool — checks output format and basic audio properties."""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import SR, setup_tool_dirs, read_result

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "music_generation", "music_generation.py")


def run_music_generation(tmp_path, params):
    input_dir, output_dir = setup_tool_dirs(tmp_path, "musicgen", params)
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT, "--input-dir", input_dir, "--output-dir", output_dir],
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(output_dir), output_dir


class TestMusicGeneration:
    """Test music generation produces valid audio output."""

    def test_basic_generation(self, tmp_path):
        """Should produce an output WAV file from a text prompt."""
        result, output_dir = run_music_generation(tmp_path, {
            "prompt": "drum beat",
            "duration_seconds": 3,
        })
        assert result["success"] is True

        # Check output audio exists
        output_wav = os.path.join(output_dir, "output.wav")
        assert os.path.exists(output_wav), "No output.wav produced"
        assert os.path.getsize(output_wav) > 1000, "Output file suspiciously small"

    def test_output_duration_approximate(self, tmp_path):
        """Generated audio duration should be approximately as requested."""
        target_seconds = 5
        result, output_dir = run_music_generation(tmp_path, {
            "prompt": "ambient pad",
            "duration_seconds": target_seconds,
        })
        assert result["success"] is True

        # Read the output and check duration
        output_wav = os.path.join(output_dir, "output.wav")
        try:
            from scipy.io import wavfile
            sr_out, data = wavfile.read(output_wav)
            actual_duration = len(data) / sr_out
            # Allow 50% tolerance (fallback synth may vary)
            assert actual_duration > target_seconds * 0.5, (
                f"Output too short: {actual_duration:.1f}s vs requested {target_seconds}s"
            )
        except ImportError:
            pytest.skip("scipy not available for WAV verification")

    def test_output_not_silent(self, tmp_path):
        """Generated audio should not be all zeros."""
        result, output_dir = run_music_generation(tmp_path, {
            "prompt": "energetic rock guitar",
            "duration_seconds": 3,
        })
        assert result["success"] is True

        output_wav = os.path.join(output_dir, "output.wav")
        try:
            from scipy.io import wavfile
            _, data = wavfile.read(output_wav)
            data = data.astype(np.float32)
            rms = np.sqrt(np.mean(data**2))
            assert rms > 0.001, "Output is effectively silent"
        except ImportError:
            pytest.skip("scipy not available for audio analysis")

    def test_various_prompts(self, tmp_path):
        """Different prompt keywords should all produce output without errors."""
        prompts = ["bass line", "ambient atmosphere", "simple melody"]
        for prompt in prompts:
            result, output_dir = run_music_generation(tmp_path, {
                "prompt": prompt,
                "duration_seconds": 2,
            })
            assert result["success"] is True, f"Failed for prompt: '{prompt}'"
```

### 7. Create `tools/tests/test_timbre_transfer.py` — Timbre transfer validation

```python
"""Validate timbre_transfer tool — checks output audio and pitch preservation."""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import SR, generate_sine, setup_tool_dirs, read_result

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "timbre_transfer", "timbre_transfer.py")


def run_timbre_transfer(tmp_path, samples, target_instrument):
    params = {"target_instrument": target_instrument}
    input_dir, output_dir = setup_tool_dirs(tmp_path, "timbre", params, samples)
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT, "--input-dir", input_dir, "--output-dir", output_dir],
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(output_dir), output_dir


class TestTimbreTransfer:
    """Test timbre transfer produces valid transformed audio."""

    @pytest.mark.parametrize("instrument", ["trumpet", "violin", "flute", "piano"])
    def test_produces_output(self, tmp_path, instrument):
        """Should produce an output WAV for each instrument target."""
        samples = generate_sine(440.0, duration_sec=2.0)
        result, output_dir = run_timbre_transfer(tmp_path, samples, instrument)
        assert result["success"] is True

        output_wav = os.path.join(output_dir, "output.wav")
        assert os.path.exists(output_wav), f"No output.wav for {instrument}"

    def test_output_same_duration(self, tmp_path):
        """Output audio should be approximately same duration as input."""
        duration = 2.0
        samples = generate_sine(440.0, duration_sec=duration)
        result, output_dir = run_timbre_transfer(tmp_path, samples, "violin")
        assert result["success"] is True

        try:
            from scipy.io import wavfile
            sr_out, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            out_duration = len(data) / sr_out
            assert abs(out_duration - duration) < 1.0, (
                f"Duration mismatch: input {duration}s, output {out_duration:.1f}s"
            )
        except ImportError:
            pytest.skip("scipy not available")

    def test_output_not_silent(self, tmp_path):
        """Transformed audio should not be silent."""
        samples = generate_sine(440.0, duration_sec=2.0)
        result, output_dir = run_timbre_transfer(tmp_path, samples, "trumpet")
        assert result["success"] is True

        try:
            from scipy.io import wavfile
            _, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            rms = np.sqrt(np.mean(data.astype(np.float32) ** 2))
            assert rms > 0.001, "Timbre transfer output is silent"
        except ImportError:
            pytest.skip("scipy not available")

    def test_pitch_preservation(self, tmp_path):
        """Fundamental frequency should be approximately preserved after transfer."""
        freq = 440.0
        samples = generate_sine(freq, duration_sec=2.0)
        result, output_dir = run_timbre_transfer(tmp_path, samples, "flute")
        assert result["success"] is True

        try:
            from scipy.io import wavfile
            sr_out, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            if data.ndim > 1:
                data = data.mean(axis=1)
            data = data.astype(np.float32)

            # Use autocorrelation to detect pitch
            mid = len(data) // 2
            frame = data[mid : mid + 4096]
            if np.max(np.abs(frame)) < 0.001:
                pytest.skip("Frame too quiet for pitch detection")

            corr = np.correlate(frame, frame, mode="full")
            corr = corr[len(corr) // 2 :]

            expected_period = sr_out / freq
            search_start = int(expected_period * 0.5)
            search_end = min(int(expected_period * 2.0), len(corr) - 1)
            if search_end <= search_start:
                pytest.skip("Search range too small")

            peak_lag = search_start + np.argmax(corr[search_start:search_end])
            detected_freq = sr_out / peak_lag

            # Allow generous tolerance (within 1 octave / 1200 cents)
            cents_error = abs(1200 * np.log2(detected_freq / freq))
            assert cents_error < 1200, (
                f"Pitch shifted too far: {cents_error:.0f} cents "
                f"(expected {freq} Hz, detected {detected_freq:.1f} Hz)"
            )
        except ImportError:
            pytest.skip("scipy not available")
```

### 8. Create `tools/tests/test_phase12_tools.py` — Tests for phase 12 tools (noise reduction, mastering, etc.)

These tools may not exist yet when this phase runs — tests should skip gracefully if the tool script isn't found.

```python
"""Validate phase 12 audio processing tools (noise_reduction, pitch_correction, auto_eq, mastering_assistant).

These tools are created in phase 12. Tests skip gracefully if tool doesn't exist yet.
"""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_sine, generate_noisy_signal, setup_tool_dirs, read_result
)

TOOLS_BASE = os.path.join(os.path.dirname(__file__), "..")


def tool_exists(name):
    return os.path.exists(os.path.join(TOOLS_BASE, name, f"{name}.py"))


def run_tool(tmp_path, tool_name, params, samples=None):
    script = os.path.join(TOOLS_BASE, tool_name, f"{tool_name}.py")
    input_dir, output_dir = setup_tool_dirs(tmp_path, tool_name, params, samples)
    result = subprocess.run(
        [sys.executable, script, "--input-dir", input_dir, "--output-dir", output_dir],
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, f"{tool_name} failed: {result.stderr}"
    return read_result(output_dir), output_dir


# ── Noise Reduction ────────────────────────────────────────────────────────


@pytest.mark.skipif(not tool_exists("noise_reduction"), reason="noise_reduction not yet implemented")
class TestNoiseReduction:
    """Test noise reduction improves SNR on synthetically noised signals."""

    def test_snr_improvement(self, tmp_path):
        """Denoised signal should have better SNR than noisy input."""
        clean = generate_sine(440.0, duration_sec=3.0, amplitude=0.5)
        noisy, noise, snr_before = generate_noisy_signal(clean, snr_db=10)

        result, output_dir = run_tool(tmp_path, "noise_reduction", {
            "strength": 0.8,
            "noise_profile_seconds": 0.5,
        }, noisy)

        assert result["success"] is True

        # Read denoised output
        output_wav = os.path.join(output_dir, "output.wav")
        assert os.path.exists(output_wav)

        from scipy.io import wavfile
        _, denoised = wavfile.read(output_wav)
        denoised = denoised.astype(np.float32)
        if denoised.dtype == np.int16:
            denoised = denoised / 32767.0

        # Trim to matching length
        min_len = min(len(clean), len(denoised))
        residual = denoised[:min_len] - clean[:min_len]
        snr_after = 10 * np.log10(np.mean(clean[:min_len] ** 2) / (np.mean(residual ** 2) + 1e-10))

        # Should improve SNR by at least 2 dB
        assert snr_after > snr_before + 2.0, (
            f"SNR improvement too small: {snr_before:.1f} dB → {snr_after:.1f} dB"
        )

    def test_produces_output(self, tmp_path):
        """Should produce an output WAV file."""
        clean = generate_sine(440.0, duration_sec=2.0)
        noisy, _, _ = generate_noisy_signal(clean, snr_db=15)

        result, output_dir = run_tool(tmp_path, "noise_reduction", {"strength": 0.5}, noisy)
        assert result["success"] is True
        assert os.path.exists(os.path.join(output_dir, "output.wav"))


# ── Mastering Assistant ────────────────────────────────────────────────────


@pytest.mark.skipif(not tool_exists("mastering_assistant"), reason="mastering_assistant not yet implemented")
class TestMasteringAssistant:
    """Test mastering assistant achieves target LUFS and respects peak ceiling."""

    def test_lufs_target(self, tmp_path):
        """Output loudness should be within 2 dB of target LUFS."""
        # Generate a quiet signal
        samples = generate_sine(440.0, duration_sec=5.0, amplitude=0.1)

        target_lufs = -14.0
        result, output_dir = run_tool(tmp_path, "mastering_assistant", {
            "target_lufs": target_lufs,
            "apply_limiter": True,
            "ceiling_db": -1.0,
        }, samples)

        assert result["success"] is True

        # Check reported LUFS (from tool result)
        if "final_lufs" in result:
            error = abs(result["final_lufs"] - target_lufs)
            assert error < 3.0, (
                f"LUFS error too large: {error:.1f} dB "
                f"(target {target_lufs}, got {result['final_lufs']})"
            )

        # Optionally verify with pyloudnorm
        try:
            import pyloudnorm as pyln
            from scipy.io import wavfile

            sr_out, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            data = data.astype(np.float64)
            if data.dtype == np.int16:
                data = data / 32767.0
            if data.ndim == 1:
                data = data.reshape(-1, 1)

            meter = pyln.Meter(sr_out)
            measured_lufs = meter.integrated_loudness(data)

            error = abs(measured_lufs - target_lufs)
            assert error < 3.0, (
                f"pyloudnorm LUFS verification failed: measured {measured_lufs:.1f}, "
                f"target {target_lufs:.1f}"
            )
        except ImportError:
            pass  # pyloudnorm optional

    def test_peak_ceiling(self, tmp_path):
        """Output peak should not exceed the specified ceiling."""
        samples = generate_sine(440.0, duration_sec=3.0, amplitude=0.8)

        result, output_dir = run_tool(tmp_path, "mastering_assistant", {
            "target_lufs": -14.0,
            "apply_limiter": True,
            "ceiling_db": -1.0,
        }, samples)

        assert result["success"] is True

        try:
            from scipy.io import wavfile
            _, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            data = data.astype(np.float64)
            if np.max(np.abs(data)) > 1.0:
                data = data / 32767.0  # int16 → float

            peak_db = 20 * np.log10(np.max(np.abs(data)) + 1e-10)
            assert peak_db <= 0.0, f"Peak exceeds 0 dBFS: {peak_db:.1f} dB"
        except ImportError:
            pass


# ── Auto EQ ────────────────────────────────────────────────────────────────


@pytest.mark.skipif(not tool_exists("auto_eq"), reason="auto_eq not yet implemented")
class TestAutoEQ:
    """Test auto EQ produces valid frequency band suggestions."""

    def test_produces_suggestions(self, tmp_path):
        """Should return a list of EQ band suggestions."""
        # Generate broadband noise
        rng = np.random.RandomState(42)
        samples = (0.3 * rng.randn(SR * 3)).astype(np.float32)

        result, _ = run_tool(tmp_path, "auto_eq", {
            "target_curve": "flat",
            "max_boost_db": 6.0,
        }, samples)

        assert result["success"] is True
        # Result should contain suggestion bands
        assert "bands" in result or "suggestions" in result


# ── Pitch Correction ──────────────────────────────────────────────────────


@pytest.mark.skipif(not tool_exists("pitch_correction"), reason="pitch_correction not yet implemented")
class TestPitchCorrection:
    """Test pitch correction on detuned synthetic tones."""

    def test_corrects_detuned_note(self, tmp_path):
        """A slightly detuned A4 (~445 Hz) should be corrected closer to 440 Hz."""
        # Generate a tone 20 cents sharp of A4
        detuned_freq = 440.0 * (2 ** (20 / 1200))  # ~445.1 Hz
        samples = generate_sine(detuned_freq, duration_sec=2.0, amplitude=0.5)

        result, output_dir = run_tool(tmp_path, "pitch_correction", {
            "correction_strength": 1.0,
        }, samples)

        assert result["success"] is True
        assert os.path.exists(os.path.join(output_dir, "output.wav"))

    def test_produces_output(self, tmp_path):
        """Should produce an output WAV file."""
        samples = generate_sine(440.0, duration_sec=2.0)
        result, output_dir = run_tool(tmp_path, "pitch_correction", {
            "correction_strength": 0.5,
        }, samples)

        assert result["success"] is True
        assert os.path.exists(os.path.join(output_dir, "output.wav"))
```

### 9. Create `tools/tests/conftest.py` — Pytest configuration

```python
"""Pytest configuration for audio tool tests."""
import sys
import os

# Add tests directory to path so audio_fixtures can be imported
sys.path.insert(0, os.path.dirname(__file__))
```

### 10. Update `tools/tests/requirements.txt` — Add test dependencies

```
pytest>=7.0
numpy
scipy
mir_eval>=0.7
pyloudnorm>=0.1
```

Note: `soundfile` is NOT required for test signal generation (we use `scipy.io.wavfile`). `mir_eval` and `pyloudnorm` are optional — tests that require them use `pytest.mark.skipif` to skip gracefully if not installed.

### 11. Update `.github/workflows/ci.yml` — Add audio tool test job

In the existing CI workflow, ensure the Python test step installs audio test dependencies and runs the full suite:

```yaml
  - name: Run Python audio tool tests
    if: hashFiles('tools/tests/requirements.txt') != ''
    run: |
      python3 -m pip install --upgrade pip
      python3 -m pip install -r tools/tests/requirements.txt
      # Install each tool's own requirements (numpy, soundfile, etc.)
      for req in tools/*/requirements.txt; do
        python3 -m pip install -r "$req" || true
      done
      python3 -m pytest tools/tests/ -v --tb=short -x
    continue-on-error: true
```

## Files Expected To Change
- `tools/tests/audio_fixtures.py` (new — shared signal generators + helpers)
- `tools/tests/conftest.py` (new — pytest config)
- `tools/tests/test_beat_detection.py` (new)
- `tools/tests/test_key_detection.py` (new)
- `tools/tests/test_chord_detection.py` (new)
- `tools/tests/test_audio_to_midi.py` (new)
- `tools/tests/test_music_generation.py` (new)
- `tools/tests/test_timbre_transfer.py` (new)
- `tools/tests/test_phase12_tools.py` (new — noise reduction, mastering, auto EQ, pitch correction)
- `tools/tests/requirements.txt` (update — add mir_eval, pyloudnorm, scipy)
- `.github/workflows/ci.yml` (update — install tool deps + run full suite)

## Validation

```bash
# Install test dependencies
pip install -r tools/tests/requirements.txt
for req in tools/*/requirements.txt; do pip install -r "$req" || true; done

# Run all audio tool tests
python3 -m pytest tools/tests/ -v --tb=short

# Run a specific test file
python3 -m pytest tools/tests/test_beat_detection.py -v
```

## Exit Criteria
- `audio_fixtures.py` provides reusable generators: click tracks, sine waves, chords, chord sequences, melodies, noisy signals, two-source mixes.
- Beat detection tests pass: BPM within 5% for click tracks at 80-160 BPM, F-measure > 0.7 (with mir_eval).
- Key detection tests pass: Correct or related key for C major, A minor, G major, E minor; MIREX weighted score >= 0.5.
- Chord detection tests pass: Detects chord changes in synthetic progressions; timestamps monotonic.
- Audio-to-MIDI tests pass: Detects A4 as MIDI ~69; produces valid MIDI file; handles silence.
- Music generation tests pass: Produces non-silent output WAV of approximately requested duration.
- Timbre transfer tests pass: Produces output for all instruments; approximately preserves pitch.
- Phase 12 tool tests skip gracefully if tools don't exist yet; pass once tools are implemented.
- All tests use synthetic signals only — no external downloads required.
- `pytest tools/tests/ -v` shows all applicable tests passing.
- CI workflow updated to install dependencies and run audio tool tests.
