"""
Stem-Splitter AI Skill — Separates audio into stems using Demucs.

Runs Demucs (htdemucs model) on an input audio file to produce up to 4 stems:
vocals, drums, bass, other. Then creates new tracks in the Waive engine
and inserts the separated audio clips.
"""

import os
import subprocess
import tempfile
from pathlib import Path

# Import the client from the parent package
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from waive_client import WaiveClient


STEM_NAMES = ["vocals", "drums", "bass", "other"]

# Default Demucs model — htdemucs is the fastest 4-stem model
DEFAULT_MODEL = "htdemucs"


class StemSplitter:
    """Splits an audio file into stems and loads them into the Waive engine."""

    def __init__(self, client: WaiveClient, model: str = DEFAULT_MODEL,
                 output_dir: str | None = None):
        self.client = client
        self.model = model
        self.output_dir = output_dir or tempfile.mkdtemp(prefix="waive_stems_")

    def run(self, input_file: str, start_time: float = 0.0) -> dict:
        """
        Split input_file into stems and insert them into the engine.

        Args:
            input_file: Path to the audio file to split.
            start_time: Where to place the stems on the timeline (seconds).

        Returns:
            dict with status, stem file paths, and track indices.
        """
        input_path = Path(input_file).resolve()
        if not input_path.exists():
            raise FileNotFoundError(f"Input file not found: {input_file}")

        # Step 1: Run Demucs to separate stems
        stem_paths = self._run_demucs(input_path)

        # Step 2: Create tracks and insert clips in the engine
        track_info = self._insert_stems(stem_paths, start_time)

        return {
            "status": "ok",
            "input_file": str(input_path),
            "stems": track_info,
        }

    def _run_demucs(self, input_path: Path) -> dict[str, Path]:
        """Run Demucs separation and return paths to output stems."""
        cmd = [
            sys.executable, "-m", "demucs",
            "--name", self.model,
            "--out", self.output_dir,
            str(input_path),
        ]

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
        )

        if result.returncode != 0:
            raise RuntimeError(
                f"Demucs failed (exit {result.returncode}):\n{result.stderr}"
            )

        # Demucs outputs to: output_dir/model_name/track_name/{stem}.wav
        stem_dir = Path(self.output_dir) / self.model / input_path.stem
        if not stem_dir.exists():
            raise FileNotFoundError(
                f"Expected Demucs output at {stem_dir} but directory not found. "
                f"Demucs stdout: {result.stdout}"
            )

        stem_paths = {}
        for stem_name in STEM_NAMES:
            stem_file = stem_dir / f"{stem_name}.wav"
            if stem_file.exists():
                stem_paths[stem_name] = stem_file

        if not stem_paths:
            raise FileNotFoundError(
                f"No stem files found in {stem_dir}. "
                f"Expected: {', '.join(STEM_NAMES)}"
            )

        return stem_paths

    def _insert_stems(self, stem_paths: dict[str, Path],
                      start_time: float) -> list[dict]:
        """Create tracks in the engine and insert stem audio clips."""
        track_info = []

        for stem_name, stem_path in stem_paths.items():
            # Create a new track
            add_result = self.client.add_track()
            if add_result.get("status") != "ok":
                raise RuntimeError(f"Failed to add track: {add_result}")

            track_index = add_result["track_index"]

            # Insert the stem audio clip
            clip_result = self.client.insert_audio_clip(
                track_id=track_index,
                file_path=str(stem_path),
                start_time=start_time,
            )
            if clip_result.get("status") != "ok":
                raise RuntimeError(
                    f"Failed to insert clip for {stem_name}: {clip_result}"
                )

            track_info.append({
                "stem": stem_name,
                "track_index": track_index,
                "file_path": str(stem_path),
                "duration": clip_result.get("duration", 0),
            })

        return track_info


def main():
    """CLI entry point for stem splitting."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Split audio into stems and load into Waive engine"
    )
    parser.add_argument("input_file", help="Path to audio file to split")
    parser.add_argument("--host", default="127.0.0.1", help="Engine host")
    parser.add_argument("--port", type=int, default=9090, help="Engine port")
    parser.add_argument("--model", default=DEFAULT_MODEL, help="Demucs model name")
    parser.add_argument("--output-dir", help="Directory for stem output files")
    parser.add_argument("--start-time", type=float, default=0.0,
                        help="Timeline position for stem placement (seconds)")
    args = parser.parse_args()

    client = WaiveClient(host=args.host, port=args.port)
    client.connect()

    try:
        splitter = StemSplitter(
            client=client,
            model=args.model,
            output_dir=args.output_dir,
        )
        result = splitter.run(args.input_file, start_time=args.start_time)

        import json
        print(json.dumps(result, indent=2))
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
