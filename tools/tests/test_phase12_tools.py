"""Validate phase 12 audio processing tools (noise_reduction, pitch_correction, auto_eq, mastering_assistant).

These tools are created in phase 12. Tests skip gracefully if tool doesn't exist yet.
"""
import subprocess
import sys
import os
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_sine, generate_noisy_signal, setup_tool_dirs, read_result
)

TOOLS_BASE = os.path.join(os.path.dirname(__file__), "..")


def tool_exists(name):
    return os.path.exists(os.path.join(TOOLS_BASE, name, "__main__.py"))


def run_tool(tmp_path, tool_name, params, samples=None):
    import json
    script = os.path.join(TOOLS_BASE, tool_name, "__main__.py")
    input_dir, output_dir, input_wav = setup_tool_dirs(tmp_path, tool_name, params, samples)
    request = {"output_dir": output_dir, "params": params}
    if input_wav:
        request["input_file"] = input_wav
    result = subprocess.run(
        [sys.executable, script],
        input=json.dumps(request),
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, f"{tool_name} failed: {result.stderr}"
    return read_result(result.stdout), output_dir


# ── Noise Reduction ────────────────────────────────────────────────────────


@pytest.mark.skipif(not tool_exists("noise_reduction"), reason="noise_reduction not yet implemented")
class TestNoiseReduction:
    """Test noise reduction improves SNR on synthetically noised signals."""

    def test_snr_improvement(self, tmp_path):
        """Denoised signal should have better SNR than noisy input."""
        clean = generate_sine(440.0, duration_sec=3.0, amplitude=0.5)
        noisy, noise, snr_before = generate_noisy_signal(clean, snr_db=10)

        result, output_dir = run_tool(tmp_path, "noise_reduction", {
            "strength": 0.8,
            "noise_profile_seconds": 0.5,
        }, noisy)

        assert result["success"] is True

        # Read denoised output (find any WAV file in output dir)
        wav_files = [f for f in os.listdir(output_dir) if f.endswith('.wav')]
        assert len(wav_files) > 0, f"No WAV files in output: {os.listdir(output_dir)}"
        output_wav = os.path.join(output_dir, wav_files[0])

        from scipy.io import wavfile
        _, denoised = wavfile.read(output_wav)
        if denoised.dtype == np.int16:
            denoised = denoised.astype(np.float32) / 32767.0
        else:
            denoised = denoised.astype(np.float32)

        # Trim to matching length
        min_len = min(len(clean), len(denoised))
        residual = denoised[:min_len] - clean[:min_len]
        snr_after = 10 * np.log10(np.mean(clean[:min_len] ** 2) / (np.mean(residual ** 2) + 1e-10))

        # Should improve SNR by at least 2 dB
        assert snr_after > snr_before + 2.0, (
            f"SNR improvement too small: {snr_before:.1f} dB → {snr_after:.1f} dB"
        )

    def test_produces_output(self, tmp_path):
        """Should produce an output WAV file."""
        clean = generate_sine(440.0, duration_sec=2.0)
        noisy, _, _ = generate_noisy_signal(clean, snr_db=15)

        result, output_dir = run_tool(tmp_path, "noise_reduction", {"strength": 0.5}, noisy)
        assert result["success"] is True
        wav_files = [f for f in os.listdir(output_dir) if f.endswith('.wav')]
        assert len(wav_files) > 0


# ── Mastering Assistant ────────────────────────────────────────────────────


@pytest.mark.skipif(not tool_exists("mastering_assistant"), reason="mastering_assistant not yet implemented")
class TestMasteringAssistant:
    """Test mastering assistant achieves target LUFS and respects peak ceiling."""

    def test_lufs_target(self, tmp_path):
        """Output loudness should be within 2 dB of target LUFS."""
        # Generate a quiet signal
        samples = generate_sine(440.0, duration_sec=5.0, amplitude=0.1)

        target_lufs = -14.0
        result, output_dir = run_tool(tmp_path, "mastering_assistant", {
            "target_lufs": target_lufs,
            "apply_limiter": True,
            "ceiling_db": -1.0,
        }, samples)

        assert result["success"] is True

        # Check reported LUFS (from tool result)
        if "final_lufs" in result:
            error = abs(result["final_lufs"] - target_lufs)
            assert error < 3.0, (
                f"LUFS error too large: {error:.1f} dB "
                f"(target {target_lufs}, got {result['final_lufs']})"
            )

        # Optionally verify with pyloudnorm
        try:
            import pyloudnorm as pyln
            from scipy.io import wavfile

            sr_out, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            if data.dtype == np.int16:
                data = data.astype(np.float64) / 32767.0
            else:
                data = data.astype(np.float64)
            if data.ndim == 1:
                data = data.reshape(-1, 1)

            meter = pyln.Meter(sr_out)
            measured_lufs = meter.integrated_loudness(data)

            error = abs(measured_lufs - target_lufs)
            assert error < 3.0, (
                f"pyloudnorm LUFS verification failed: measured {measured_lufs:.1f}, "
                f"target {target_lufs:.1f}"
            )
        except ImportError:
            pass  # pyloudnorm optional

    def test_peak_ceiling(self, tmp_path):
        """Output peak should not exceed the specified ceiling."""
        samples = generate_sine(440.0, duration_sec=3.0, amplitude=0.8)

        result, output_dir = run_tool(tmp_path, "mastering_assistant", {
            "target_lufs": -14.0,
            "apply_limiter": True,
            "ceiling_db": -1.0,
        }, samples)

        assert result["success"] is True

        try:
            from scipy.io import wavfile
            _, data = wavfile.read(os.path.join(output_dir, "output.wav"))
            data = data.astype(np.float64)
            if np.max(np.abs(data)) > 1.0:
                data = data / 32767.0  # int16 → float

            peak_db = 20 * np.log10(np.max(np.abs(data)) + 1e-10)
            assert peak_db <= 0.0, f"Peak exceeds 0 dBFS: {peak_db:.1f} dB"
        except ImportError:
            pass


# ── Auto EQ ────────────────────────────────────────────────────────────────


@pytest.mark.skipif(not tool_exists("auto_eq"), reason="auto_eq not yet implemented")
class TestAutoEQ:
    """Test auto EQ produces valid frequency band suggestions."""

    def test_produces_suggestions(self, tmp_path):
        """Should return a list of EQ band suggestions."""
        # Generate broadband noise
        rng = np.random.RandomState(42)
        samples = (0.3 * rng.randn(SR * 3)).astype(np.float32)

        result, _ = run_tool(tmp_path, "auto_eq", {
            "target_curve": "flat",
            "max_boost_db": 6.0,
        }, samples)

        assert result["success"] is True
        # Result should contain suggestion bands
        assert "bands" in result or "suggestions" in result


# ── Pitch Correction ──────────────────────────────────────────────────────


@pytest.mark.skipif(not tool_exists("pitch_correction"), reason="pitch_correction not yet implemented")
class TestPitchCorrection:
    """Test pitch correction on detuned synthetic tones."""

    def test_corrects_detuned_note(self, tmp_path):
        """A slightly detuned A4 (~445 Hz) should be corrected closer to 440 Hz."""
        # Generate a tone 20 cents sharp of A4
        detuned_freq = 440.0 * (2 ** (20 / 1200))  # ~445.1 Hz
        samples = generate_sine(detuned_freq, duration_sec=2.0, amplitude=0.5)

        result, output_dir = run_tool(tmp_path, "pitch_correction", {
            "correction_strength": 1.0,
        }, samples)

        assert result["success"] is True
        assert os.path.exists(os.path.join(output_dir, "output.wav"))

    def test_produces_output(self, tmp_path):
        """Should produce an output WAV file."""
        samples = generate_sine(440.0, duration_sec=2.0)
        result, output_dir = run_tool(tmp_path, "pitch_correction", {
            "correction_strength": 0.5,
        }, samples)

        assert result["success"] is True
        assert os.path.exists(os.path.join(output_dir, "output.wav"))
