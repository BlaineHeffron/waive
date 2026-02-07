"""Chord detection tool — identifies chords from audio using chroma features."""

import json
import sys
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

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

# Chord templates (1 = note present, 0 = absent) — 12 semitones relative to root
CHORD_TEMPLATES = {
    'maj':  [1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0],
    'min':  [1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0],
    '7':    [1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0],
    'min7': [1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0],
    'maj7': [1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1],
    'dim':  [1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0],
    'sus4': [1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0],
    'sus2': [1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0],
}

def compute_chroma(samples, sr, hop_length, frame_length=4096):
    """Compute chroma features from audio."""
    n_frames = max(1, 1 + (len(samples) - frame_length) // hop_length)
    chroma = np.zeros((12, n_frames))
    window = np.hanning(frame_length)

    for i in range(n_frames):
        start = i * hop_length
        end = start + frame_length
        if end > len(samples):
            break

        frame = samples[start:end] * window
        spectrum = np.abs(np.fft.rfft(frame))
        freqs = np.fft.rfftfreq(frame_length, 1.0 / sr)

        # Map frequencies to chroma bins
        for j, freq in enumerate(freqs):
            if freq < 65 or freq > 2000:  # focus on musical range
                continue
            if freq > 0:
                midi = 12 * np.log2(freq / 440.0) + 69
                chroma_bin = int(round(midi)) % 12
                chroma[chroma_bin, i] += spectrum[j] ** 2

    # Normalize each frame
    for i in range(n_frames):
        norm = np.linalg.norm(chroma[:, i])
        if norm > 0:
            chroma[:, i] /= norm

    return chroma

def match_chord(chroma_frame):
    """Match a chroma frame to the best chord template."""
    best_score = -1
    best_chord = "N"  # No chord

    for root in range(12):
        for quality, template in CHORD_TEMPLATES.items():
            # Rotate template to match root
            rotated = np.roll(template, root)
            score = np.dot(chroma_frame, rotated)

            if score > best_score:
                best_score = score
                root_name = NOTE_NAMES[root]
                if quality == 'maj':
                    best_chord = root_name
                elif quality == 'min':
                    best_chord = root_name + 'm'
                else:
                    best_chord = root_name + quality

    return best_chord, best_score

def main():
    request = json.loads(sys.stdin.read())
    params = request.get("params", {})
    input_file = request.get("input_file")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    hop_seconds = params.get("hop_seconds", 0.5)

    try:
        samples, sr = load_audio(input_file)
        hop_length = int(sr * hop_seconds)

        chroma = compute_chroma(samples, sr, hop_length)

        chords = []
        prev_chord = None
        for i in range(chroma.shape[1]):
            chord, confidence = match_chord(chroma[:, i])
            time_sec = round(i * hop_length / sr, 3)

            # Only emit on chord change (avoid repeats)
            if chord != prev_chord:
                chords.append({
                    "time": time_sec,
                    "chord": chord,
                    "confidence": round(float(confidence), 3)
                })
                prev_chord = chord

        result = {
            "status": "ok",
            "results": {
                "chords": chords,
                "chord_count": len(chords),
                "duration": round(len(samples) / sr, 3)
            }
        }
        print(json.dumps(result))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
