"""
Temporal Arranger AI Skill — Song structure analysis and rearrangement.

Detects song sections (verse, chorus, bridge, etc.) using beat tracking
and spectral analysis. Supports prompt-driven rearrangement operations.
"""

import json
import re
import sys
import tempfile
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Optional

import librosa
import numpy as np
from scipy import signal as scipy_signal

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from waive_client import WaiveClient


@dataclass
class Section:
    """A detected song section."""
    label: str           # verse, chorus, bridge, intro, outro
    start_time: float    # seconds
    end_time: float      # seconds
    confidence: float    # 0.0-1.0
    energy: float        # relative energy level

    @property
    def duration(self) -> float:
        return self.end_time - self.start_time


@dataclass
class Arrangement:
    """Complete song structure analysis."""
    bpm: float
    total_duration: float
    sections: list[Section]
    beat_times: list[float]


class TemporalArranger:
    """Analyzes song structure and performs arrangement operations."""

    def __init__(self, client: WaiveClient, output_dir: str | None = None):
        self.client = client
        self.output_dir = output_dir or tempfile.mkdtemp(prefix="waive_arrange_")

    def analyze(self, input_file: str) -> Arrangement:
        """
        Analyze the structure of an audio file.

        Returns an Arrangement with detected sections.
        """
        input_path = Path(input_file).resolve()
        if not input_path.exists():
            raise FileNotFoundError(f"Input file not found: {input_file}")

        y, sr = librosa.load(str(input_path), sr=22050, mono=True)
        duration = librosa.get_duration(y=y, sr=sr)

        # Beat tracking
        tempo, beat_frames = librosa.beat.beat_track(y=y, sr=sr)
        bpm = float(np.atleast_1d(tempo)[0])
        beat_times = librosa.frames_to_time(beat_frames, sr=sr).tolist()

        # Section detection
        sections = self._detect_sections(y, sr, bpm, beat_times, duration)

        return Arrangement(
            bpm=bpm,
            total_duration=duration,
            sections=sections,
            beat_times=beat_times,
        )

    def rearrange(self, input_file: str, prompt: str,
                  start_time: float = 0.0) -> dict:
        """
        Rearrange an audio file based on a text prompt.

        Args:
            input_file: Path to the audio file.
            prompt: Natural language instruction, e.g.:
                    "double the chorus", "remove the bridge",
                    "swap verse 1 and verse 2", "extend the intro by 8 bars"
            start_time: Where to place the result on the timeline.

        Returns:
            dict with status, new arrangement, and track info.
        """
        # Analyze the song structure
        arrangement = self.analyze(input_file)

        # Parse the prompt into an operation
        operation = self._parse_prompt(prompt, arrangement)

        # Execute the rearrangement (render new audio segments)
        new_sections = self._execute_operation(
            input_file, arrangement, operation
        )

        # Insert the rearranged clips into the engine
        track_info = self._insert_arrangement(new_sections, start_time, input_file)

        return {
            "status": "ok",
            "input_file": str(Path(input_file).resolve()),
            "original_arrangement": {
                "bpm": arrangement.bpm,
                "sections": [asdict(s) for s in arrangement.sections],
            },
            "operation": operation,
            "new_arrangement": [asdict(s) for s in new_sections],
            "track": track_info,
        }

    def _detect_sections(self, y: np.ndarray, sr: int, bpm: float,
                         beat_times: list[float],
                         duration: float) -> list[Section]:
        """Detect song sections using self-similarity and energy analysis."""
        # Compute chroma features for harmonic analysis
        chroma = librosa.feature.chroma_cqt(y=y, sr=sr)

        # Compute MFCCs for timbral similarity
        mfcc = librosa.feature.mfcc(y=y, sr=sr, n_mfcc=13)

        # Compute RMS energy
        rms = librosa.feature.rms(y=y)[0]

        # Build recurrence/self-similarity matrix
        # Combine chroma and MFCC features
        features = np.vstack([
            librosa.util.normalize(chroma, axis=1),
            librosa.util.normalize(mfcc, axis=1),
        ])

        # Self-similarity using cosine distance
        sim = librosa.segment.recurrence_matrix(
            features, mode="affinity", sym=True
        )

        # Detect structural boundaries using novelty curve
        # Compute novelty as the difference along the checkerboard kernel diagonal
        # librosa removed novelty() function in recent versions
        novelty = np.sqrt(np.sum(np.diff(sim, axis=1) ** 2, axis=1))

        # Find peaks in the novelty curve (section boundaries)
        hop_length = 512  # default librosa hop
        frame_times = librosa.frames_to_time(
            np.arange(len(novelty)), sr=sr, hop_length=hop_length
        )

        # Adaptive peak detection
        if len(novelty) > 0:
            # Normalize novelty
            novelty_norm = novelty / (np.max(novelty) + 1e-10)
            threshold = np.mean(novelty_norm) + 0.5 * np.std(novelty_norm)

            peaks, properties = scipy_signal.find_peaks(
                novelty_norm,
                height=threshold,
                distance=int(sr * 4 / hop_length),  # min 4 seconds between sections
            )
            boundary_times = frame_times[peaks].tolist()
        else:
            boundary_times = []

        # Add start and end
        boundaries = [0.0] + sorted(boundary_times) + [duration]

        # Snap boundaries to nearest beat
        if beat_times:
            snapped = [0.0]
            for b in boundaries[1:-1]:
                nearest = min(beat_times, key=lambda bt: abs(bt - b))
                if nearest not in snapped:
                    snapped.append(nearest)
            snapped.append(duration)
            boundaries = sorted(set(snapped))

        # Label sections based on energy and repetition patterns
        sections = []
        energies = []

        for i in range(len(boundaries) - 1):
            start = boundaries[i]
            end = boundaries[i + 1]

            # Calculate energy for this segment
            start_frame = librosa.time_to_frames(start, sr=sr)
            end_frame = librosa.time_to_frames(end, sr=sr)
            end_frame = min(end_frame, len(rms) - 1)

            if start_frame < end_frame:
                segment_energy = float(np.mean(rms[start_frame:end_frame]))
            else:
                segment_energy = 0.0

            energies.append(segment_energy)

        # Classify sections by relative energy
        if energies:
            energy_arr = np.array(energies)
            energy_median = np.median(energy_arr)
            energy_std = np.std(energy_arr)

            for i, (start, end) in enumerate(
                zip(boundaries[:-1], boundaries[1:])
            ):
                e = energies[i]
                seg_duration = end - start

                # Heuristic labeling
                if i == 0 and seg_duration < 15:
                    label = "intro"
                elif i == len(energies) - 1 and seg_duration < 15:
                    label = "outro"
                elif e > energy_median + 0.5 * energy_std:
                    label = "chorus"
                elif e < energy_median - 0.3 * energy_std:
                    label = "bridge"
                else:
                    label = "verse"

                # Number duplicate labels
                existing_labels = [s.label for s in sections]
                base_label = label
                count = sum(1 for l in existing_labels if l.startswith(base_label))
                if count > 0:
                    label = f"{base_label}_{count + 1}"
                    # Rename the first one too
                    for s in sections:
                        if s.label == base_label:
                            s.label = f"{base_label}_1"

                confidence = min(1.0, 0.5 + 0.5 * abs(e - energy_median) /
                                (energy_std + 1e-10))

                sections.append(Section(
                    label=label,
                    start_time=round(start, 3),
                    end_time=round(end, 3),
                    confidence=round(confidence, 2),
                    energy=round(float(e), 4),
                ))

        return sections

    def _parse_prompt(self, prompt: str,
                      arrangement: Arrangement) -> dict:
        """Parse a natural language prompt into a structured operation."""
        prompt_lower = prompt.lower().strip()
        sections = arrangement.sections
        section_labels = [s.label for s in sections]

        # Match "double the X" or "repeat the X"
        match = re.match(
            r"(double|repeat|duplicate)\s+(?:the\s+)?(\w+(?:\s*\d*)?)",
            prompt_lower
        )
        if match:
            target = self._find_section(match.group(2), sections)
            return {
                "type": "duplicate",
                "target_section": target.label if target else match.group(2),
                "times": 2,
            }

        # Match "remove the X" or "delete the X"
        match = re.match(
            r"(remove|delete|cut)\s+(?:the\s+)?(\w+(?:\s*\d*)?)",
            prompt_lower
        )
        if match:
            target = self._find_section(match.group(2), sections)
            return {
                "type": "remove",
                "target_section": target.label if target else match.group(2),
            }

        # Match "swap X and Y"
        match = re.match(
            r"swap\s+(?:the\s+)?(\w+(?:\s*\d*)?)\s+and\s+(?:the\s+)?(\w+(?:\s*\d*)?)",
            prompt_lower
        )
        if match:
            a = self._find_section(match.group(1), sections)
            b = self._find_section(match.group(2), sections)
            return {
                "type": "swap",
                "section_a": a.label if a else match.group(1),
                "section_b": b.label if b else match.group(2),
            }

        # Match "extend X by N bars"
        match = re.match(
            r"extend\s+(?:the\s+)?(\w+(?:\s*\d*)?)\s+by\s+(\d+)\s+bars?",
            prompt_lower
        )
        if match:
            target = self._find_section(match.group(1), sections)
            return {
                "type": "extend",
                "target_section": target.label if target else match.group(1),
                "bars": int(match.group(2)),
            }

        return {"type": "unknown", "raw_prompt": prompt}

    def _find_section(self, name: str,
                      sections: list[Section]) -> Optional[Section]:
        """Find a section by fuzzy name match."""
        name = name.strip().lower()
        # Exact match
        for s in sections:
            if s.label.lower() == name:
                return s
        # Partial match
        for s in sections:
            if name in s.label.lower() or s.label.lower() in name:
                return s
        return None

    def _execute_operation(self, input_file: str,
                           arrangement: Arrangement,
                           operation: dict) -> list[Section]:
        """Execute the rearrangement and return new section list."""
        sections = list(arrangement.sections)
        op_type = operation.get("type")

        if op_type == "duplicate":
            target_label = operation["target_section"]
            target = self._find_section(target_label, sections)
            if target is None:
                raise ValueError(f"Section not found: {target_label}")

            # Insert a copy of the section right after it
            idx = sections.index(target)
            dup = Section(
                label=f"{target.label}_copy",
                start_time=target.start_time,
                end_time=target.end_time,
                confidence=target.confidence,
                energy=target.energy,
            )
            sections.insert(idx + 1, dup)

        elif op_type == "remove":
            target_label = operation["target_section"]
            target = self._find_section(target_label, sections)
            if target is None:
                raise ValueError(f"Section not found: {target_label}")
            sections.remove(target)

        elif op_type == "swap":
            a = self._find_section(operation["section_a"], sections)
            b = self._find_section(operation["section_b"], sections)
            if a is None or b is None:
                raise ValueError("One or both sections not found for swap.")
            idx_a = sections.index(a)
            idx_b = sections.index(b)
            sections[idx_a], sections[idx_b] = sections[idx_b], sections[idx_a]

        elif op_type == "extend":
            target_label = operation["target_section"]
            target = self._find_section(target_label, sections)
            if target is None:
                raise ValueError(f"Section not found: {target_label}")
            bars = operation.get("bars", 4)
            bar_duration = (60.0 / arrangement.bpm) * 4
            extension = bars * bar_duration
            idx = sections.index(target)
            sections[idx] = Section(
                label=target.label,
                start_time=target.start_time,
                end_time=target.end_time + extension,
                confidence=target.confidence,
                energy=target.energy,
            )

        # Recalculate timeline positions for the new arrangement
        return self._recalculate_timeline(sections)

    def _recalculate_timeline(self, sections: list[Section]) -> list[Section]:
        """Recalculate start/end times for a rearranged section list."""
        result = []
        current_time = 0.0
        for section in sections:
            duration = section.duration
            result.append(Section(
                label=section.label,
                start_time=round(current_time, 3),
                end_time=round(current_time + duration, 3),
                confidence=section.confidence,
                energy=section.energy,
            ))
            current_time += duration
        return result

    def _insert_arrangement(self, sections: list[Section],
                            start_time: float, source_file: str) -> dict:
        """
        Insert the rearranged sections into the engine.

        Renders each section from the source audio and inserts clips at the
        correct timeline positions.
        """
        import soundfile as sf

        add_result = self.client.add_track()
        if add_result.get("status") != "ok":
            raise RuntimeError(f"Failed to add track: {add_result}")

        track_index = add_result["track_index"]

        # Load source audio
        y, sr = librosa.load(source_file, sr=None, mono=False)

        # Handle mono/stereo
        if y.ndim == 1:
            y = y.reshape(1, -1)  # (channels, samples)

        inserted_clips = []
        timeline_offset = start_time
        total_samples = y.shape[1]

        # Crossfade length: 10ms to prevent clicks at section boundaries
        fade_samples = min(int(sr * 0.01), 512)

        for section in sections:
            # Convert section times to sample indices in original audio
            start_sample = int(section.start_time * sr)
            end_sample = int(section.end_time * sr)

            # Clamp to valid range
            start_sample = max(0, min(start_sample, total_samples))
            end_sample = max(start_sample, min(end_sample, total_samples))

            if start_sample >= end_sample:
                continue

            # Slice audio for this section
            section_audio = y[:, start_sample:end_sample].copy()

            # Apply fade-in/fade-out to prevent clicks at boundaries
            if fade_samples > 0 and section_audio.shape[1] > fade_samples * 2:
                fade_in = np.linspace(0.0, 1.0, fade_samples)
                fade_out = np.linspace(1.0, 0.0, fade_samples)
                section_audio[:, :fade_samples] *= fade_in
                section_audio[:, -fade_samples:] *= fade_out

            # Save section to temp WAV file
            temp_file = Path(self.output_dir) / f"section_{section.label}_{id(section)}.wav"
            sf.write(str(temp_file), section_audio.T, sr)  # transpose for soundfile (samples, channels)

            # Insert clip at timeline position
            insert_result = self.client.insert_audio_clip(
                track_id=track_index,
                file_path=str(temp_file),
                start_time=timeline_offset
            )

            if insert_result.get("status") == "ok":
                inserted_clips.append({
                    "label": section.label,
                    "file": str(temp_file),
                    "timeline_position": timeline_offset,
                    "duration": section.duration,
                })
                timeline_offset += section.duration

        return {
            "track_index": track_index,
            "sections_inserted": len(inserted_clips),
            "total_duration": timeline_offset - start_time,
            "clips": inserted_clips,
        }


def main():
    """CLI entry point for temporal arrangement."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Analyze and rearrange song structure"
    )
    subparsers = parser.add_subparsers(dest="command")

    # Analyze subcommand
    analyze_parser = subparsers.add_parser("analyze", help="Analyze song structure")
    analyze_parser.add_argument("input_file", help="Audio file to analyze")

    # Rearrange subcommand
    rearrange_parser = subparsers.add_parser("rearrange", help="Rearrange song")
    rearrange_parser.add_argument("input_file", help="Audio file to rearrange")
    rearrange_parser.add_argument("prompt", help="Rearrangement instruction")
    rearrange_parser.add_argument("--start-time", type=float, default=0.0)

    # Common args
    for p in [analyze_parser, rearrange_parser]:
        p.add_argument("--host", default="127.0.0.1")
        p.add_argument("--port", type=int, default=9090)
        p.add_argument("--output-dir")

    args = parser.parse_args()

    if args.command == "analyze":
        client = WaiveClient(host=args.host, port=args.port)
        # Analysis doesn't need engine connection, but we init for consistency
        arranger = TemporalArranger(client=client, output_dir=args.output_dir)
        result = arranger.analyze(args.input_file)
        print(json.dumps({
            "bpm": result.bpm,
            "total_duration": result.total_duration,
            "sections": [asdict(s) for s in result.sections],
        }, indent=2))

    elif args.command == "rearrange":
        client = WaiveClient(host=args.host, port=args.port)
        client.connect()
        try:
            arranger = TemporalArranger(client=client, output_dir=args.output_dir)
            result = arranger.rearrange(
                args.input_file, args.prompt, start_time=args.start_time
            )
            print(json.dumps(result, indent=2, default=str))
        finally:
            client.disconnect()
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
