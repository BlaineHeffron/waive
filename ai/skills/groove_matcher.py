"""
Groove-Matcher AI Skill — Generates MIDI drums that match organic timing.

Analyzes onset timing and micro-rhythmic patterns in a recording,
then generates a MIDI drum pattern that follows the same groove feel.
"""

import json
import struct
import sys
import tempfile
from pathlib import Path

import librosa
import numpy as np
from midiutil import MIDIFile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from waive_client import WaiveClient


# General MIDI drum map (channel 10)
GM_DRUM_MAP = {
    "kick": 36,
    "snare": 38,
    "closed_hihat": 42,
    "open_hihat": 46,
    "ride": 51,
    "crash": 49,
    "tom_high": 50,
    "tom_mid": 47,
    "tom_low": 45,
}

# Common pattern templates (hits per beat position)
PATTERNS = {
    "basic_rock": {
        "kick":         [1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0],
        "snare":        [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0],
        "closed_hihat": [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0],
    },
    "four_on_floor": {
        "kick":         [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0],
        "snare":        [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0],
        "closed_hihat": [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0],
    },
    "shuffle": {
        "kick":         [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        "snare":        [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0],
        "closed_hihat": [1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1],
    },
    "halftime": {
        "kick":         [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        "snare":        [0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0],
        "closed_hihat": [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0],
    },
}

DEFAULT_VELOCITY = 100


class GrooveMatcher:
    """Analyzes organic timing and generates matching MIDI drum patterns."""

    def __init__(self, client: WaiveClient, output_dir: str | None = None):
        self.client = client
        self.output_dir = output_dir or tempfile.mkdtemp(prefix="waive_groove_")

    def run(self, input_file: str, pattern: str = "basic_rock",
            num_bars: int = 4, start_time: float = 0.0) -> dict:
        """
        Analyze timing of input_file and generate a groove-matched drum pattern.

        Args:
            input_file: Path to the audio recording to analyze.
            pattern: Name of the drum pattern template to use.
            num_bars: Number of bars to generate.
            start_time: Where to place the MIDI clip on the timeline.

        Returns:
            dict with status, BPM, groove analysis, and track info.
        """
        input_path = Path(input_file).resolve()
        if not input_path.exists():
            raise FileNotFoundError(f"Input file not found: {input_file}")

        if pattern not in PATTERNS:
            raise ValueError(
                f"Unknown pattern '{pattern}'. Available: {list(PATTERNS.keys())}"
            )

        # Step 1: Analyze the recording's timing
        groove = self._analyze_groove(input_path)

        # Step 2: Generate the MIDI pattern with humanized timing
        midi_path = self._generate_midi(groove, pattern, num_bars)

        # Step 3: Insert into the engine
        track_result = self._insert_pattern(midi_path, start_time)

        return {
            "status": "ok",
            "input_file": str(input_path),
            "bpm": groove["bpm"],
            "swing_ratio": groove["swing_ratio"],
            "pattern": pattern,
            "num_bars": num_bars,
            "midi_file": str(midi_path),
            "track": track_result,
        }

    def _analyze_groove(self, audio_path: Path) -> dict:
        """Extract timing information from an organic recording."""
        y, sr = librosa.load(str(audio_path), sr=22050, mono=True)

        # Estimate tempo and beat positions
        tempo, beat_frames = librosa.beat.beat_track(y=y, sr=sr)
        beat_times = librosa.frames_to_time(beat_frames, sr=sr)

        # Get onset strength envelope for micro-timing analysis
        onset_env = librosa.onset.onset_strength(y=y, sr=sr)
        onset_frames = librosa.onset.onset_detect(
            y=y, sr=sr, onset_envelope=onset_env, backtrack=False
        )
        onset_times = librosa.frames_to_time(onset_frames, sr=sr)

        # Calculate inter-onset intervals for swing analysis
        if len(beat_times) >= 4:
            iois = np.diff(beat_times)
            # Swing ratio: compare even vs odd subdivisions
            # In a swung groove, the ratio of long-to-short notes deviates from 1.0
            if len(iois) >= 2:
                even_iois = iois[0::2]
                odd_iois = iois[1::2]
                min_len = min(len(even_iois), len(odd_iois))
                if min_len > 0:
                    swing_ratio = float(np.mean(even_iois[:min_len]) /
                                       np.mean(odd_iois[:min_len]))
                else:
                    swing_ratio = 1.0
            else:
                swing_ratio = 1.0
        else:
            swing_ratio = 1.0

        # Calculate timing deviations from the grid (humanization profile)
        bpm = float(np.atleast_1d(tempo)[0])
        beat_duration = 60.0 / bpm
        sixteenth = beat_duration / 4.0

        # Map onsets to nearest grid position and measure deviation
        deviations = []
        for onset in onset_times:
            grid_pos = round(onset / sixteenth) * sixteenth
            deviation = onset - grid_pos
            deviations.append(deviation)

        timing_std = float(np.std(deviations)) if deviations else 0.0

        return {
            "bpm": bpm,
            "beat_times": beat_times.tolist(),
            "onset_times": onset_times.tolist(),
            "swing_ratio": round(swing_ratio, 3),
            "timing_std": timing_std,
            "sixteenth_duration": sixteenth,
        }

    def _generate_midi(self, groove: dict, pattern_name: str,
                       num_bars: int) -> Path:
        """Generate a MIDI file with groove-matched timing."""
        bpm = groove["bpm"]
        swing_ratio = groove["swing_ratio"]
        timing_std = groove["timing_std"]
        pattern = PATTERNS[pattern_name]

        midi = MIDIFile(1)  # single track
        track = 0
        channel = 9  # GM drum channel (0-indexed)
        midi.addTempo(track, 0, bpm)

        steps_per_bar = 16  # 16th note grid
        beat_duration = 60.0 / bpm
        sixteenth = beat_duration / 4.0

        for bar in range(num_bars):
            for step in range(steps_per_bar):
                # Calculate base time for this step
                base_time_beats = (bar * 4) + (step / 4.0)

                # Apply swing: push even 16th notes slightly later
                if step % 2 == 1:  # off-beat 16ths
                    swing_offset = (swing_ratio - 1.0) * sixteenth * 0.5
                    # Convert to beats
                    swing_offset_beats = swing_offset / beat_duration
                else:
                    swing_offset_beats = 0.0

                # Apply human-like timing deviation
                if timing_std > 0:
                    deviation = np.random.normal(0, timing_std)
                    deviation = np.clip(deviation, -sixteenth * 0.4, sixteenth * 0.4)
                    deviation_beats = deviation / beat_duration
                else:
                    deviation_beats = 0.0

                time = base_time_beats + swing_offset_beats + deviation_beats
                time = max(0, time)  # clamp to positive

                # Add notes for each drum that hits on this step
                for drum_name, hits in pattern.items():
                    pattern_step = step % len(hits)
                    if hits[pattern_step]:
                        note = GM_DRUM_MAP[drum_name]
                        # Velocity humanization: slight random variation
                        velocity = int(np.clip(
                            DEFAULT_VELOCITY + np.random.randint(-15, 10),
                            40, 127
                        ))
                        duration = 0.25  # 16th note
                        midi.addNote(track, channel, note, time, duration, velocity)

        # Write MIDI file
        output_path = Path(self.output_dir) / f"groove_{pattern_name}_{num_bars}bars.mid"
        with open(output_path, "wb") as f:
            midi.writeFile(f)

        return output_path

    def _insert_pattern(self, midi_path: Path, start_time: float) -> dict:
        """Create a track in the engine and insert the MIDI drum pattern."""
        add_result = self.client.add_track()
        if add_result.get("status") != "ok":
            raise RuntimeError(f"Failed to add track: {add_result}")

        track_index = add_result["track_index"]

        clip_result = self.client.insert_midi_clip(
            track_id=track_index,
            file_path=str(midi_path),
            start_time=start_time,
        )
        if clip_result.get("status") != "ok":
            raise RuntimeError(f"Failed to insert MIDI clip: {clip_result}")

        return {
            "track_index": track_index,
            "clip_name": clip_result.get("clip_name", ""),
            "duration": clip_result.get("duration", 0),
        }


def main():
    """CLI entry point for groove matching."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Analyze groove and generate matching drum pattern"
    )
    parser.add_argument("input_file", help="Audio file to analyze for groove")
    parser.add_argument("--pattern", default="basic_rock",
                        choices=list(PATTERNS.keys()),
                        help="Drum pattern template")
    parser.add_argument("--bars", type=int, default=4, help="Number of bars")
    parser.add_argument("--host", default="127.0.0.1", help="Engine host")
    parser.add_argument("--port", type=int, default=9090, help="Engine port")
    parser.add_argument("--output-dir", help="Directory for MIDI output")
    parser.add_argument("--start-time", type=float, default=0.0,
                        help="Timeline position (seconds)")
    args = parser.parse_args()

    client = WaiveClient(host=args.host, port=args.port)
    client.connect()

    try:
        matcher = GrooveMatcher(client=client, output_dir=args.output_dir)
        result = matcher.run(
            args.input_file,
            pattern=args.pattern,
            num_bars=args.bars,
            start_time=args.start_time,
        )
        print(json.dumps(result, indent=2))
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
