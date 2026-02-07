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

    Returns (input_dir, output_dir, input_wav_path) paths.
    """
    input_dir = os.path.join(str(tmp_path), f"{tool_name}_in")
    output_dir = os.path.join(str(tmp_path), f"{tool_name}_out")
    os.makedirs(input_dir, exist_ok=True)
    os.makedirs(output_dir, exist_ok=True)

    # Write input.wav if audio provided
    input_wav_path = None
    if audio_samples is not None:
        input_wav_path = os.path.join(input_dir, "input.wav")
        save_wav(input_wav_path, audio_samples, sr)

    return input_dir, output_dir, input_wav_path


def read_result(tool_stdout):
    """Parse JSON result from tool's stdout."""
    result = json.loads(tool_stdout)
    if result.get("status") == "error":
        raise RuntimeError(f"Tool error: {result.get('message')}")
    # Return results dict with added "success" field for test compatibility
    results = result.get("results", {})
    results["success"] = (result.get("status") == "ok")
    return results


def read_result_file(output_dir):
    """Read result.json from output directory (old interface)."""
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
