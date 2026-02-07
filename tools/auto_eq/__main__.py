"""Auto EQ — analyzes spectrum and suggests corrective EQ bands."""

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

# Target curves (relative dB offsets per band)
# Bands: sub (30-60), bass (60-250), low_mid (250-500), mid (500-2k), upper_mid (2k-4k), presence (4k-8k), air (8k-16k)
TARGET_CURVES = {
    "flat":    [0, 0, 0, 0, 0, 0, 0],
    "warm":    [2, 3, 1, 0, -1, -2, -3],
    "bright":  [-1, -1, 0, 0, 1, 2, 3],
    "vocal":   [-2, -1, 0, 1, 3, 2, 0],
}

BAND_RANGES = [
    ("sub",       30,   60),
    ("bass",      60,   250),
    ("low_mid",   250,  500),
    ("mid",       500,  2000),
    ("upper_mid", 2000, 4000),
    ("presence",  4000, 8000),
    ("air",       8000, 16000),
]

def analyze_spectrum(samples, sr):
    """Compute average magnitude per frequency band."""
    frame_length = 4096
    hop_length = 2048
    n_frames = max(1, (len(samples) - frame_length) // hop_length)
    freqs = np.fft.rfftfreq(frame_length, 1.0 / sr)

    band_energy = np.zeros(len(BAND_RANGES))
    band_counts = np.zeros(len(BAND_RANGES))
    window = np.hanning(frame_length)

    for i in range(n_frames):
        start = i * hop_length
        frame = samples[start:start + frame_length]
        if len(frame) < frame_length:
            break
        spectrum = np.abs(np.fft.rfft(frame * window))

        for b, (name, lo, hi) in enumerate(BAND_RANGES):
            mask = (freqs >= lo) & (freqs < hi)
            if mask.any():
                band_energy[b] += np.mean(spectrum[mask] ** 2)
                band_counts[b] += 1

    # Average and convert to dB
    band_db = np.zeros(len(BAND_RANGES))
    for b in range(len(BAND_RANGES)):
        if band_counts[b] > 0:
            avg = band_energy[b] / band_counts[b]
            band_db[b] = 10 * np.log10(max(avg, 1e-20))

    return band_db

def suggest_eq(band_db, target_curve_name, max_boost_db):
    """Compare actual spectrum to target curve and suggest corrections."""
    target_offsets = TARGET_CURVES.get(target_curve_name, TARGET_CURVES["flat"])

    # Normalize: find the median band level
    median_level = np.median(band_db)

    suggestions = []
    for i, (name, lo, hi) in enumerate(BAND_RANGES):
        actual_relative = band_db[i] - median_level
        desired_relative = target_offsets[i]
        correction = desired_relative - actual_relative

        # Clamp correction
        correction = np.clip(correction, -max_boost_db, max_boost_db)

        # Only suggest if correction is significant (> 0.5 dB)
        if abs(correction) > 0.5:
            center_freq = int(np.sqrt(lo * hi))  # geometric mean
            suggestions.append({
                "band": name,
                "center_freq_hz": center_freq,
                "low_freq_hz": lo,
                "high_freq_hz": hi,
                "current_level_db": round(float(actual_relative), 1),
                "suggested_gain_db": round(float(correction), 1),
                "reason": f"{'Boost' if correction > 0 else 'Cut'} {name} by {abs(correction):.1f} dB to match {target_curve_name} target"
            })

    return suggestions

def main():
    request = json.loads(sys.stdin.read())
    params = request.get("params", {})
    input_file = request.get("input_file")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    target_curve = params.get("target_curve", "flat")
    max_boost_db = float(params.get("max_boost_db", 6))

    try:
        samples, sr = load_audio(input_file)
        band_db = analyze_spectrum(samples, sr)
        suggestions = suggest_eq(band_db, target_curve, max_boost_db)

        result = {
            "status": "ok",
            "results": {
                "target_curve": target_curve,
                "suggestions": suggestions,
                "suggestion_count": len(suggestions),
                "band_analysis": [
                    {"band": name, "level_db": round(float(band_db[i]), 1)}
                    for i, (name, lo, hi) in enumerate(BAND_RANGES)
                ],
                "duration": round(len(samples) / sr, 3)
            }
        }
        print(json.dumps(result))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
