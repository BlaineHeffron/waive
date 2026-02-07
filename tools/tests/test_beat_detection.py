"""Validate beat_detection tool against synthetic click tracks."""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_click_track, setup_tool_dirs, read_result
)

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "beat_detection", "__main__.py")

# Try to import mir_eval for standard metrics; skip advanced tests if unavailable
try:
    import mir_eval
    HAS_MIR_EVAL = True
except ImportError:
    HAS_MIR_EVAL = False


def run_beat_detection(tmp_path, samples, params=None):
    """Helper: run the beat detection tool and return parsed result."""
    import json
    if params is None:
        params = {"method": "onset"}
    input_dir, output_dir, input_wav = setup_tool_dirs(tmp_path, "beat_det", params, samples)

    # Tools use stdin JSON: {"input_file": "...", "output_dir": "...", "params": {...}}
    request = {"input_file": input_wav, "output_dir": output_dir, "params": params}
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT],
        input=json.dumps(request),
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(result.stdout)


class TestBeatDetectionBPM:
    """Test BPM estimation accuracy against click tracks at known tempos."""

    @pytest.mark.parametrize("bpm", [80, 100, 120, 140, 160])
    def test_bpm_estimation(self, tmp_path, bpm):
        """BPM estimate should be within 5% of ground truth."""
        samples, _ = generate_click_track(bpm, duration_sec=10.0)
        result = run_beat_detection(tmp_path, samples)

        assert result["success"] is True
        estimated_bpm = result["bpm"]

        # Allow octave errors (double/half tempo) — common in beat tracking
        error = min(
            abs(estimated_bpm - bpm),
            abs(estimated_bpm - bpm * 2),
            abs(estimated_bpm - bpm / 2),
        )
        tolerance = bpm * 0.05  # 5% tolerance
        assert error < tolerance, (
            f"BPM estimation too far off: expected ~{bpm}, got {estimated_bpm}"
        )

    def test_120bpm_autocorrelation(self, tmp_path):
        """Autocorrelation method should also detect 120 BPM."""
        samples, _ = generate_click_track(120, duration_sec=10.0)
        result = run_beat_detection(tmp_path, samples, {"method": "autocorrelation"})

        assert result["success"] is True
        estimated_bpm = result["bpm"]
        error = min(abs(estimated_bpm - 120), abs(estimated_bpm - 240), abs(estimated_bpm - 60))
        assert error < 6.0, f"Autocorrelation BPM too far off: {estimated_bpm}"


class TestBeatDetectionPositions:
    """Test beat position accuracy using mir_eval metrics."""

    @pytest.mark.skipif(not HAS_MIR_EVAL, reason="mir_eval not installed")
    @pytest.mark.parametrize("bpm", [100, 120, 140])
    def test_beat_f_measure(self, tmp_path, bpm):
        """Beat positions should achieve F-measure > 0.7 on clean click tracks."""
        samples, reference_beats = generate_click_track(bpm, duration_sec=10.0)
        result = run_beat_detection(tmp_path, samples)

        assert result["success"] is True
        estimated_beats = np.array(result["beats"])

        if len(estimated_beats) < 2:
            pytest.skip("Too few beats detected to evaluate")

        scores = mir_eval.beat.evaluate(reference_beats, estimated_beats)
        f_measure = scores["F-measure"]
        assert f_measure > 0.7, (
            f"Beat F-measure too low: {f_measure:.3f} (CMLt: {scores['CMLt']:.3f})"
        )

    def test_beat_count_reasonable(self, tmp_path):
        """Number of detected beats should be roughly correct."""
        bpm = 120
        duration = 10.0
        expected_beats = int(duration * bpm / 60)
        samples, _ = generate_click_track(bpm, duration)
        result = run_beat_detection(tmp_path, samples)

        assert result["success"] is True
        actual_count = result["beat_count"]
        # Allow 30% deviation
        assert abs(actual_count - expected_beats) < expected_beats * 0.3, (
            f"Beat count off: expected ~{expected_beats}, got {actual_count}"
        )


class TestBeatDetectionEdgeCases:
    """Edge cases and error handling."""

    def test_silent_audio(self, tmp_path):
        """Should handle silence gracefully (no crash, returns result)."""
        samples = np.zeros(SR * 5, dtype=np.float32)
        result = run_beat_detection(tmp_path, samples)
        assert result["success"] is True  # Should succeed, just with few/no beats

    def test_very_short_audio(self, tmp_path):
        """Should handle very short audio (< 1 second)."""
        samples, _ = generate_click_track(120, duration_sec=0.5)
        result = run_beat_detection(tmp_path, samples)
        assert result["success"] is True
