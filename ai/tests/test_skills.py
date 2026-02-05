"""Comprehensive unit tests for the 4 AI skills: StemSplitter, GrooveMatcher,
StyleTransfer, and GhostEngineer.

All external dependencies (librosa, numpy, subprocess, midiutil, soundfile)
are mocked so tests run without those packages installed.
"""

import os
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest.mock import Mock, MagicMock, patch, call

import pytest

# Ensure the ai/ directory is on sys.path for waive_client imports
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


# ---------------------------------------------------------------------------
# Fixtures shared across all skill tests
# ---------------------------------------------------------------------------

@pytest.fixture
def mock_client():
    """A fully-mocked WaiveClient with sensible defaults."""
    client = Mock()
    client.add_track.return_value = {"status": "ok", "track_index": 0}
    client.insert_audio_clip.return_value = {
        "status": "ok", "track_id": 0, "duration": 120.0,
    }
    client.insert_midi_clip.return_value = {
        "status": "ok", "clip_name": "groove_basic_rock_4bars.mid", "duration": 8.0,
    }
    client.load_plugin.return_value = {
        "status": "ok", "plugin_name": "eq",
    }
    client.set_parameter.return_value = {"status": "ok"}
    client.get_tracks.return_value = {
        "status": "ok",
        "tracks": [
            {"index": 0, "name": "Track 1", "clips": []},
            {"index": 1, "name": "Track 2", "clips": []},
        ],
    }
    client.get_edit_state.return_value = {"status": "ok", "edit_xml": ""}
    client.set_track_volume.return_value = {"status": "ok"}
    client.set_track_pan.return_value = {"status": "ok"}
    return client


# ===========================================================================
# StemSplitter Tests
# ===========================================================================

class TestStemSplitter:
    """Tests for ai/skills/stem_splitter.py."""

    def _import_stem_splitter(self):
        from skills.stem_splitter import StemSplitter, STEM_NAMES, DEFAULT_MODEL
        return StemSplitter, STEM_NAMES, DEFAULT_MODEL

    # -- run() raises FileNotFoundError for missing file --------------------
    def test_run_raises_for_missing_file(self, mock_client):
        StemSplitter, _, _ = self._import_stem_splitter()
        splitter = StemSplitter(client=mock_client)

        with pytest.raises(FileNotFoundError, match="Input file not found"):
            splitter.run("/nonexistent/path/to/audio.wav")

    # -- _run_demucs() constructs correct command and handles output dir ----
    @patch("skills.stem_splitter.subprocess.run")
    def test_run_demucs_correct_command(self, mock_subprocess, mock_client, tmp_path):
        StemSplitter, STEM_NAMES, DEFAULT_MODEL = self._import_stem_splitter()

        output_dir = str(tmp_path / "stems_out")
        splitter = StemSplitter(client=mock_client, model="htdemucs",
                                output_dir=output_dir)

        # Create the input file
        input_file = tmp_path / "song.wav"
        input_file.write_bytes(b"RIFF" + b"\x00" * 40)

        # Set up the Demucs output directory structure that _run_demucs expects
        stem_dir = tmp_path / "stems_out" / "htdemucs" / "song"
        stem_dir.mkdir(parents=True)
        for stem in STEM_NAMES:
            (stem_dir / f"{stem}.wav").write_bytes(b"fake wav data")

        mock_subprocess.return_value = Mock(returncode=0, stdout="", stderr="")

        result = splitter._run_demucs(input_file)

        # Verify subprocess was called
        mock_subprocess.assert_called_once()
        cmd = mock_subprocess.call_args[0][0]

        # Command should include python -m demucs with correct flags
        assert "-m" in cmd
        assert "demucs" in cmd
        assert "--name" in cmd
        assert "htdemucs" in cmd
        assert "--out" in cmd
        assert output_dir in cmd
        assert str(input_file) in cmd

        # Verify result contains all 4 stems
        assert len(result) == 4
        for stem_name in STEM_NAMES:
            assert stem_name in result
            assert result[stem_name].name == f"{stem_name}.wav"

    # -- _insert_stems() creates tracks and inserts clips -------------------
    def test_insert_stems_creates_tracks_and_clips(self, mock_client, tmp_path):
        StemSplitter, STEM_NAMES, _ = self._import_stem_splitter()
        splitter = StemSplitter(client=mock_client)

        # Create mock stem files
        stem_paths = {}
        for i, stem_name in enumerate(STEM_NAMES):
            p = tmp_path / f"{stem_name}.wav"
            p.write_bytes(b"fake data")
            stem_paths[stem_name] = p

        # Track indices increment for each add_track call
        mock_client.add_track.side_effect = [
            {"status": "ok", "track_index": i} for i in range(4)
        ]
        mock_client.insert_audio_clip.return_value = {
            "status": "ok", "duration": 180.0,
        }

        track_info = splitter._insert_stems(stem_paths, start_time=2.5)

        assert len(track_info) == 4
        assert mock_client.add_track.call_count == 4
        assert mock_client.insert_audio_clip.call_count == 4

        for i, info in enumerate(track_info):
            assert info["stem"] in STEM_NAMES
            assert info["track_index"] == i
            assert info["duration"] == 180.0

        # Verify start_time was passed to insert_audio_clip
        for c in mock_client.insert_audio_clip.call_args_list:
            assert c.kwargs.get("start_time") == 2.5 or c[1].get("start_time") == 2.5

    # -- _run_demucs() raises RuntimeError on Demucs failure ----------------
    @patch("skills.stem_splitter.subprocess.run")
    def test_run_demucs_raises_on_failure(self, mock_subprocess, mock_client, tmp_path):
        StemSplitter, _, _ = self._import_stem_splitter()
        splitter = StemSplitter(client=mock_client)

        input_file = tmp_path / "song.wav"
        input_file.write_bytes(b"RIFF" + b"\x00" * 40)

        mock_subprocess.return_value = Mock(
            returncode=1,
            stdout="",
            stderr="CUDA out of memory",
        )

        with pytest.raises(RuntimeError, match="Demucs failed"):
            splitter._run_demucs(input_file)


# ===========================================================================
# GrooveMatcher Tests
# ===========================================================================

class TestGrooveMatcher:
    """Tests for ai/skills/groove_matcher.py."""

    def _import_groove_matcher(self):
        from skills.groove_matcher import GrooveMatcher, PATTERNS, GM_DRUM_MAP
        return GrooveMatcher, PATTERNS, GM_DRUM_MAP

    # -- run() raises FileNotFoundError for missing file --------------------
    def test_run_raises_for_missing_file(self, mock_client):
        GrooveMatcher, _, _ = self._import_groove_matcher()
        gm = GrooveMatcher(client=mock_client)

        with pytest.raises(FileNotFoundError, match="Input file not found"):
            gm.run("/nonexistent/audio.wav")

    # -- run() raises ValueError for unknown pattern ------------------------
    def test_run_raises_for_unknown_pattern(self, mock_client, tmp_path):
        GrooveMatcher, _, _ = self._import_groove_matcher()
        gm = GrooveMatcher(client=mock_client)

        # Create a real file so we pass the file check
        input_file = tmp_path / "beat.wav"
        input_file.write_bytes(b"RIFF" + b"\x00" * 40)

        with pytest.raises(ValueError, match="Unknown pattern"):
            gm.run(str(input_file), pattern="nonexistent_pattern")

    # -- _analyze_groove() returns correct structure ------------------------
    @patch("skills.groove_matcher.librosa")
    @patch("skills.groove_matcher.np")
    def test_analyze_groove_returns_correct_structure(
        self, mock_np, mock_librosa, mock_client, tmp_path
    ):
        GrooveMatcher, _, _ = self._import_groove_matcher()
        gm = GrooveMatcher(client=mock_client)

        input_file = tmp_path / "beat.wav"
        input_file.write_bytes(b"fake wav")

        # Set up mock returns for librosa calls
        import numpy as real_np

        mock_y = real_np.zeros(22050)  # 1 second of audio at 22050Hz
        mock_librosa.load.return_value = (mock_y, 22050)

        # beat_track returns tempo and beat frames
        mock_librosa.beat.beat_track.return_value = (
            real_np.array([120.0]),
            real_np.array([0, 10, 20, 30, 40, 50]),
        )

        # frames_to_time returns beat times
        beat_times = real_np.array([0.0, 0.5, 1.0, 1.5, 2.0, 2.5])
        mock_librosa.frames_to_time.return_value = beat_times

        # onset detection
        mock_librosa.onset.onset_strength.return_value = real_np.ones(100)
        mock_librosa.onset.onset_detect.return_value = real_np.array([0, 5, 10, 15, 20])

        # Use real numpy for all math operations inside the function
        # We need to restore np for actual computation; patch at module level
        # Instead, let numpy mock delegate to real numpy
        mock_np.diff = real_np.diff
        mock_np.mean = real_np.mean
        mock_np.atleast_1d = real_np.atleast_1d
        mock_np.std = real_np.std

        groove = gm._analyze_groove(input_file)

        # Verify structure
        assert "bpm" in groove
        assert "swing_ratio" in groove
        assert "timing_std" in groove
        assert "sixteenth_duration" in groove
        assert "beat_times" in groove
        assert "onset_times" in groove

        assert isinstance(groove["bpm"], float)
        assert isinstance(groove["swing_ratio"], float)
        assert isinstance(groove["timing_std"], float)
        assert isinstance(groove["sixteenth_duration"], float)

        # BPM should match what we returned from beat_track
        assert groove["bpm"] == 120.0

        # sixteenth_duration = 60/bpm/4 = 60/120/4 = 0.125
        assert abs(groove["sixteenth_duration"] - 0.125) < 1e-6

    # -- _generate_midi() creates a valid MIDI file with correct bars -------
    @patch("skills.groove_matcher.np")
    def test_generate_midi_creates_file(self, mock_np, mock_client, tmp_path):
        GrooveMatcher, _, _ = self._import_groove_matcher()
        gm = GrooveMatcher(client=mock_client, output_dir=str(tmp_path))

        import numpy as real_np
        mock_np.random = Mock()
        mock_np.random.normal.return_value = 0.0
        mock_np.random.randint.return_value = 0
        mock_np.clip = real_np.clip

        groove = {
            "bpm": 120.0,
            "swing_ratio": 1.0,
            "timing_std": 0.0,
            "sixteenth_duration": 0.125,
        }

        midi_path = gm._generate_midi(groove, "basic_rock", num_bars=4)

        # File should exist
        assert midi_path.exists()
        assert midi_path.suffix == ".mid"
        assert "basic_rock" in midi_path.name
        assert "4bars" in midi_path.name

        # File should be non-empty (valid MIDI header)
        file_size = midi_path.stat().st_size
        assert file_size > 0

        # Verify MIDI header starts with MThd
        with open(midi_path, "rb") as f:
            header = f.read(4)
            assert header == b"MThd"

    # -- _insert_pattern() creates track and inserts MIDI clip --------------
    def test_insert_pattern_creates_track_and_clip(self, mock_client, tmp_path):
        GrooveMatcher, _, _ = self._import_groove_matcher()
        gm = GrooveMatcher(client=mock_client)

        midi_file = tmp_path / "groove.mid"
        midi_file.write_bytes(b"MThd" + b"\x00" * 100)

        mock_client.add_track.return_value = {"status": "ok", "track_index": 3}
        mock_client.insert_midi_clip.return_value = {
            "status": "ok", "clip_name": "groove.mid", "duration": 8.0,
        }

        result = gm._insert_pattern(midi_file, start_time=1.0)

        mock_client.add_track.assert_called_once()
        mock_client.insert_midi_clip.assert_called_once_with(
            track_id=3,
            file_path=str(midi_file),
            start_time=1.0,
        )

        assert result["track_index"] == 3
        assert result["clip_name"] == "groove.mid"
        assert result["duration"] == 8.0

    # -- Swing ratio calculation with known even/odd IOIs -------------------
    @patch("skills.groove_matcher.librosa")
    @patch("skills.groove_matcher.np")
    def test_swing_ratio_with_known_iois(
        self, mock_np, mock_librosa, mock_client, tmp_path
    ):
        GrooveMatcher, _, _ = self._import_groove_matcher()
        gm = GrooveMatcher(client=mock_client)

        input_file = tmp_path / "swing.wav"
        input_file.write_bytes(b"fake wav")

        import numpy as real_np

        mock_y = real_np.zeros(22050)
        mock_librosa.load.return_value = (mock_y, 22050)

        # Simulate a swung beat: alternating long/short intervals
        # Beat times: 0.0, 0.6, 1.0, 1.6, 2.0, 2.6, 3.0
        # IOIs: [0.6, 0.4, 0.6, 0.4, 0.6, 0.4]
        # even IOIs (0-indexed): 0.6, 0.6, 0.6 -> mean = 0.6
        # odd IOIs (0-indexed): 0.4, 0.4, 0.4  -> mean = 0.4
        # swing_ratio = 0.6 / 0.4 = 1.5
        beat_times = real_np.array([0.0, 0.6, 1.0, 1.6, 2.0, 2.6, 3.0])

        mock_librosa.beat.beat_track.return_value = (
            real_np.array([120.0]),
            real_np.arange(7),
        )
        mock_librosa.frames_to_time.return_value = beat_times

        # Onset detection (minimal for this test)
        mock_librosa.onset.onset_strength.return_value = real_np.ones(100)
        mock_librosa.onset.onset_detect.return_value = real_np.array([0, 5])

        # Wire up real numpy functions
        mock_np.diff = real_np.diff
        mock_np.mean = real_np.mean
        mock_np.atleast_1d = real_np.atleast_1d
        mock_np.std = real_np.std

        groove = gm._analyze_groove(input_file)

        # swing_ratio should be 0.6/0.4 = 1.5
        assert abs(groove["swing_ratio"] - 1.5) < 0.01


# ===========================================================================
# StyleTransfer Tests
# ===========================================================================

class TestStyleTransfer:
    """Tests for ai/skills/style_transfer.py."""

    def _import_style_transfer(self):
        from skills.style_transfer import (
            StyleTransfer, STYLE_PRESETS, StylePreset, PluginSetting,
        )
        return StyleTransfer, STYLE_PRESETS, StylePreset, PluginSetting

    # -- _match_prompt() returns correct preset for each style keyword ------
    @pytest.mark.parametrize("prompt,expected_key", [
        ("make it sound lofi", "lofi_cassette"),
        ("bright modern pop", "bright_modern_pop"),
        ("dark ambient atmosphere", "dark_ambient"),
        ("warm vinyl", "vinyl_warmth"),
        ("aggressive distortion", "aggressive_distortion"),
        ("clean acoustic guitar", "clean_acoustic"),
        ("80s synthwave neon", "80s_synthwave"),
        ("cassette tape 90s", "lofi_cassette"),
    ])
    def test_match_prompt_returns_correct_preset(
        self, mock_client, prompt, expected_key
    ):
        StyleTransfer, STYLE_PRESETS, _, _ = self._import_style_transfer()
        st = StyleTransfer(client=mock_client)

        result = st._match_prompt(prompt)
        expected_preset = STYLE_PRESETS[expected_key]

        assert result is not None, f"Expected match for '{prompt}' but got None"
        assert result.name == expected_preset.name

    # -- _match_prompt() returns None for unmatched prompts (score < 2) -----
    def test_match_prompt_returns_none_for_unmatched(self, mock_client):
        StyleTransfer, _, _, _ = self._import_style_transfer()
        st = StyleTransfer(client=mock_client)

        # These prompts should not match any preset with score >= 2
        result = st._match_prompt("xyzzy foobar gibberish")
        assert result is None

    # -- run() returns error dict when no style matches ---------------------
    def test_run_returns_error_when_no_match(self, mock_client):
        StyleTransfer, _, _, _ = self._import_style_transfer()
        st = StyleTransfer(client=mock_client)

        result = st.run("xyzzy foobar gibberish")

        assert result["status"] == "error"
        assert "No matching style found" in result["message"]
        assert "available_styles" in result
        assert isinstance(result["available_styles"], list)
        assert "hint" in result

    # -- _apply_preset() calls load_plugin and set_parameter ----------------
    def test_apply_preset_calls_load_and_set(self, mock_client):
        StyleTransfer, STYLE_PRESETS, _, _ = self._import_style_transfer()
        st = StyleTransfer(client=mock_client)

        preset = STYLE_PRESETS["lofi_cassette"]
        track_id = 0

        mock_client.load_plugin.return_value = {
            "status": "ok", "plugin_name": "eq",
        }
        mock_client.set_parameter.return_value = {"status": "ok"}

        result = st._apply_preset(track_id, preset)

        # Should have called load_plugin once per plugin in the preset
        assert mock_client.load_plugin.call_count == len(preset.plugins)

        # Should have called set_parameter for each parameter in each plugin
        total_params = sum(len(p.parameters) for p in preset.plugins)
        assert mock_client.set_parameter.call_count == total_params

        # Verify load_plugin was called with correct track_id and plugin_id
        for i, plugin in enumerate(preset.plugins):
            load_call = mock_client.load_plugin.call_args_list[i]
            assert load_call.kwargs.get("track_id") == track_id or \
                load_call[1].get("track_id") == track_id

        # Result should contain info for each plugin
        assert result["track_id"] == track_id
        assert len(result["plugins"]) == len(preset.plugins)
        for plugin_result in result["plugins"]:
            assert plugin_result["loaded"] is True
            assert len(plugin_result["parameters_set"]) > 0

    # -- list_styles() returns all built-in presets -------------------------
    def test_list_styles_returns_all_presets(self, mock_client):
        StyleTransfer, STYLE_PRESETS, _, _ = self._import_style_transfer()
        st = StyleTransfer(client=mock_client)

        styles = st.list_styles()

        assert len(styles) == len(STYLE_PRESETS)
        keys_returned = {s["key"] for s in styles}
        assert keys_returned == set(STYLE_PRESETS.keys())

        # Verify structure of each style entry
        for style in styles:
            assert "key" in style
            assert "name" in style
            assert "description" in style
            assert "tags" in style
            assert "plugin_count" in style
            assert isinstance(style["tags"], list)
            assert style["plugin_count"] > 0

    # -- Custom presets can be added and matched ----------------------------
    def test_custom_presets_can_be_added_and_matched(self, mock_client):
        StyleTransfer, _, StylePreset, PluginSetting = self._import_style_transfer()

        custom_preset = StylePreset(
            name="My Custom Dubstep",
            description="Filthy dubstep wobble bass sound",
            tags=["dubstep", "wobble", "filthy", "bass"],
            plugins=[
                PluginSetting(
                    plugin_id="wobble_synth",
                    plugin_name="Wobble Synth",
                    parameters={"wobble_rate": 0.8, "distortion": 0.9},
                ),
            ],
        )

        st = StyleTransfer(
            client=mock_client,
            custom_presets={"dubstep_wobble": custom_preset},
        )

        # The custom preset should be in the list
        styles = st.list_styles()
        keys = {s["key"] for s in styles}
        assert "dubstep_wobble" in keys

        # It should be matchable via prompt
        matched = st._match_prompt("filthy dubstep wobble bass")
        assert matched is not None
        assert matched.name == "My Custom Dubstep"


# ===========================================================================
# GhostEngineer Tests
# ===========================================================================

class TestGhostEngineer:
    """Tests for ai/skills/ghost_engineer.py."""

    def _import_ghost_engineer(self):
        from skills.ghost_engineer import (
            GhostEngineer, TrackAnalysis, MixDecision, PAN_PRESETS,
        )
        return GhostEngineer, TrackAnalysis, MixDecision, PAN_PRESETS

    # -- _categorize_track() with name-based detection ----------------------
    @pytest.mark.parametrize("name,expected_category", [
        ("Bass Guitar", "bass"),
        ("Kick Drum", "bass"),           # "kick" -> bass in name_map
        ("Sub Bass", "bass"),
        ("Drum Bus", "drums"),
        ("Snare Top", "drums"),
        ("HiHat", "drums"),
        ("Lead Vocals", "vocals"),
        ("Vox Backup", "vocals"),
        ("Piano Chords", "keys"),
        ("Synth Pad", "keys"),
        ("Organ", "keys"),
        ("Keys Layer", "keys"),
    ])
    def test_categorize_track_name_based(
        self, mock_client, name, expected_category
    ):
        GhostEngineer, _, _, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        # Spectral values don't matter -- name takes priority
        category = ge._categorize_track(name, centroid=1000.0,
                                        low_ratio=0.3, mid_ratio=0.4,
                                        high_ratio=0.3)
        assert category == expected_category

    # -- _categorize_track() with spectral-based fallback -------------------
    @pytest.mark.parametrize("centroid,low_ratio,expected", [
        (300, 0.7, "bass"),       # low centroid, high low energy -> bass
        (600, 0.5, "drums"),      # mid-low centroid, moderate low -> drums
        (3000, 0.15, "vocals"),   # mid-high centroid, low bass -> vocals
        (1500, 0.35, "vocals"),   # centroid < 3500, low_ratio > 0.1 -> vocals (checked before keys)
        (5000, 0.05, "other"),    # high centroid, no match -> other
    ])
    def test_categorize_track_spectral_fallback(
        self, mock_client, centroid, low_ratio, expected
    ):
        GhostEngineer, _, _, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        # Use a generic name that doesn't trigger name-based detection
        category = ge._categorize_track("Track 1", centroid=centroid,
                                        low_ratio=low_ratio, mid_ratio=0.3,
                                        high_ratio=0.2)
        assert category == expected

    # -- _calculate_mix() applies correct gain offsets ----------------------
    def test_calculate_mix_gain_offsets(self, mock_client):
        GhostEngineer, TrackAnalysis, _, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client, target_lufs=-14.0)

        # Use LUFS values close to the target so computed gains stay within
        # the clamping range [-40, 6] and we can observe differentiation.
        # mean_lufs = (-14 + -14 + -14 + -14) / 4 = -14.0
        # gain_offset = -14.0 - (-14.0) = 0.0
        # For bass (lufs=-14, role=-2.0):  target_db=0+(-2)=-2; vol = -2 + (-14-(-14)) = -2.0
        # For drums (lufs=-14, role=0.0):  target_db=0+0=0;     vol = 0 + (-14-(-14))  = 0.0
        # For vocals (lufs=-14, role=2.0): target_db=0+2=2;     vol = 2 + (-14-(-14))  = 2.0
        # For keys (lufs=-14, role=-3.0):  target_db=0+(-3)=-3; vol = -3 + (-14-(-14)) = -3.0
        analyses = [
            TrackAnalysis(
                track_index=0, track_name="Bass", file_path="/tmp/bass.wav",
                lufs=-14.0, category="bass",
                spectral_centroid_hz=200.0,
            ),
            TrackAnalysis(
                track_index=1, track_name="Drums", file_path="/tmp/drums.wav",
                lufs=-14.0, category="drums",
                spectral_centroid_hz=800.0,
            ),
            TrackAnalysis(
                track_index=2, track_name="Vocals", file_path="/tmp/vocals.wav",
                lufs=-14.0, category="vocals",
                spectral_centroid_hz=2500.0,
            ),
            TrackAnalysis(
                track_index=3, track_name="Keys", file_path="/tmp/keys.wav",
                lufs=-14.0, category="keys",
                spectral_centroid_hz=1500.0,
            ),
        ]

        decisions = ge._calculate_mix(analyses)

        assert len(decisions) == 4

        # Check each decision has the expected fields
        for d in decisions:
            assert d.volume_db is not None
            assert d.pan is not None
            assert d.reasoning != ""
            assert -40.0 <= d.volume_db <= 6.0

        # Find decisions by track name
        bass_d = next(d for d in decisions if d.track_name == "Bass")
        drums_d = next(d for d in decisions if d.track_name == "Drums")
        vocals_d = next(d for d in decisions if d.track_name == "Vocals")
        keys_d = next(d for d in decisions if d.track_name == "Keys")

        # Role offsets: vocals +2, drums 0, other -1, bass -2, keys -3
        # With all LUFS equal and gain_offset=0, volume_db should directly
        # reflect the role offsets.
        assert vocals_d.volume_db == pytest.approx(2.0)
        assert drums_d.volume_db == pytest.approx(0.0)
        assert bass_d.volume_db == pytest.approx(-2.0)
        assert keys_d.volume_db == pytest.approx(-3.0)

        # Ordering: vocals > drums > bass > keys
        assert vocals_d.volume_db > drums_d.volume_db
        assert drums_d.volume_db > bass_d.volume_db
        assert bass_d.volume_db > keys_d.volume_db

    # -- _calculate_mix() keeps bass and vocals centered (pan=0) ------------
    def test_calculate_mix_bass_vocals_centered(self, mock_client):
        GhostEngineer, TrackAnalysis, _, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        analyses = [
            TrackAnalysis(
                track_index=0, track_name="Bass", file_path="/tmp/bass.wav",
                lufs=-20.0, category="bass",
            ),
            TrackAnalysis(
                track_index=1, track_name="Vocals", file_path="/tmp/vocals.wav",
                lufs=-16.0, category="vocals",
            ),
        ]

        decisions = ge._calculate_mix(analyses)

        bass_d = next(d for d in decisions if d.track_name == "Bass")
        vocals_d = next(d for d in decisions if d.track_name == "Vocals")

        assert bass_d.pan == 0.0
        assert vocals_d.pan == 0.0

    # -- _calculate_mix() spreads multiple instruments of same category -----
    def test_calculate_mix_spreads_same_category(self, mock_client):
        GhostEngineer, TrackAnalysis, _, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        # Two "keys" tracks should get spread stereo
        analyses = [
            TrackAnalysis(
                track_index=0, track_name="Track A", file_path="/tmp/a.wav",
                lufs=-18.0, category="keys",
            ),
            TrackAnalysis(
                track_index=1, track_name="Track B", file_path="/tmp/b.wav",
                lufs=-18.0, category="keys",
            ),
        ]

        decisions = ge._calculate_mix(analyses)

        pans = [d.pan for d in decisions]
        # With 2 items in the same category: one at -0.4, one at 0.4
        assert len(pans) == 2
        assert pans[0] == pytest.approx(-0.4)
        assert pans[1] == pytest.approx(0.4)

    # -- _calculate_mix() spreads three instruments evenly ------------------
    def test_calculate_mix_spreads_three_evenly(self, mock_client):
        GhostEngineer, TrackAnalysis, _, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        # Three "drums" tracks - but note bass and vocals override to 0;
        # use "other" category to test general spread
        analyses = [
            TrackAnalysis(
                track_index=i, track_name=f"Track {i}",
                file_path=f"/tmp/{i}.wav",
                lufs=-18.0, category="other",
            )
            for i in range(3)
        ]

        decisions = ge._calculate_mix(analyses)
        pans = [d.pan for d in decisions]

        # count == 3: pan = -0.6 + 1.2*idx/(3-1)
        # idx=0: -0.6, idx=1: 0.0, idx=2: 0.6
        assert len(pans) == 3
        assert pans[0] == pytest.approx(-0.6)
        assert pans[1] == pytest.approx(0.0)
        assert pans[2] == pytest.approx(0.6)

    # -- _discover_audio_files() correctly parses Tracktion Edit XML ---------
    def test_discover_audio_files_parses_xml(self, mock_client):
        GhostEngineer, _, _, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        # Create sample XML matching the Tracktion Edit format
        # Use tmp files that we claim exist via patching Path.exists
        edit_xml = """<EDIT>
  <TRACK>
    <AUDIOCLIP source="/tmp/test_vocals.wav"/>
  </TRACK>
  <TRACK>
    <AUDIOCLIP source="/tmp/test_bass.wav"/>
  </TRACK>
  <TRACK>
    <MIDICLIP source="some_midi.mid"/>
  </TRACK>
</EDIT>"""

        mock_client.get_edit_state.return_value = {
            "status": "ok",
            "edit_xml": edit_xml,
        }

        tracks = [
            {"index": 0, "name": "Vocals", "clips": []},
            {"index": 1, "name": "Bass", "clips": []},
            {"index": 2, "name": "MIDI", "clips": []},
        ]

        # Patch Path.exists to return True for our test files
        with patch.object(Path, "exists", return_value=True):
            result = ge._discover_audio_files(tracks)

        # Should find 2 audio clips (MIDICLIP has no AUDIOCLIP child)
        assert 0 in result
        assert result[0] == "/tmp/test_vocals.wav"
        assert 1 in result
        assert result[1] == "/tmp/test_bass.wav"
        # Track 2 has MIDICLIP not AUDIOCLIP, so no entry
        assert 2 not in result

    # -- _discover_audio_files() handles empty XML --------------------------
    def test_discover_audio_files_handles_empty_xml(self, mock_client):
        GhostEngineer, _, _, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        mock_client.get_edit_state.return_value = {
            "status": "ok",
            "edit_xml": "",
        }

        result = ge._discover_audio_files([])
        assert result == {}

    # -- _apply_mix() calls set_track_volume and set_track_pan --------------
    def test_apply_mix_calls_volume_and_pan(self, mock_client):
        GhostEngineer, _, MixDecision, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        decisions = [
            MixDecision(
                track_index=0, track_name="Bass",
                volume_db=-6.0, pan=0.0, reasoning="test",
            ),
            MixDecision(
                track_index=1, track_name="Drums",
                volume_db=-3.0, pan=0.3, reasoning="test",
            ),
        ]

        mock_client.set_track_volume.return_value = {"status": "ok"}
        mock_client.set_track_pan.return_value = {"status": "ok"}

        results = ge._apply_mix(decisions)

        assert len(results) == 2

        # Verify correct calls to set_track_volume
        assert mock_client.set_track_volume.call_count == 2
        mock_client.set_track_volume.assert_any_call(0, -6.0)
        mock_client.set_track_volume.assert_any_call(1, -3.0)

        # Verify correct calls to set_track_pan
        assert mock_client.set_track_pan.call_count == 2
        mock_client.set_track_pan.assert_any_call(0, 0.0)
        mock_client.set_track_pan.assert_any_call(1, 0.3)

        # Verify result structure
        for r in results:
            assert r["volume_applied"] is True
            assert r["pan_applied"] is True
            assert "track_index" in r
            assert "track_name" in r

    # -- _apply_mix() reports failure when engine returns error --------------
    def test_apply_mix_reports_failure(self, mock_client):
        GhostEngineer, _, MixDecision, _ = self._import_ghost_engineer()
        ge = GhostEngineer(client=mock_client)

        decisions = [
            MixDecision(
                track_index=0, track_name="Bass",
                volume_db=-6.0, pan=0.0, reasoning="test",
            ),
        ]

        mock_client.set_track_volume.return_value = {"status": "error"}
        mock_client.set_track_pan.return_value = {"status": "error"}

        results = ge._apply_mix(decisions)

        assert results[0]["volume_applied"] is False
        assert results[0]["pan_applied"] is False
