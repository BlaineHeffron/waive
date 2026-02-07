"""Validate chord_detection tool against synthetic chord sequences."""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_chord_sequence, setup_tool_dirs, read_result
)

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "chord_detection", "__main__.py")


def run_chord_detection(tmp_path, samples, hop_seconds=0.5):
    import json
    params = {"hop_seconds": hop_seconds}
    input_dir, output_dir, input_wav = setup_tool_dirs(tmp_path, "chord_det", params, samples)
    request = {"input_file": input_wav, "output_dir": output_dir, "params": params}
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT],
        input=json.dumps(request),
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(result.stdout)


class TestChordDetection:
    """Test chord detection on synthetic chord progressions."""

    def test_single_major_chord(self, tmp_path):
        """Should detect a single sustained major chord."""
        chords = [(261.63, "major", "C")]
        samples, annotations = generate_chord_sequence(chords, segment_duration=3.0)
        result = run_chord_detection(tmp_path, samples)

        assert result["success"] is True
        assert result["chord_count"] >= 1
        # The first detected chord should be C or C-related
        first_chord = result["chords"][0]["chord"]
        assert "C" in first_chord, f"Expected C-based chord, got '{first_chord}'"

    def test_chord_progression(self, tmp_path):
        """Should detect chord changes in a I-vi-IV-V progression (C-Am-F-G)."""
        chords = [
            (261.63, "major", "C"),
            (220.00, "minor", "Am"),
            (349.23, "major", "F"),
            (392.00, "major", "G"),
        ]
        samples, annotations = generate_chord_sequence(chords, segment_duration=2.0)
        result = run_chord_detection(tmp_path, samples)

        assert result["success"] is True
        # Should detect at least some chord changes
        assert result["chord_count"] >= 2, (
            f"Should detect chord changes, only got {result['chord_count']} chords"
        )

    def test_chord_count_reasonable(self, tmp_path):
        """Number of detected chords should be in a reasonable range."""
        chords = [
            (261.63, "major", "C"),
            (392.00, "major", "G"),
            (220.00, "minor", "Am"),
        ]
        samples, annotations = generate_chord_sequence(chords, segment_duration=2.0)
        result = run_chord_detection(tmp_path, samples, hop_seconds=0.5)

        assert result["success"] is True
        # With 3 distinct chords over 6 seconds, should detect 2-20 chord entries
        assert 2 <= result["chord_count"] <= 20

    def test_confidence_scores(self, tmp_path):
        """All chord detections should have valid confidence scores."""
        chords = [(261.63, "major", "C")]
        samples, _ = generate_chord_sequence(chords, segment_duration=3.0)
        result = run_chord_detection(tmp_path, samples)

        assert result["success"] is True
        for chord_entry in result["chords"]:
            assert "confidence" in chord_entry
            assert chord_entry["confidence"] >= 0.0

    def test_timestamps_monotonic(self, tmp_path):
        """Chord timestamps should be monotonically increasing."""
        chords = [
            (261.63, "major", "C"),
            (392.00, "major", "G"),
        ]
        samples, _ = generate_chord_sequence(chords, segment_duration=3.0)
        result = run_chord_detection(tmp_path, samples)

        assert result["success"] is True
        times = [c["time"] for c in result["chords"]]
        for i in range(1, len(times)):
            assert times[i] >= times[i - 1], "Timestamps not monotonic"

    def test_silent_audio(self, tmp_path):
        """Should handle silence gracefully."""
        samples = np.zeros(SR * 3, dtype=np.float32)
        result = run_chord_detection(tmp_path, samples)
        assert result["success"] is True
