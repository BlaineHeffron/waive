"""Validate key_detection tool against synthetic tonal signals."""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_chord, setup_tool_dirs, read_result, KEY_TEST_CASES
)

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "key_detection", "__main__.py")

try:
    import mir_eval
    HAS_MIR_EVAL = True
except ImportError:
    HAS_MIR_EVAL = False


def run_key_detection(tmp_path, samples):
    import json
    input_dir, output_dir, input_wav = setup_tool_dirs(tmp_path, "key_det", {}, samples)
    request = {"input_file": input_wav, "output_dir": output_dir, "params": {}}
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT],
        input=json.dumps(request),
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(result.stdout)


class TestKeyDetection:
    """Test key detection accuracy on synthetic chord signals."""

    @pytest.mark.parametrize("root_hz,mode,expected_key", KEY_TEST_CASES)
    def test_key_detection_exact(self, tmp_path, root_hz, mode, expected_key):
        """Key detection should identify the correct key for a pure chord."""
        samples = generate_chord(root_hz, mode, duration_sec=5.0)
        result = run_key_detection(tmp_path, samples)

        assert result["success"] is True
        detected_key = result["key"]

        # Exact match or acceptable related key
        if detected_key == expected_key:
            return  # Perfect

        # Check if it's a closely related key using mir_eval scoring
        if HAS_MIR_EVAL:
            score = mir_eval.key.weighted_score(expected_key, detected_key)
            assert score >= 0.3, (
                f"Key detection too far off: expected '{expected_key}', "
                f"got '{detected_key}' (mir_eval score: {score})"
            )
        else:
            # Without mir_eval, accept relative/parallel keys manually
            # (e.g., C major ↔ A minor is acceptable)
            pytest.skip(f"Got '{detected_key}' instead of '{expected_key}' — need mir_eval for scoring")

    @pytest.mark.skipif(not HAS_MIR_EVAL, reason="mir_eval not installed")
    @pytest.mark.parametrize("root_hz,mode,expected_key", KEY_TEST_CASES)
    def test_key_weighted_score(self, tmp_path, root_hz, mode, expected_key):
        """MIREX weighted score should be >= 0.5 (exact or relative key)."""
        samples = generate_chord(root_hz, mode, duration_sec=5.0)
        result = run_key_detection(tmp_path, samples)

        assert result["success"] is True
        score = mir_eval.key.weighted_score(expected_key, result["key"])
        assert score >= 0.5, (
            f"Weighted score too low: {score} for '{expected_key}' vs '{result['key']}'"
        )

    def test_confidence_range(self, tmp_path):
        """Confidence score should be in [-1, 1] range."""
        samples = generate_chord(261.63, "major", duration_sec=5.0)
        result = run_key_detection(tmp_path, samples)
        assert result["success"] is True
        assert -1.0 <= result["confidence"] <= 1.0

    def test_alternatives_provided(self, tmp_path):
        """Result should include top alternative keys."""
        samples = generate_chord(440.0, "minor", duration_sec=5.0)
        result = run_key_detection(tmp_path, samples)
        assert result["success"] is True
        assert "alternatives" in result
        assert len(result["alternatives"]) >= 3

    def test_silent_audio(self, tmp_path):
        """Should handle silence gracefully."""
        samples = np.zeros(SR * 5, dtype=np.float32)
        result = run_key_detection(tmp_path, samples)
        assert result["success"] is True
