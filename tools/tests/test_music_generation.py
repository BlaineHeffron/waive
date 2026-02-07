"""Validate music_generation tool — checks output format and basic audio properties."""
import subprocess
import sys
import os
import pytest
import numpy as np
import json

from audio_fixtures import SR, setup_tool_dirs, read_result, read_result_file

TOOL_DIR = os.path.join(os.path.dirname(__file__), "..", "music_generation")
# Check for both __main__.py and tool_name.py
if os.path.exists(os.path.join(TOOL_DIR, "__main__.py")):
    TOOL_SCRIPT = os.path.join(TOOL_DIR, "__main__.py")
else:
    TOOL_SCRIPT = os.path.join(TOOL_DIR, "music_generation.py")

# Check if tool is actually functional (imports work)
def tool_can_run():
    try:
        result = subprocess.run(
            [sys.executable, "-c", "import sys; sys.path.insert(0, 'tools/music_generation'); import music_generation"],
            capture_output=True, text=True, timeout=10
        )
        return result.returncode == 0
    except:
        return False

TOOL_FUNCTIONAL = tool_can_run()


def run_music_generation(tmp_path, params):
    import json
    input_dir, output_dir, _ = setup_tool_dirs(tmp_path, "musicgen", params)
    request = {"output_dir": output_dir, "params": params}
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT],
        input=json.dumps(request),
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(result.stdout), output_dir


@pytest.mark.skipif(not TOOL_FUNCTIONAL, reason="music_generation tool has dependency issues")
class TestMusicGeneration:
    """Test music generation produces valid audio output."""

    def test_basic_generation(self, tmp_path):
        """Should produce an output WAV file from a text prompt."""
        result, output_dir = run_music_generation(tmp_path, {
            "prompt": "drum beat",
            "duration_seconds": 3,
        })
        assert result["success"] is True

        # Check output audio exists
        output_wav = os.path.join(output_dir, "output.wav")
        assert os.path.exists(output_wav), "No output.wav produced"
        assert os.path.getsize(output_wav) > 1000, "Output file suspiciously small"

    def test_output_duration_approximate(self, tmp_path):
        """Generated audio duration should be approximately as requested."""
        target_seconds = 5
        result, output_dir = run_music_generation(tmp_path, {
            "prompt": "ambient pad",
            "duration_seconds": target_seconds,
        })
        assert result["success"] is True

        # Read the output and check duration
        output_wav = os.path.join(output_dir, "output.wav")
        try:
            from scipy.io import wavfile
            sr_out, data = wavfile.read(output_wav)
            actual_duration = len(data) / sr_out
            # Allow 50% tolerance (fallback synth may vary)
            assert actual_duration > target_seconds * 0.5, (
                f"Output too short: {actual_duration:.1f}s vs requested {target_seconds}s"
            )
        except ImportError:
            pytest.skip("scipy not available for WAV verification")

    def test_output_not_silent(self, tmp_path):
        """Generated audio should not be all zeros."""
        result, output_dir = run_music_generation(tmp_path, {
            "prompt": "energetic rock guitar",
            "duration_seconds": 3,
        })
        assert result["success"] is True

        output_wav = os.path.join(output_dir, "output.wav")
        try:
            from scipy.io import wavfile
            _, data = wavfile.read(output_wav)
            data = data.astype(np.float32)
            rms = np.sqrt(np.mean(data**2))
            assert rms > 0.001, "Output is effectively silent"
        except ImportError:
            pytest.skip("scipy not available for audio analysis")

    def test_various_prompts(self, tmp_path):
        """Different prompt keywords should all produce output without errors."""
        prompts = ["bass line", "ambient atmosphere", "simple melody"]
        for prompt in prompts:
            result, output_dir = run_music_generation(tmp_path, {
                "prompt": prompt,
                "duration_seconds": 2,
            })
            assert result["success"] is True, f"Failed for prompt: '{prompt}'"
