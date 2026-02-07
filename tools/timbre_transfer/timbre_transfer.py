#!/usr/bin/env python3
"""
Timbre Transfer Tool
Transforms audio to sound like different instruments using additive synthesis or spectral filtering.
"""

import argparse
import json
import os
import sys
from pathlib import Path

try:
    import numpy as np
    import scipy.io.wavfile as wavfile
    from scipy import signal
    SCIPY_AVAILABLE = True
except ImportError:
    SCIPY_AVAILABLE = False

try:
    import librosa
    import soundfile as sf
    LIBROSA_AVAILABLE = True
except ImportError:
    LIBROSA_AVAILABLE = False


# Instrument harmonic profiles (relative amplitudes for first 8 harmonics)
INSTRUMENT_PROFILES = {
    "trumpet": [1.0, 0.3, 0.8, 0.2, 0.6, 0.1, 0.4, 0.05],  # Strong odd harmonics
    "violin": [1.0, 0.7, 0.6, 0.5, 0.4, 0.3, 0.25, 0.2],   # Rich harmonics
    "flute": [1.0, 0.2, 0.1, 0.05, 0.03, 0.02, 0.01, 0.01], # Fundamental-heavy
    "saxophone": [1.0, 0.8, 0.6, 0.4, 0.3, 0.2, 0.15, 0.1], # Strong low harmonics
    "cello": [1.0, 0.6, 0.5, 0.4, 0.35, 0.3, 0.25, 0.2],    # Warm, rich
    "piano": [1.0, 0.5, 0.3, 0.2, 0.15, 0.1, 0.08, 0.05],   # Decaying harmonics
}


def write_result(output_dir, success, message, mode=None):
    """Write result.json to output directory."""
    result = {
        "success": success,
        "message": message
    }
    if mode:
        result["mode"] = mode

    result_path = os.path.join(output_dir, "result.json")
    with open(result_path, 'w') as f:
        json.dump(result, f, indent=2)


def process_with_librosa(input_wav, output_wav, target_instrument, sr=22050):
    """Full mode: use librosa for pitch detection and additive synthesis."""
    # Load audio
    y, sr = librosa.load(input_wav, sr=sr)

    # Detect pitch contour
    f0, voiced_flag, voiced_probs = librosa.pyin(
        y, fmin=librosa.note_to_hz('C2'), fmax=librosa.note_to_hz('C7'), sr=sr
    )

    # Detect onsets for amplitude envelope
    onset_frames = librosa.onset.onset_detect(y=y, sr=sr, backtrack=True)
    onset_times = librosa.frames_to_time(onset_frames, sr=sr)

    # Get harmonic profile
    harmonics = INSTRUMENT_PROFILES[target_instrument]

    # Generate output using additive synthesis
    hop_length = 512
    output = np.zeros_like(y)

    for i, (pitch, voiced) in enumerate(zip(f0, voiced_flag)):
        if voiced and not np.isnan(pitch):
            # Time for this frame
            t_start = i * hop_length / sr
            t_end = (i + 1) * hop_length / sr
            n_samples = hop_length
            t = np.linspace(t_start, t_end, n_samples)

            # Create amplitude envelope based on proximity to onsets
            amp = 1.0
            if len(onset_times) > 0:
                time_since_onset = t_start - onset_times[onset_times <= t_start]
                if len(time_since_onset) > 0:
                    time_since_onset = time_since_onset[-1]
                    # Exponential decay from onset
                    amp = np.exp(-time_since_onset * 2.0)

            # Additive synthesis with harmonics
            frame_signal = np.zeros(n_samples)
            for h_idx, h_amp in enumerate(harmonics):
                harmonic_freq = pitch * (h_idx + 1)
                frame_signal += h_amp * np.sin(2 * np.pi * harmonic_freq * t)

            # Apply amplitude envelope
            frame_signal *= amp

            # Add to output
            start_idx = i * hop_length
            end_idx = start_idx + n_samples
            if end_idx <= len(output):
                output[start_idx:end_idx] += frame_signal

    # Normalize
    output = output / (np.max(np.abs(output)) + 1e-8)

    # Write output
    sf.write(output_wav, output, sr)


def process_with_scipy(input_wav, output_wav, target_instrument):
    """Fallback mode: use scipy for basic spectral filtering."""
    # Read input
    sr, y = wavfile.read(input_wav)

    # Convert to float
    if y.dtype == np.int16:
        y = y.astype(np.float32) / 32768.0
    elif y.dtype == np.int32:
        y = y.astype(np.float32) / 2147483648.0

    # If stereo, convert to mono
    if len(y.shape) > 1:
        y = np.mean(y, axis=1)

    # FFT
    Y = np.fft.rfft(y)
    freqs = np.fft.rfftfreq(len(y), 1/sr)

    # Get harmonic profile
    harmonics = INSTRUMENT_PROFILES[target_instrument]

    # Shape the spectrum based on instrument profile
    # Create a spectral envelope that emphasizes certain harmonic bands
    magnitude = np.abs(Y)
    phase = np.angle(Y)

    # Find fundamental frequency (simplistic: just the peak below 1000 Hz)
    low_freq_mask = freqs < 1000
    if np.any(low_freq_mask):
        low_freq_mags = magnitude[low_freq_mask]
        f0_idx = np.argmax(low_freq_mags)
        f0 = freqs[low_freq_mask][f0_idx]
    else:
        f0 = 200.0  # Default

    # Create spectral shaping based on harmonics
    new_magnitude = np.zeros_like(magnitude)
    for h_idx, h_amp in enumerate(harmonics):
        harmonic_freq = f0 * (h_idx + 1)
        # Gaussian window around each harmonic
        bandwidth = f0 * 0.1
        weight = h_amp * np.exp(-((freqs - harmonic_freq) ** 2) / (2 * bandwidth ** 2))
        new_magnitude += weight * magnitude

    # Reconstruct
    Y_new = new_magnitude * np.exp(1j * phase)
    y_out = np.fft.irfft(Y_new, len(y))

    # Normalize
    y_out = y_out / (np.max(np.abs(y_out)) + 1e-8)

    # Convert back to int16
    y_out = (y_out * 32767).astype(np.int16)

    # Write output
    wavfile.write(output_wav, sr, y_out)


def main():
    parser = argparse.ArgumentParser(description="Timbre Transfer Tool")
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

        target_instrument = params.get("target_instrument")
        if not target_instrument:
            write_result(output_dir, False, "Missing required parameter: target_instrument")
            return 1

        if target_instrument not in INSTRUMENT_PROFILES:
            write_result(output_dir, False, f"Invalid instrument: {target_instrument}")
            return 1

        # Check for input.wav
        input_wav = os.path.join(input_dir, "input.wav")
        if not os.path.exists(input_wav):
            write_result(output_dir, False, "Missing input.wav")
            return 1

        # Output path
        output_wav = os.path.join(output_dir, "output.wav")

        # Process based on available libraries
        if LIBROSA_AVAILABLE:
            process_with_librosa(input_wav, output_wav, target_instrument)
            mode = "full"
            message = f"Successfully transformed audio to {target_instrument} timbre using librosa"
        elif SCIPY_AVAILABLE:
            process_with_scipy(input_wav, output_wav, target_instrument)
            mode = "fallback"
            message = f"Successfully transformed audio to {target_instrument} timbre using scipy (fallback mode)"
        else:
            write_result(output_dir, False, "Neither librosa nor scipy available")
            return 1

        # Write success result
        write_result(output_dir, True, message, mode)
        return 0

    except Exception as e:
        write_result(output_dir, False, f"Error: {str(e)}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
