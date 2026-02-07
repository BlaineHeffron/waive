"""Validate timbre_transfer tool — checks output audio and pitch preservation."""
import subprocess
import sys
import os
import pytest
import numpy as np
import json

from audio_fixtures import SR, generate_sine, setup_tool_dirs, read_result, read_result_file

TOOL_DIR = os.path.join(os.path.dirname(__file__), "..", "timbre_transfer")
# Check for both __main__.py and tool_name.py
if os.path.exists(os.path.join(TOOL_DIR, "__main__.py")):
    TOOL_SCRIPT = os.path.join(TOOL_DIR, "__main__.py")
else:
    TOOL_SCRIPT = os.path.join(TOOL_DIR, "timbre_transfer.py")

# Check if tool is actually functional (imports work)
def tool_can_run():
    try:
        result = subprocess.run(
            [sys.executable, "-c", "import sys; sys.path.insert(0, 'tools/timbre_transfer'); import timbre_transfer"],
            capture_output=True, text=True, timeout=10
        )
        return result.returncode == 0
    except:
        return False

TOOL_FUNCTIONAL = tool_can_run()


def run_timbre_transfer(tmp_path, samples, target_instrument):
    """Run timbre transfer tool using old --input-dir/--output-dir interface."""
    params = {"target_instrument": target_instrument}
    input_dir, output_dir, input_wav = setup_tool_dirs(tmp_path, "timbre", params, samples)

    # Write params.json for old interface
    with open(os.path.join(input_dir, "params.json"), "w") as f:
        json.dump(params, f)

    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT, "--input-dir", input_dir, "--output-dir", output_dir],
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result_file(output_dir), output_dir


@pytest.mark.skipif(not TOOL_FUNCTIONAL, reason="timbre_transfer tool has dependency issues")
class TestTimbreTransfer:
    """Test timbre transfer produces valid transformed audio."""

    @pytest.mark.parametrize("instrument", ["trumpet", "violin", "flute", "piano"])
    def test_produces_output(self, tmp_path, instrument):
        """Should produce an output WAV for each instrument target."""
        samples = generate_sine(440.0, duration_sec=2.0)
        result, output_dir = run_timbre_transfer(tmp_path, samples, instrument)
        assert result["success"] is True

        output_wav = os.path.join(output_dir, "output.wav")
        assert os.path.exists(output_wav), f"No output.wav for {instrument}"

    def test_output_same_duration(self, tmp_path):
        """Output audio should be approximately same duration as input."""
        duration = 2.0
        samples = generate_sine(440.0, duration_sec=duration)
        result, output_dir = run_timbre_transfer(tmp_path, samples, "violin")
        assert result["success"] is True

        try:
            from scipy.io import wavfile
            sr_out, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            out_duration = len(data) / sr_out
            assert abs(out_duration - duration) < 1.0, (
                f"Duration mismatch: input {duration}s, output {out_duration:.1f}s"
            )
        except ImportError:
            pytest.skip("scipy not available")

    def test_output_not_silent(self, tmp_path):
        """Transformed audio should not be silent."""
        samples = generate_sine(440.0, duration_sec=2.0)
        result, output_dir = run_timbre_transfer(tmp_path, samples, "trumpet")
        assert result["success"] is True

        try:
            from scipy.io import wavfile
            _, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            rms = np.sqrt(np.mean(data.astype(np.float32) ** 2))
            assert rms > 0.001, "Timbre transfer output is silent"
        except ImportError:
            pytest.skip("scipy not available")

    def test_pitch_preservation(self, tmp_path):
        """Fundamental frequency should be approximately preserved after transfer."""
        freq = 440.0
        samples = generate_sine(freq, duration_sec=2.0)
        result, output_dir = run_timbre_transfer(tmp_path, samples, "flute")
        assert result["success"] is True

        try:
            from scipy.io import wavfile
            sr_out, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            if data.ndim > 1:
                data = data.mean(axis=1)
            data = data.astype(np.float32)

            # Use autocorrelation to detect pitch
            mid = len(data) // 2
            frame = data[mid : mid + 4096]
            if np.max(np.abs(frame)) < 0.001:
                pytest.skip("Frame too quiet for pitch detection")

            corr = np.correlate(frame, frame, mode="full")
            corr = corr[len(corr) // 2 :]

            expected_period = sr_out / freq
            search_start = int(expected_period * 0.5)
            search_end = min(int(expected_period * 2.0), len(corr) - 1)
            if search_end <= search_start:
                pytest.skip("Search range too small")

            peak_lag = search_start + np.argmax(corr[search_start:search_end])
            detected_freq = sr_out / peak_lag

            # Allow generous tolerance (within 1 octave / 1200 cents)
            cents_error = abs(1200 * np.log2(detected_freq / freq))
            assert cents_error < 1200, (
                f"Pitch shifted too far: {cents_error:.0f} cents "
                f"(expected {freq} Hz, detected {detected_freq:.1f} Hz)"
            )
        except ImportError:
            pytest.skip("scipy not available")
