"""Key detection tool — estimates musical key using Krumhansl-Schmuckler algorithm."""

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

# Krumhansl-Kessler key profiles
MAJOR_PROFILE = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
MINOR_PROFILE = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]

def compute_chroma_summary(samples, sr):
    """Compute average chroma vector for the entire audio."""
    frame_length = 4096
    hop_length = 2048
    n_frames = max(1, 1 + (len(samples) - frame_length) // hop_length)
    chroma = np.zeros(12)
    window = np.hanning(frame_length)

    for i in range(n_frames):
        start = i * hop_length
        end = start + frame_length
        if end > len(samples):
            break

        frame = samples[start:end] * window
        spectrum = np.abs(np.fft.rfft(frame))
        freqs = np.fft.rfftfreq(frame_length, 1.0 / sr)

        for j, freq in enumerate(freqs):
            if freq < 65 or freq > 2000:
                continue
            if freq > 0:
                midi = 12 * np.log2(freq / 440.0) + 69
                chroma_bin = int(round(midi)) % 12
                chroma[chroma_bin] += spectrum[j] ** 2

    norm = np.linalg.norm(chroma)
    if norm > 0:
        chroma /= norm

    return chroma

def detect_key(chroma):
    """Use Krumhansl-Schmuckler algorithm to find best key."""
    best_corr = -2
    best_key = "C major"

    results = []

    for root in range(12):
        # Rotate chroma to align with this root
        rotated = np.roll(chroma, -root)

        # Correlate with major profile
        major_corr = np.corrcoef(rotated, MAJOR_PROFILE)[0, 1]
        results.append((NOTE_NAMES[root] + " major", major_corr))

        if major_corr > best_corr:
            best_corr = major_corr
            best_key = NOTE_NAMES[root] + " major"

        # Correlate with minor profile
        minor_corr = np.corrcoef(rotated, MINOR_PROFILE)[0, 1]
        results.append((NOTE_NAMES[root] + " minor", minor_corr))

        if minor_corr > best_corr:
            best_corr = minor_corr
            best_key = NOTE_NAMES[root] + " minor"

    # Sort by correlation for top alternatives
    results.sort(key=lambda x: x[1], reverse=True)
    alternatives = [{"key": r[0], "correlation": round(r[1], 3)} for r in results[:5]]

    return best_key, round(best_corr, 3), alternatives

def main():
    request = json.loads(sys.stdin.read())
    input_file = request.get("input_file")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    try:
        samples, sr = load_audio(input_file)
        chroma = compute_chroma_summary(samples, sr)
        key, confidence, alternatives = detect_key(chroma)

        result = {
            "status": "ok",
            "results": {
                "key": key,
                "confidence": confidence,
                "alternatives": alternatives,
                "duration": round(len(samples) / sr, 3)
            }
        }
        print(json.dumps(result))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
