"""Pitch correction — shifts pitch of monophonic audio to nearest semitone."""

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
        data_int = np.clip(data * 32767, -32768, 32767).astype(np.int16)
        wavfile.write(file_path, sr, data_int)

# Scale intervals in semitones from root
SCALE_INTERVALS = {
    "major": [0, 2, 4, 5, 7, 9, 11],
    "minor": [0, 2, 3, 5, 7, 8, 10],
}

NOTE_MAP = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3,
            'E': 4, 'F': 5, 'F#': 6, 'Gb': 6, 'G': 7, 'G#': 8, 'Ab': 8,
            'A': 9, 'A#': 10, 'Bb': 10, 'B': 11}

def parse_key(key_str):
    """Parse key string like 'C major' into (root_semitone, scale_intervals) or None."""
    if not key_str:
        return None
    parts = key_str.strip().split()
    if len(parts) < 2:
        return None
    root_name = parts[0]
    quality = parts[1].lower()
    root = NOTE_MAP.get(root_name)
    intervals = SCALE_INTERVALS.get(quality)
    if root is None or intervals is None:
        return None
    # Build set of allowed MIDI note classes
    allowed = set()
    for interval in intervals:
        allowed.add((root + interval) % 12)
    return allowed

def nearest_allowed_midi(midi_note, allowed_set):
    """Find the nearest MIDI note that's in the allowed set."""
    if allowed_set is None:
        return round(midi_note)

    note_class = round(midi_note) % 12
    if note_class in allowed_set:
        return round(midi_note)

    # Search up and down
    for offset in range(1, 7):
        if (note_class + offset) % 12 in allowed_set:
            return round(midi_note) + offset
        if (note_class - offset) % 12 in allowed_set:
            return round(midi_note) - offset

    return round(midi_note)

def pitch_shift_frame(frame, shift_semitones, sr):
    """Simple pitch shift using resampling (changes length slightly)."""
    if abs(shift_semitones) < 0.01:
        return frame

    ratio = 2.0 ** (shift_semitones / 12.0)
    indices = np.arange(0, len(frame), ratio)
    indices = indices[indices < len(frame) - 1].astype(int)

    if len(indices) == 0:
        return frame

    shifted = frame[indices]

    # Pad or trim to original length
    if len(shifted) < len(frame):
        shifted = np.pad(shifted, (0, len(frame) - len(shifted)))
    else:
        shifted = shifted[:len(frame)]

    return shifted

def estimate_pitch_yin(frame, sr, fmin=80, fmax=1000):
    """Simplified YIN pitch estimation."""
    tau_min = max(1, int(sr / fmax))
    tau_max = min(int(sr / fmin), len(frame) // 2)

    if tau_max <= tau_min:
        return 0.0

    d = np.zeros(tau_max)
    for tau in range(1, tau_max):
        diff = frame[:tau_max - tau] - frame[tau:tau_max]
        d[tau] = np.sum(diff ** 2)

    d_prime = np.ones(tau_max)
    running_sum = 0.0
    for tau in range(1, tau_max):
        running_sum += d[tau]
        if running_sum > 0:
            d_prime[tau] = d[tau] * tau / running_sum

    threshold = 0.2
    for tau in range(tau_min, tau_max - 1):
        if d_prime[tau] < threshold and d_prime[tau] < d_prime[tau + 1]:
            return sr / tau

    return 0.0

def main():
    request = json.loads(sys.stdin.read())
    params = request.get("params", {})
    input_file = request.get("input_file")
    output_dir = request.get("output_dir", "/tmp")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    correction_strength = float(params.get("correction_strength", 0.5))
    key_str = params.get("key", "")
    allowed_notes = parse_key(key_str)

    try:
        samples, sr = load_audio(input_file)

        frame_length = 2048
        hop_length = frame_length // 4
        window = np.hanning(frame_length)
        n_frames = max(1, 1 + (len(samples) - frame_length) // hop_length)

        output = np.zeros(len(samples))
        window_sum = np.zeros(len(samples))

        corrections_applied = 0

        for i in range(n_frames):
            start = i * hop_length
            frame = samples[start:start + frame_length]
            if len(frame) < frame_length:
                frame = np.pad(frame, (0, frame_length - len(frame)))

            rms = np.sqrt(np.mean(frame ** 2))
            if rms < 0.005:
                # Quiet frame — pass through
                output[start:start + frame_length] += frame * window
                window_sum[start:start + frame_length] += window ** 2
                continue

            freq = estimate_pitch_yin(frame, sr)
            if freq <= 0:
                output[start:start + frame_length] += frame * window
                window_sum[start:start + frame_length] += window ** 2
                continue

            midi_note = 12 * np.log2(freq / 440.0) + 69
            target_midi = nearest_allowed_midi(midi_note, allowed_notes)
            shift = (target_midi - midi_note) * correction_strength

            if abs(shift) > 0.05:
                corrected = pitch_shift_frame(frame, shift, sr)
                corrections_applied += 1
            else:
                corrected = frame

            output[start:start + frame_length] += corrected * window
            window_sum[start:start + frame_length] += window ** 2

        nonzero = window_sum > 1e-10
        output[nonzero] /= window_sum[nonzero]

        base_name = os.path.splitext(os.path.basename(input_file))[0]
        output_path = os.path.join(output_dir, base_name + "_corrected.wav")
        os.makedirs(output_dir, exist_ok=True)
        save_audio(output_path, output, sr)

        result = {
            "status": "ok",
            "results": {
                "corrections_applied": corrections_applied,
                "total_frames": n_frames,
                "correction_strength": correction_strength,
                "key_constraint": key_str if key_str else "chromatic",
                "duration": round(len(samples) / sr, 3)
            },
            "output_files": [output_path]
        }
        print(json.dumps(result))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
