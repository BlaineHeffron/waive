"""Beat detection tool — estimates BPM and beat positions from audio."""

import json
import sys
import numpy as np

def load_audio(file_path):
    """Load audio file, return (samples, sample_rate). Tries soundfile, falls back to scipy."""
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

def onset_strength(samples, sr, hop_length=512):
    """Compute onset strength envelope using spectral flux."""
    # Short-time energy via simple frame-based approach
    frame_length = 2048
    n_frames = 1 + (len(samples) - frame_length) // hop_length

    # Compute STFT magnitudes
    window = np.hanning(frame_length)
    magnitudes = []
    for i in range(n_frames):
        start = i * hop_length
        frame = samples[start:start + frame_length] * window
        spectrum = np.abs(np.fft.rfft(frame))
        magnitudes.append(spectrum)

    magnitudes = np.array(magnitudes)

    # Spectral flux (positive differences only)
    flux = np.zeros(n_frames)
    for i in range(1, n_frames):
        diff = magnitudes[i] - magnitudes[i - 1]
        flux[i] = np.sum(np.maximum(diff, 0))

    return flux, hop_length

def detect_beats_onset(samples, sr):
    """Detect beats using onset-based peak picking."""
    hop = 512
    flux, _ = onset_strength(samples, sr, hop)

    # Normalize
    if flux.max() > 0:
        flux = flux / flux.max()

    # Adaptive threshold
    kernel_size = int(sr / hop * 0.5)  # 0.5 second window
    if kernel_size < 3:
        kernel_size = 3
    threshold = np.convolve(flux, np.ones(kernel_size) / kernel_size, mode='same') + 0.1

    # Peak picking
    beats = []
    min_interval = int(sr / hop * 0.25)  # minimum 0.25s between beats
    last_beat = -min_interval

    for i in range(1, len(flux) - 1):
        if (flux[i] > flux[i-1] and flux[i] > flux[i+1] and
            flux[i] > threshold[i] and i - last_beat >= min_interval):
            beats.append(i * hop / sr)
            last_beat = i

    # Estimate BPM from inter-beat intervals
    bpm = 120.0
    if len(beats) >= 2:
        intervals = np.diff(beats)
        median_interval = np.median(intervals)
        if median_interval > 0:
            bpm = 60.0 / median_interval
            # Constrain to reasonable range
            while bpm < 60:
                bpm *= 2
            while bpm > 200:
                bpm /= 2

    return round(bpm, 1), [round(b, 3) for b in beats]

def detect_tempo_autocorrelation(samples, sr):
    """Estimate tempo using autocorrelation of onset envelope."""
    hop = 512
    flux, _ = onset_strength(samples, sr, hop)

    # Autocorrelation
    n = len(flux)
    flux_centered = flux - flux.mean()
    corr = np.correlate(flux_centered, flux_centered, mode='full')
    corr = corr[n-1:]  # positive lags only

    # Search for peak in BPM range 60-200
    min_lag = int(60 / (200 * hop / sr))  # lag for 200 BPM
    max_lag = int(60 / (60 * hop / sr))   # lag for 60 BPM
    max_lag = min(max_lag, len(corr) - 1)

    if max_lag <= min_lag:
        return 120.0, []

    search = corr[min_lag:max_lag+1]
    best_lag = min_lag + np.argmax(search)
    bpm = 60.0 * sr / (best_lag * hop)

    return round(bpm, 1), []

def main():
    request = json.loads(sys.stdin.read())
    params = request.get("params", {})
    input_file = request.get("input_file")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    method = params.get("method", "onset")

    try:
        samples, sr = load_audio(input_file)

        if method == "autocorrelation":
            bpm, beats = detect_tempo_autocorrelation(samples, sr)
        else:
            bpm, beats = detect_beats_onset(samples, sr)

        result = {
            "status": "ok",
            "results": {
                "bpm": bpm,
                "beat_count": len(beats),
                "beats": beats,
                "duration": round(len(samples) / sr, 3),
                "method": method
            }
        }
        print(json.dumps(result))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
