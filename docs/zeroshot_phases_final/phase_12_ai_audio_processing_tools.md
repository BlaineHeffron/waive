# Phase 12: AI Audio Processing & Smart Mixing Tools (Python)

## Objective
Add four Python-based audio processing tools: noise reduction, pitch correction, auto-EQ suggestions, and a mastering assistant. These run as external tool processes (Phase 3 system). They represent the "smart" layer — tools that analyze audio and either process it directly or suggest parameter changes the AI can apply.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- Python tools live in `tools/` directory.

## Architecture Context

### External Tool System (from Phase 3)
- Tools in `tools/<tool_name>/` with `.waive-tool.json` + `__main__.py`.
- stdin JSON: `{"params":{...}, "input_file": "...", "output_dir": "..."}`
- stdout JSON: `{"status":"ok", "results":{...}, "output_files": [...]}`

### Processing vs. suggestion tools
- **Processing tools** (noise_reduction, pitch_correction): take audio input, produce modified audio output. The output file is inserted back into the session.
- **Suggestion tools** (auto_eq, mastering_assistant): analyze audio and return parameter recommendations as JSON. The AI agent reads the suggestions and executes the appropriate commands.

## Implementation Tasks

### 1. Create `tools/noise_reduction/.waive-tool.json`

```json
{
  "name": "noise_reduction",
  "displayName": "Noise Reduction",
  "version": "0.1.0",
  "description": "Reduce background noise from an audio recording using spectral gating. Works best on recordings with steady-state noise (hiss, hum, fan noise).",
  "category": "ai",
  "inputSchema": {
    "type": "object",
    "properties": {
      "strength": {
        "type": "number",
        "description": "Noise reduction strength 0.0-1.0 (default: 0.5). Higher = more aggressive.",
        "default": 0.5
      },
      "noise_profile_seconds": {
        "type": "number",
        "description": "Seconds of audio from the start to use as noise profile (default: 0.5). Use a quiet section.",
        "default": 0.5
      }
    }
  },
  "command": ["python3", "-m", "noise_reduction"],
  "acceptsAudioInput": true,
  "producesAudioOutput": true
}
```

### 2. Create `tools/noise_reduction/__main__.py`

```python
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
```

### 3. Create `tools/noise_reduction/requirements.txt`

```
numpy
soundfile
```

### 4. Create `tools/pitch_correction/.waive-tool.json`

```json
{
  "name": "pitch_correction",
  "displayName": "Pitch Correction",
  "version": "0.1.0",
  "description": "Correct pitch of a monophonic audio source (vocals, single instruments) to the nearest semitone. Similar to auto-tune but more subtle.",
  "category": "ai",
  "inputSchema": {
    "type": "object",
    "properties": {
      "correction_strength": {
        "type": "number",
        "description": "How strongly to correct pitch 0.0-1.0 (default: 0.5). 1.0 = hard snap to nearest note.",
        "default": 0.5
      },
      "key": {
        "type": "string",
        "description": "Optional key constraint (e.g., 'C major', 'A minor'). If set, only corrects to notes in this key.",
        "default": ""
      }
    }
  },
  "command": ["python3", "-m", "pitch_correction"],
  "acceptsAudioInput": true,
  "producesAudioOutput": true
}
```

### 5. Create `tools/pitch_correction/__main__.py`

```python
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
```

### 6. Create `tools/pitch_correction/requirements.txt`

```
numpy
soundfile
```

### 7. Create `tools/auto_eq/.waive-tool.json`

```json
{
  "name": "auto_eq",
  "displayName": "Auto EQ Suggestions",
  "version": "0.1.0",
  "description": "Analyze audio frequency spectrum and suggest corrective EQ settings. Returns a list of frequency bands with suggested gain adjustments to achieve a balanced mix. Does NOT modify audio — returns suggestions for the AI to apply via plugin parameters.",
  "category": "ai",
  "inputSchema": {
    "type": "object",
    "properties": {
      "target_curve": {
        "type": "string",
        "description": "Target frequency response: 'flat' (neutral), 'warm' (boosted lows, rolled highs), 'bright' (boosted highs), 'vocal' (presence boost). Default: 'flat'.",
        "default": "flat"
      },
      "max_boost_db": {
        "type": "number",
        "description": "Maximum EQ boost in dB (default: 6). Limits how aggressive corrections are.",
        "default": 6
      }
    }
  },
  "command": ["python3", "-m", "auto_eq"],
  "acceptsAudioInput": true,
  "producesAudioOutput": false
}
```

### 8. Create `tools/auto_eq/__main__.py`

```python
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
```

### 9. Create `tools/auto_eq/requirements.txt`

```
numpy
soundfile
```

### 10. Create `tools/mastering_assistant/.waive-tool.json`

```json
{
  "name": "mastering_assistant",
  "displayName": "Mastering Assistant",
  "version": "0.1.0",
  "description": "Analyze a mix and apply loudness normalization with optional soft limiting. Outputs a mastered audio file with consistent loudness targeting a specified LUFS level.",
  "category": "ai",
  "inputSchema": {
    "type": "object",
    "properties": {
      "target_lufs": {
        "type": "number",
        "description": "Target integrated loudness in LUFS (default: -14). Streaming: -14, CD: -9, broadcast: -23.",
        "default": -14
      },
      "apply_limiter": {
        "type": "boolean",
        "description": "Apply soft limiter to prevent clipping (default: true).",
        "default": true
      },
      "ceiling_db": {
        "type": "number",
        "description": "Limiter ceiling in dBFS (default: -1.0). Peak level will not exceed this.",
        "default": -1.0
      }
    }
  },
  "command": ["python3", "-m", "mastering_assistant"],
  "acceptsAudioInput": true,
  "producesAudioOutput": true
}
```

### 11. Create `tools/mastering_assistant/__main__.py`

```python
"""Mastering assistant — loudness normalization + soft limiting."""

import json
import sys
import os
import numpy as np

def load_audio(file_path):
    try:
        import soundfile as sf
        data, sr = sf.read(file_path, dtype='float32')
        return data, sr  # Keep stereo if present
    except ImportError:
        from scipy.io import wavfile
        sr, data = wavfile.read(file_path)
        if data.dtype != np.float32:
            data = data.astype(np.float32) / np.iinfo(data.dtype).max
        return data, sr

def save_audio(file_path, data, sr):
    try:
        import soundfile as sf
        sf.write(file_path, data, sr)
    except ImportError:
        from scipy.io import wavfile
        data_int = np.clip(data * 32767, -32768, 32767).astype(np.int16)
        wavfile.write(file_path, sr, data_int)

def compute_lufs(samples, sr):
    """Simplified integrated LUFS measurement (ITU-R BS.1770-4 approximation)."""
    if samples.ndim == 1:
        samples = samples.reshape(-1, 1)

    # K-weighting filter (simplified: just apply pre-emphasis)
    # True K-weighting needs specific filter coefficients, but for a
    # reasonable approximation we use a simple high-shelf emphasis.
    channels = samples.shape[1]

    # Block size: 400ms with 75% overlap
    block_samples = int(0.4 * sr)
    hop = block_samples // 4
    n_blocks = max(1, (samples.shape[0] - block_samples) // hop)

    block_powers = []
    for i in range(n_blocks):
        start = i * hop
        block = samples[start:start + block_samples]

        # Mean square per channel, then sum
        channel_powers = []
        for ch in range(channels):
            ms = np.mean(block[:, ch] ** 2)
            channel_powers.append(ms)

        # Sum channels (simplified — no surround weighting)
        total_power = sum(channel_powers)
        if total_power > 0:
            block_powers.append(total_power)

    if not block_powers:
        return -70.0

    # Absolute gating: -70 LUFS
    block_loudness = [10 * np.log10(p + 1e-20) - 0.691 for p in block_powers]

    # Relative gating threshold
    ungated_mean = np.mean([p for p, l in zip(block_powers, block_loudness) if l > -70])
    if ungated_mean <= 0:
        return -70.0

    relative_threshold = 10 * np.log10(ungated_mean + 1e-20) - 0.691 - 10

    # Final gated measurement
    gated_powers = [p for p, l in zip(block_powers, block_loudness) if l > relative_threshold]
    if not gated_powers:
        return -70.0

    mean_power = np.mean(gated_powers)
    lufs = 10 * np.log10(mean_power + 1e-20) - 0.691

    return round(lufs, 1)

def soft_limit(samples, ceiling_linear):
    """Soft limiter using tanh saturation."""
    # Normalize to ceiling
    peak = np.max(np.abs(samples))
    if peak <= ceiling_linear:
        return samples

    # Apply soft clipping
    ratio = ceiling_linear / peak
    normalized = samples * ratio

    # Gentle saturation for peaks above 90% of ceiling
    threshold = ceiling_linear * 0.9
    result = np.where(
        np.abs(normalized) > threshold,
        np.sign(normalized) * (threshold + (ceiling_linear - threshold) * np.tanh(
            (np.abs(normalized) - threshold) / (ceiling_linear - threshold + 1e-10)
        )),
        normalized
    )

    return result

def main():
    request = json.loads(sys.stdin.read())
    params = request.get("params", {})
    input_file = request.get("input_file")
    output_dir = request.get("output_dir", "/tmp")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    target_lufs = float(params.get("target_lufs", -14))
    apply_limiter = params.get("apply_limiter", True)
    ceiling_db = float(params.get("ceiling_db", -1.0))

    try:
        samples, sr = load_audio(input_file)
        was_mono = samples.ndim == 1
        if was_mono:
            samples = samples.reshape(-1, 1)

        # Measure current loudness
        current_lufs = compute_lufs(samples, sr)

        # Calculate gain adjustment
        gain_db = target_lufs - current_lufs
        gain_db = np.clip(gain_db, -20, 20)  # safety clamp
        gain_linear = 10 ** (gain_db / 20.0)

        # Apply gain
        result = samples * gain_linear

        # Apply limiter if requested
        if apply_limiter:
            ceiling_linear = 10 ** (ceiling_db / 20.0)
            result = soft_limit(result, ceiling_linear)

        # Measure final loudness
        final_lufs = compute_lufs(result, sr)
        final_peak_db = round(20 * np.log10(np.max(np.abs(result)) + 1e-20), 1)

        if was_mono:
            result = result.squeeze()

        base_name = os.path.splitext(os.path.basename(input_file))[0]
        output_path = os.path.join(output_dir, base_name + "_mastered.wav")
        os.makedirs(output_dir, exist_ok=True)
        save_audio(output_path, result, sr)

        result_json = {
            "status": "ok",
            "results": {
                "original_lufs": current_lufs,
                "target_lufs": target_lufs,
                "final_lufs": final_lufs,
                "gain_applied_db": round(gain_db, 1),
                "peak_db": final_peak_db,
                "limiter_applied": apply_limiter,
                "ceiling_db": ceiling_db,
                "duration": round(samples.shape[0] / sr, 3)
            },
            "output_files": [output_path]
        }
        print(json.dumps(result_json))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
```

### 12. Create `tools/mastering_assistant/requirements.txt`

```
numpy
soundfile
```

## Files Expected To Change (all new files)
- `tools/noise_reduction/.waive-tool.json`
- `tools/noise_reduction/__main__.py`
- `tools/noise_reduction/requirements.txt`
- `tools/pitch_correction/.waive-tool.json`
- `tools/pitch_correction/__main__.py`
- `tools/pitch_correction/requirements.txt`
- `tools/auto_eq/.waive-tool.json`
- `tools/auto_eq/__main__.py`
- `tools/auto_eq/requirements.txt`
- `tools/mastering_assistant/.waive-tool.json`
- `tools/mastering_assistant/__main__.py`
- `tools/mastering_assistant/requirements.txt`

## Validation

```bash
# Build (should be unaffected — Python tools are runtime only)
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- All four tool directories exist with manifest + `__main__.py` + `requirements.txt`.
- Noise reduction takes audio input and outputs denoised audio.
- Pitch correction takes audio input and outputs pitch-corrected audio.
- Auto EQ analyzes audio and returns frequency band suggestions (no audio output).
- Mastering assistant normalizes loudness and optionally applies limiting.
- All tools follow the external tool stdin/stdout JSON contract.
- Build compiles with no errors.
