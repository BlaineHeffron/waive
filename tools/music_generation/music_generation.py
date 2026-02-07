#!/usr/bin/env python3
"""
Music Generation Tool
Generates audio from text prompts using MusicGen or fallback synthesis.
"""

import argparse
import json
import os
import sys
import struct
from pathlib import Path

try:
    import numpy as np
    NUMPY_AVAILABLE = True
except ImportError:
    NUMPY_AVAILABLE = False

try:
    import scipy.io.wavfile as wavfile
    SCIPY_AVAILABLE = True
except ImportError:
    SCIPY_AVAILABLE = False

try:
    from audiocraft.models import MusicGen
    import torch
    import soundfile as sf
    AUDIOCRAFT_AVAILABLE = True
except ImportError:
    AUDIOCRAFT_AVAILABLE = False


def write_result(output_dir, success, message, mode=None, duration=None):
    """Write result.json to output directory."""
    result = {
        "success": success,
        "message": message
    }
    if mode:
        result["mode"] = mode
    if duration is not None:
        result["duration"] = duration

    result_path = os.path.join(output_dir, "result.json")
    with open(result_path, 'w') as f:
        json.dump(result, f, indent=2)


def write_wav_numpy(filename, rate, data):
    """Write WAV file using only numpy and struct (no scipy)."""
    # Normalize to int16
    data = np.clip(data, -1.0, 1.0)
    data = (data * 32767).astype(np.int16)

    # Write WAV manually
    with open(filename, 'wb') as f:
        # RIFF header
        f.write(b'RIFF')
        f.write(struct.pack('<I', 36 + len(data) * 2))
        f.write(b'WAVE')

        # fmt chunk
        f.write(b'fmt ')
        f.write(struct.pack('<I', 16))  # Chunk size
        f.write(struct.pack('<H', 1))   # Audio format (PCM)
        f.write(struct.pack('<H', 1))   # Num channels (mono)
        f.write(struct.pack('<I', rate)) # Sample rate
        f.write(struct.pack('<I', rate * 2)) # Byte rate
        f.write(struct.pack('<H', 2))   # Block align
        f.write(struct.pack('<H', 16))  # Bits per sample

        # data chunk
        f.write(b'data')
        f.write(struct.pack('<I', len(data) * 2))
        f.write(data.tobytes())


def generate_with_audiocraft(prompt, duration_seconds, temperature, output_wav):
    """Full mode: use MusicGen for generation."""
    model = MusicGen.get_pretrained('facebook/musicgen-small')
    model.set_generation_params(duration=duration_seconds, temperature=temperature)

    # Generate
    wav = model.generate([prompt])

    # wav shape is (batch, channels, samples)
    # Convert to (samples,) for mono output
    audio = wav[0].cpu().numpy()
    if audio.shape[0] > 1:
        audio = np.mean(audio, axis=0)
    else:
        audio = audio[0]

    # MusicGen outputs at 32000 Hz
    sf.write(output_wav, audio, 32000)

    return len(audio) / 32000


def apply_envelope(signal, attack_samples, release_samples):
    """Apply simple attack/release envelope."""
    envelope = np.ones_like(signal)

    # Attack
    if attack_samples > 0:
        envelope[:attack_samples] = np.linspace(0, 1, attack_samples)

    # Release
    if release_samples > 0:
        envelope[-release_samples:] = np.linspace(1, 0, release_samples)

    return signal * envelope


def generate_drum_pattern(duration, sr=44100):
    """Generate simple drum pattern."""
    n_samples = int(duration * sr)
    output = np.zeros(n_samples)

    # 120 BPM = 0.5s per beat
    beat_duration = 0.5
    beat_samples = int(beat_duration * sr)

    for beat_idx in range(int(duration / beat_duration)):
        start_idx = beat_idx * beat_samples

        # Kick drum (sine wave with pitch envelope)
        kick_dur = 0.1
        kick_samples = int(kick_dur * sr)
        t = np.linspace(0, kick_dur, kick_samples)
        freq = 60 * np.exp(-t * 20)  # Pitch envelope
        kick = np.sin(2 * np.pi * freq * t) * np.exp(-t * 10)
        if start_idx + kick_samples <= n_samples:
            output[start_idx:start_idx + kick_samples] += kick * 0.5

        # Snare on beats 2 and 4
        if beat_idx % 2 == 1:
            snare_dur = 0.08
            snare_samples = int(snare_dur * sr)
            t = np.linspace(0, snare_dur, snare_samples)
            snare = np.random.randn(snare_samples) * np.exp(-t * 30)
            snare += np.sin(2 * np.pi * 180 * t) * np.exp(-t * 25)
            if start_idx + snare_samples <= n_samples:
                output[start_idx:start_idx + snare_samples] += snare * 0.3

        # Hi-hat (eighth notes)
        for eighth in range(2):
            hihat_start = start_idx + eighth * beat_samples // 2
            hihat_dur = 0.03
            hihat_samples = int(hihat_dur * sr)
            hihat = np.random.randn(hihat_samples) * np.exp(-np.linspace(0, hihat_dur, hihat_samples) * 50)
            if hihat_start + hihat_samples <= n_samples:
                output[hihat_start:hihat_start + hihat_samples] += hihat * 0.15

    return output


def generate_bass_line(duration, sr=44100):
    """Generate simple bass line."""
    n_samples = int(duration * sr)
    output = np.zeros(n_samples)

    # Pentatonic scale in A (A1 = 55 Hz)
    scale = [55, 55 * 9/8, 55 * 5/4, 55 * 3/2, 55 * 7/4]

    note_duration = 0.5
    note_samples = int(note_duration * sr)

    for note_idx in range(int(duration / note_duration)):
        start_idx = note_idx * note_samples
        freq = scale[note_idx % len(scale)]

        t = np.linspace(0, note_duration, note_samples)
        note = np.sin(2 * np.pi * freq * t)
        note += 0.3 * np.sin(2 * np.pi * freq * 2 * t)  # Harmonic
        note = apply_envelope(note, int(0.01 * sr), int(0.1 * sr))

        if start_idx + note_samples <= n_samples:
            output[start_idx:start_idx + note_samples] += note * 0.4

    return output


def generate_pad(duration, sr=44100):
    """Generate ambient chord pad."""
    n_samples = int(duration * sr)
    t = np.linspace(0, duration, n_samples)

    # A minor chord (A3, C4, E4)
    freqs = [220, 261.63, 329.63]
    output = np.zeros(n_samples)

    # Add slow LFO for movement
    lfo = 0.2 * np.sin(2 * np.pi * 0.5 * t)

    for freq in freqs:
        modulated_freq = freq * (1 + lfo)
        output += np.sin(2 * np.pi * modulated_freq * t)

    # Overall envelope
    output = apply_envelope(output, int(0.5 * sr), int(0.5 * sr))

    return output / len(freqs)


def generate_melody(duration, sr=44100):
    """Generate simple melodic pattern."""
    n_samples = int(duration * sr)
    output = np.zeros(n_samples)

    # Pentatonic scale in A (A4 = 440 Hz)
    scale = [440, 440 * 9/8, 440 * 5/4, 440 * 3/2, 440 * 7/4]

    note_duration = 0.25
    note_samples = int(note_duration * sr)

    # Random melody
    np.random.seed(42)

    for note_idx in range(int(duration / note_duration)):
        start_idx = note_idx * note_samples
        freq = scale[np.random.randint(len(scale))]

        t = np.linspace(0, note_duration, note_samples)
        note = np.sin(2 * np.pi * freq * t)
        note = apply_envelope(note, int(0.01 * sr), int(0.05 * sr))

        if start_idx + note_samples <= n_samples:
            output[start_idx:start_idx + note_samples] += note * 0.3

    return output


def generate_with_numpy(prompt, duration_seconds, output_wav):
    """Fallback mode: basic synthesis based on prompt keywords."""
    sr = 44100

    # Parse prompt for keywords
    prompt_lower = prompt.lower()

    if any(kw in prompt_lower for kw in ["drum", "beat", "percussion"]):
        audio = generate_drum_pattern(duration_seconds, sr)
    elif "bass" in prompt_lower:
        audio = generate_bass_line(duration_seconds, sr)
    elif any(kw in prompt_lower for kw in ["ambient", "pad", "atmosphere"]):
        audio = generate_pad(duration_seconds, sr)
    else:
        audio = generate_melody(duration_seconds, sr)

    # Normalize
    audio = audio / (np.max(np.abs(audio)) + 1e-8) * 0.9

    # Write output
    if SCIPY_AVAILABLE:
        audio_int16 = (audio * 32767).astype(np.int16)
        wavfile.write(output_wav, sr, audio_int16)
    else:
        write_wav_numpy(output_wav, sr, audio)

    return duration_seconds


def main():
    parser = argparse.ArgumentParser(description="Music Generation Tool")
    parser.add_argument("--input-dir", required=True, help="Input directory")
    parser.add_argument("--output-dir", required=True, help="Output directory")
    args = parser.parse_args()

    input_dir = args.input_dir
    output_dir = args.output_dir

    # Ensure output directory exists
    os.makedirs(output_dir, exist_ok=True)

    try:
        # Read params.json
        params_path = os.path.join(input_dir, "params.json")
        with open(params_path, 'r') as f:
            params = json.load(f)

        prompt = params.get("prompt")
        if not prompt:
            write_result(output_dir, False, "Missing required parameter: prompt")
            return 1

        duration_seconds = params.get("duration_seconds", 8)
        temperature = params.get("temperature", 1.0)

        # Validate duration
        if duration_seconds < 1 or duration_seconds > 30:
            write_result(output_dir, False, "duration_seconds must be between 1 and 30")
            return 1

        # Validate temperature
        if temperature < 0.1 or temperature > 2.0:
            write_result(output_dir, False, "temperature must be between 0.1 and 2.0")
            return 1

        # Output path
        output_wav = os.path.join(output_dir, "output.wav")

        # Generate based on available libraries
        if AUDIOCRAFT_AVAILABLE:
            actual_duration = generate_with_audiocraft(prompt, duration_seconds, temperature, output_wav)
            mode = "full"
            message = f"Successfully generated {actual_duration:.1f}s of music using MusicGen"
        elif NUMPY_AVAILABLE:
            actual_duration = generate_with_numpy(prompt, duration_seconds, output_wav)
            mode = "fallback"
            message = f"Successfully generated {actual_duration:.1f}s of music using numpy synthesis (fallback mode)"
        else:
            write_result(output_dir, False, "Neither audiocraft nor numpy available")
            return 1

        # Write success result
        write_result(output_dir, True, message, mode, actual_duration)
        return 0

    except Exception as e:
        write_result(output_dir, False, f"Error: {str(e)}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
