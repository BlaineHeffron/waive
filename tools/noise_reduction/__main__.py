"""Noise reduction via spectral gating — learns noise profile from quiet section, then gates."""

import json
import sys
import os
import numpy as np

def load_audio(file_path):
    try:
        import soundfile as sf
        data, sr = sf.read(file_path, dtype='float32')
        if data.ndim > 1:
            data = data.mean(axis=1)
        return data, sr
    except ImportError:
        from scipy.io import wavfile
        sr, data = wavfile.read(file_path)
        if data.dtype != np.float32:
            data = data.astype(np.float32) / np.iinfo(data.dtype).max
        if data.ndim > 1:
            data = data.mean(axis=1)
        return data, sr

def save_audio(file_path, data, sr):
    try:
        import soundfile as sf
        sf.write(file_path, data, sr)
    except ImportError:
        from scipy.io import wavfile
        # Convert to int16
        data_int = np.clip(data * 32767, -32768, 32767).astype(np.int16)
        wavfile.write(file_path, sr, data_int)

def spectral_gate(samples, sr, noise_profile_seconds=0.5, strength=0.5):
    """Apply spectral gating for noise reduction."""
    frame_length = 2048
    hop_length = frame_length // 4
    window = np.hanning(frame_length)

    # Estimate noise spectrum from the first N seconds
    noise_samples = int(noise_profile_seconds * sr)
    noise_samples = min(noise_samples, len(samples))
    noise_section = samples[:noise_samples]

    # Compute average noise magnitude spectrum
    n_noise_frames = max(1, (len(noise_section) - frame_length) // hop_length)
    noise_spectrum = np.zeros(frame_length // 2 + 1)

    for i in range(n_noise_frames):
        start = i * hop_length
        frame = noise_section[start:start + frame_length]
        if len(frame) < frame_length:
            frame = np.pad(frame, (0, frame_length - len(frame)))
        spectrum = np.abs(np.fft.rfft(frame * window))
        noise_spectrum += spectrum

    noise_spectrum /= max(n_noise_frames, 1)
    noise_threshold = noise_spectrum * (1.0 + strength * 3.0)

    # Process full audio with overlap-add
    n_frames = 1 + (len(samples) - frame_length) // hop_length
    output = np.zeros(len(samples))
    window_sum = np.zeros(len(samples))

    for i in range(n_frames):
        start = i * hop_length
        frame = samples[start:start + frame_length]
        if len(frame) < frame_length:
            frame = np.pad(frame, (0, frame_length - len(frame)))

        windowed = frame * window
        spectrum = np.fft.rfft(windowed)
        magnitude = np.abs(spectrum)
        phase = np.angle(spectrum)

        # Spectral gate: attenuate bins below noise threshold
        gain = np.ones_like(magnitude)
        mask = magnitude < noise_threshold
        # Soft gating: reduce rather than zero
        gain[mask] = np.maximum(0, 1.0 - strength * (noise_threshold[mask] / (magnitude[mask] + 1e-10)))

        # Reconstruct
        filtered = gain * magnitude * np.exp(1j * phase)
        frame_out = np.fft.irfft(filtered, n=frame_length)

        output[start:start + frame_length] += frame_out * window
        window_sum[start:start + frame_length] += window ** 2

    # Normalize by window sum
    nonzero = window_sum > 1e-10
    output[nonzero] /= window_sum[nonzero]

    return output

def main():
    request = json.loads(sys.stdin.read())
    params = request.get("params", {})
    input_file = request.get("input_file")
    output_dir = request.get("output_dir", "/tmp")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    strength = float(params.get("strength", 0.5))
    noise_profile_seconds = float(params.get("noise_profile_seconds", 0.5))

    try:
        samples, sr = load_audio(input_file)

        result_audio = spectral_gate(samples, sr, noise_profile_seconds, strength)

        base_name = os.path.splitext(os.path.basename(input_file))[0]
        output_path = os.path.join(output_dir, base_name + "_denoised.wav")
        os.makedirs(output_dir, exist_ok=True)
        save_audio(output_path, result_audio, sr)

        result = {
            "status": "ok",
            "results": {
                "strength": strength,
                "noise_profile_seconds": noise_profile_seconds,
                "duration": round(len(samples) / sr, 3)
            },
            "output_files": [output_path]
        }
        print(json.dumps(result))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
