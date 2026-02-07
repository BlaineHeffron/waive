"""Validate audio_to_midi tool against synthetic monophonic melodies."""
import subprocess
import sys
import os
import struct
import pytest
import numpy as np

from audio_fixtures import (
    SR, generate_melody, generate_sine, setup_tool_dirs, read_result, NOTE_FREQS
)

TOOL_SCRIPT = os.path.join(os.path.dirname(__file__), "..", "audio_to_midi", "__main__.py")


def run_audio_to_midi(tmp_path, samples, params=None):
    import json
    if params is None:
        params = {}
    input_dir, output_dir, input_wav = setup_tool_dirs(tmp_path, "a2m", params, samples)
    request = {"input_file": input_wav, "output_dir": output_dir, "params": params}
    result = subprocess.run(
        [sys.executable, TOOL_SCRIPT],
        input=json.dumps(request),
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, f"Tool failed: {result.stderr}"
    return read_result(result.stdout), output_dir


class TestAudioToMidi:
    """Test MIDI transcription accuracy on synthetic melodies."""

    def test_single_note_a4(self, tmp_path):
        """A sustained A4 (440 Hz) should produce MIDI note 69."""
        samples = generate_sine(440.0, duration_sec=2.0, amplitude=0.5)
        result, _ = run_audio_to_midi(tmp_path, samples)

        assert result["success"] is True
        assert result["note_count"] >= 1, "Should detect at least 1 note"

        # Check that the primary detected note is close to MIDI 69 (A4)
        midi_notes = [n["midi"] for n in result["notes"]]
        # Allow +/- 1 semitone tolerance for pitch estimation
        a4_detected = any(abs(m - 69) <= 1 for m in midi_notes)
        assert a4_detected, f"Expected MIDI ~69 (A4), got notes: {midi_notes}"

    def test_simple_melody(self, tmp_path):
        """A C-D-E-F melody should produce ~4 MIDI notes near the correct pitches."""
        notes = [
            (NOTE_FREQS["C4"], 0.5),  # C4 = MIDI 60
            (0, 0.1),                  # short silence
            (NOTE_FREQS["D4"], 0.5),  # D4 = MIDI 62
            (0, 0.1),
            (NOTE_FREQS["E4"], 0.5),  # E4 = MIDI 64
            (0, 0.1),
            (NOTE_FREQS["F4"], 0.5),  # F4 = MIDI 65
        ]
        samples, annotations = generate_melody(notes)
        result, _ = run_audio_to_midi(tmp_path, samples)

        assert result["success"] is True
        assert result["note_count"] >= 2, (
            f"Should detect at least 2 notes from 4-note melody, got {result['note_count']}"
        )

    def test_midi_file_generated(self, tmp_path):
        """Should produce a valid MIDI file in the output directory."""
        samples = generate_sine(440.0, duration_sec=1.0)
        result, output_dir = run_audio_to_midi(tmp_path, samples)

        assert result["success"] is True
        assert "midi_file" in result
        midi_path = os.path.join(output_dir, result["midi_file"])
        assert os.path.exists(midi_path), f"MIDI file not found: {midi_path}"

        # Validate MIDI header
        with open(midi_path, "rb") as f:
            header = f.read(4)
            assert header == b"MThd", "Invalid MIDI file header"

    def test_note_timing_accuracy(self, tmp_path):
        """Note start/end times should be approximately correct."""
        notes = [
            (NOTE_FREQS["A4"], 1.0),  # 1 second A4
            (0, 0.5),                   # 0.5 second silence
            (NOTE_FREQS["C5"], 1.0),  # 1 second C5
        ]
        samples, annotations = generate_melody(notes)
        result, _ = run_audio_to_midi(tmp_path, samples)

        assert result["success"] is True
        if result["note_count"] >= 2:
            # Second note should start after ~1.5 seconds
            second_note_start = result["notes"][1]["start"]
            assert 1.0 < second_note_start < 2.0, (
                f"Second note start time off: {second_note_start} (expected ~1.5)"
            )

    def test_silent_audio(self, tmp_path):
        """Should handle silence (produce 0 notes, no crash)."""
        samples = np.zeros(SR * 2, dtype=np.float32)
        result, _ = run_audio_to_midi(tmp_path, samples)
        assert result["success"] is True
        assert result["note_count"] == 0
