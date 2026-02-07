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
