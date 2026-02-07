# Phase 11: AI Audio Analysis Tools (Python)

## Objective
Add four Python-based audio analysis tools that run as external tool processes (using the external tool runner from Phase 3): beat detection, chord detection, key detection, and audio-to-MIDI transcription. Each tool reads audio input, performs analysis, and returns structured results. These are essential for an AI agent to understand audio content before making mixing or editing decisions.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- Python tools live in `tools/` directory (NOT `gui/src/tools/`).
- Each tool has its own subdirectory with `.waive-tool.json` manifest + `__main__.py`.

## Architecture Context

### External Tool System (from Phase 3)
- Tools live in `tools/<tool_name>/` directories.
- Each tool has a `.waive-tool.json` manifest file specifying name, description, inputSchema, command, etc.
- The `ExternalToolRunner` launches tools as child processes.
- stdin: JSON input (params + input audio file path if applicable).
- stdout: JSON output (results + output file paths if applicable).
- stderr: progress/logging.
- Tools are auto-discovered by scanning `tools/` directory.

### .waive-tool.json manifest format (from Phase 3/4)
```json
{
  "name": "tool_name",
  "displayName": "Human Readable Name",
  "version": "0.1.0",
  "description": "What the tool does.",
  "category": "analysis",
  "inputSchema": {
    "type": "object",
    "properties": {
      "param_name": { "type": "string", "description": "..." }
    },
    "required": ["param_name"]
  },
  "command": ["python3", "-m", "tool_name"],
  "acceptsAudioInput": true,
  "producesAudioOutput": false
}
```

### Existing Python tools (from Phase 4)
- `tools/timbre_transfer/` — transforms timbre using synthesis
- `tools/music_generation/` — generates audio from text prompts
- Both use the same manifest + `__main__.py` pattern.

### Python tool stdin/stdout contract
**stdin (JSON):**
```json
{
  "params": { ... },
  "input_file": "/path/to/audio.wav",
  "output_dir": "/path/to/output/"
}
```

**stdout (JSON):**
```json
{
  "status": "ok",
  "results": { ... },
  "output_files": ["/path/to/output/file.wav"]
}
```

## Implementation Tasks

### 1. Create `tools/beat_detection/.waive-tool.json`

```json
{
  "name": "beat_detection",
  "displayName": "Beat Detection",
  "version": "0.1.0",
  "description": "Detect tempo (BPM) and beat positions in an audio file. Returns estimated BPM and an array of beat timestamps in seconds.",
  "category": "analysis",
  "inputSchema": {
    "type": "object",
    "properties": {
      "method": {
        "type": "string",
        "description": "Detection method: 'onset' for onset-based (default), 'autocorrelation' for tempo estimation only",
        "default": "onset"
      }
    }
  },
  "command": ["python3", "-m", "beat_detection"],
  "acceptsAudioInput": true,
  "producesAudioOutput": false
}
```

### 2. Create `tools/beat_detection/__main__.py`

```python
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
```

### 3. Create `tools/beat_detection/requirements.txt`

```
numpy
soundfile
```

### 4. Create `tools/chord_detection/.waive-tool.json`

```json
{
  "name": "chord_detection",
  "displayName": "Chord Detection",
  "version": "0.1.0",
  "description": "Detect chords from an audio file. Returns a time-stamped list of detected chords (e.g., C major, Am, G7).",
  "category": "analysis",
  "inputSchema": {
    "type": "object",
    "properties": {
      "hop_seconds": {
        "type": "number",
        "description": "Analysis hop size in seconds (default: 0.5). Smaller = more granular.",
        "default": 0.5
      }
    }
  },
  "command": ["python3", "-m", "chord_detection"],
  "acceptsAudioInput": true,
  "producesAudioOutput": false
}
```

### 5. Create `tools/chord_detection/__main__.py`

```python
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
```

### 6. Create `tools/chord_detection/requirements.txt`

```
numpy
soundfile
```

### 7. Create `tools/key_detection/.waive-tool.json`

```json
{
  "name": "key_detection",
  "displayName": "Key Detection",
  "version": "0.1.0",
  "description": "Detect the musical key of an audio file. Returns the estimated key (e.g., 'C major', 'A minor') with confidence.",
  "category": "analysis",
  "inputSchema": {
    "type": "object",
    "properties": {}
  },
  "command": ["python3", "-m", "key_detection"],
  "acceptsAudioInput": true,
  "producesAudioOutput": false
}
```

### 8. Create `tools/key_detection/__main__.py`

```python
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
```

### 9. Create `tools/key_detection/requirements.txt`

```
numpy
soundfile
```

### 10. Create `tools/audio_to_midi/.waive-tool.json`

```json
{
  "name": "audio_to_midi",
  "displayName": "Audio to MIDI",
  "version": "0.1.0",
  "description": "Transcribe audio to MIDI notes. Detects pitched content and outputs a MIDI file. Works best on monophonic sources (vocals, bass, lead instruments).",
  "category": "analysis",
  "inputSchema": {
    "type": "object",
    "properties": {
      "min_note_length_ms": {
        "type": "number",
        "description": "Minimum note length in milliseconds (default: 50). Shorter = more notes detected.",
        "default": 50
      },
      "onset_threshold": {
        "type": "number",
        "description": "Onset detection sensitivity 0.0-1.0 (default: 0.3). Lower = more sensitive.",
        "default": 0.3
      }
    }
  },
  "command": ["python3", "-m", "audio_to_midi"],
  "acceptsAudioInput": true,
  "producesAudioOutput": false
}
```

### 11. Create `tools/audio_to_midi/__main__.py`

```python
"""Audio-to-MIDI transcription — detects pitched audio and writes a MIDI file."""

import json
import sys
import os
import struct
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

def estimate_pitch_yin(frame, sr, fmin=65, fmax=2000):
    """Estimate fundamental frequency using YIN algorithm (simplified)."""
    tau_min = int(sr / fmax)
    tau_max = min(int(sr / fmin), len(frame) // 2)

    if tau_max <= tau_min:
        return 0.0

    # Cumulative mean normalized difference
    d = np.zeros(tau_max)
    for tau in range(1, tau_max):
        d[tau] = np.sum((frame[:tau_max - tau] - frame[tau:tau_max]) ** 2)

    # Cumulative mean normalization
    d_prime = np.ones(tau_max)
    running_sum = 0.0
    for tau in range(1, tau_max):
        running_sum += d[tau]
        if running_sum > 0:
            d_prime[tau] = d[tau] * tau / running_sum

    # Find first dip below threshold
    threshold = 0.15
    for tau in range(tau_min, tau_max - 1):
        if d_prime[tau] < threshold and d_prime[tau] < d_prime[tau + 1]:
            # Parabolic interpolation
            if tau > 0 and tau < tau_max - 1:
                alpha = d_prime[tau - 1]
                beta = d_prime[tau]
                gamma = d_prime[tau + 1]
                denom = 2.0 * (alpha - 2 * beta + gamma)
                if abs(denom) > 1e-10:
                    peak = tau + (alpha - gamma) / denom
                    return sr / peak
            return sr / tau

    return 0.0  # No pitch detected

def freq_to_midi(freq):
    """Convert frequency to MIDI note number."""
    if freq <= 0:
        return 0
    midi = 12 * np.log2(freq / 440.0) + 69
    return int(round(midi))

def write_midi_file(notes, output_path, ticks_per_beat=480, bpm=120):
    """Write a simple MIDI file (format 0) from a list of (start_sec, end_sec, midi_note, velocity)."""

    def var_length(value):
        result = []
        result.append(value & 0x7F)
        value >>= 7
        while value:
            result.append((value & 0x7F) | 0x80)
            value >>= 7
        result.reverse()
        return bytes(result)

    us_per_beat = int(60_000_000 / bpm)

    # Build track events
    events = []
    for start_sec, end_sec, note, velocity in notes:
        start_tick = int(start_sec * bpm / 60.0 * ticks_per_beat)
        end_tick = int(end_sec * bpm / 60.0 * ticks_per_beat)
        events.append((start_tick, 0x90, note, velocity))   # note on
        events.append((end_tick,   0x80, note, 0))           # note off

    events.sort(key=lambda e: (e[0], e[1]))

    # Build track data
    track_data = bytearray()

    # Tempo meta-event
    track_data += b'\x00\xFF\x51\x03'
    track_data += struct.pack('>I', us_per_beat)[1:]

    prev_tick = 0
    for tick, status, note, vel in events:
        delta = tick - prev_tick
        track_data += var_length(max(0, delta))
        track_data += bytes([status, note & 0x7F, vel & 0x7F])
        prev_tick = tick

    # End of track
    track_data += b'\x00\xFF\x2F\x00'

    # Write MIDI file
    with open(output_path, 'wb') as f:
        # Header chunk
        f.write(b'MThd')
        f.write(struct.pack('>I', 6))       # header length
        f.write(struct.pack('>HHH', 0, 1, ticks_per_beat))  # format 0, 1 track

        # Track chunk
        f.write(b'MTrk')
        f.write(struct.pack('>I', len(track_data)))
        f.write(track_data)

def main():
    request = json.loads(sys.stdin.read())
    params = request.get("params", {})
    input_file = request.get("input_file")
    output_dir = request.get("output_dir", "/tmp")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    min_note_ms = params.get("min_note_length_ms", 50)
    onset_threshold = params.get("onset_threshold", 0.3)

    try:
        samples, sr = load_audio(input_file)

        # Analysis parameters
        frame_length = 2048
        hop_length = 512
        n_frames = 1 + (len(samples) - frame_length) // hop_length
        min_note_frames = int(min_note_ms * sr / (1000.0 * hop_length))

        # Detect pitch for each frame
        pitches = []
        for i in range(n_frames):
            start = i * hop_length
            frame = samples[start:start + frame_length]

            # Skip quiet frames
            rms = np.sqrt(np.mean(frame ** 2))
            if rms < onset_threshold * 0.01:
                pitches.append(0)
            else:
                freq = estimate_pitch_yin(frame, sr)
                pitches.append(freq_to_midi(freq))

        # Group consecutive same-pitch frames into notes
        notes = []
        current_note = 0
        note_start = 0
        note_frames = 0

        for i, pitch in enumerate(pitches):
            if pitch == current_note:
                note_frames += 1
            else:
                if current_note > 0 and note_frames >= min_note_frames:
                    start_sec = note_start * hop_length / sr
                    end_sec = (note_start + note_frames) * hop_length / sr
                    notes.append((start_sec, end_sec, current_note, 100))
                current_note = pitch
                note_start = i
                note_frames = 1

        # Don't forget the last note
        if current_note > 0 and note_frames >= min_note_frames:
            start_sec = note_start * hop_length / sr
            end_sec = (note_start + note_frames) * hop_length / sr
            notes.append((start_sec, end_sec, current_note, 100))

        # Write MIDI file
        base_name = os.path.splitext(os.path.basename(input_file))[0]
        midi_path = os.path.join(output_dir, base_name + "_transcribed.mid")
        os.makedirs(output_dir, exist_ok=True)
        write_midi_file(notes, midi_path)

        # Build note summary
        note_names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
        note_summary = []
        for start_sec, end_sec, midi_note, vel in notes:
            name = note_names[midi_note % 12] + str(midi_note // 12 - 1)
            note_summary.append({
                "start": round(start_sec, 3),
                "end": round(end_sec, 3),
                "note": name,
                "midi": midi_note
            })

        result = {
            "status": "ok",
            "results": {
                "note_count": len(notes),
                "notes": note_summary,
                "midi_file": midi_path,
                "duration": round(len(samples) / sr, 3)
            },
            "output_files": [midi_path]
        }
        print(json.dumps(result))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
```

### 12. Create `tools/audio_to_midi/requirements.txt`

```
numpy
soundfile
```

## Files Expected To Change (all new files)
- `tools/beat_detection/.waive-tool.json`
- `tools/beat_detection/__main__.py`
- `tools/beat_detection/requirements.txt`
- `tools/chord_detection/.waive-tool.json`
- `tools/chord_detection/__main__.py`
- `tools/chord_detection/requirements.txt`
- `tools/key_detection/.waive-tool.json`
- `tools/key_detection/__main__.py`
- `tools/key_detection/requirements.txt`
- `tools/audio_to_midi/.waive-tool.json`
- `tools/audio_to_midi/__main__.py`
- `tools/audio_to_midi/requirements.txt`

## Validation

```bash
# Build (should be unaffected — Python tools are runtime only)
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# Quick smoke test for each tool (requires numpy + soundfile installed)
echo '{"input_file":"/dev/null","params":{}}' | python3 -m beat_detection 2>/dev/null || true
echo '{"input_file":"/dev/null","params":{}}' | python3 -m chord_detection 2>/dev/null || true
echo '{"input_file":"/dev/null","params":{}}' | python3 -m key_detection 2>/dev/null || true
echo '{"input_file":"/dev/null","params":{}}' | python3 -m audio_to_midi 2>/dev/null || true
```

## Exit Criteria
- All four tool directories exist with manifest + `__main__.py` + `requirements.txt`.
- Each tool reads audio via stdin JSON contract, outputs results via stdout JSON.
- Beat detection returns BPM + beat timestamps.
- Chord detection returns timestamped chord progression.
- Key detection returns estimated key with confidence.
- Audio-to-MIDI outputs a MIDI file and note list.
- Build compiles with no errors.
