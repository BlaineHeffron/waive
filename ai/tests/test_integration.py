"""Integration tests requiring running Waive engine."""

import json
import pytest
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from waive_client import WaiveClient


@pytest.fixture(scope="module")
def engine_client():
    """
    Client connected to a running Waive engine.

    These tests require the engine to be running:
        ./build/engine/WaiveEngine_artefacts/Release/WaiveEngine

    Skip if engine is not available.
    """
    client = WaiveClient()
    try:
        client.connect()
        yield client
        client.disconnect()
    except ConnectionRefusedError:
        pytest.skip("Waive engine not running on localhost:9090")


def test_ping_pong(engine_client):
    """Test basic ping command returns pong."""
    response = engine_client.ping()
    assert response["status"] == "ok"
    assert "message" in response


def test_add_track_returns_index(engine_client):
    """Test add_track creates a track and returns its index."""
    response = engine_client.add_track()
    assert response["status"] == "ok"
    assert "track_index" in response
    assert isinstance(response["track_index"], int)


def test_insert_audio_clip_with_valid_file(engine_client):
    """Test inserting an audio clip with a valid file path."""
    # Create a minimal WAV file for testing
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        # Write minimal WAV header (44 bytes) + 1 second of silence at 44.1kHz
        sample_rate = 44100
        num_samples = sample_rate  # 1 second
        bits_per_sample = 16
        num_channels = 1
        byte_rate = sample_rate * num_channels * bits_per_sample // 8
        block_align = num_channels * bits_per_sample // 8
        data_size = num_samples * num_channels * bits_per_sample // 8

        # RIFF header
        f.write(b"RIFF")
        f.write((36 + data_size).to_bytes(4, "little"))
        f.write(b"WAVE")

        # fmt chunk
        f.write(b"fmt ")
        f.write((16).to_bytes(4, "little"))  # chunk size
        f.write((1).to_bytes(2, "little"))   # PCM format
        f.write(num_channels.to_bytes(2, "little"))
        f.write(sample_rate.to_bytes(4, "little"))
        f.write(byte_rate.to_bytes(4, "little"))
        f.write(block_align.to_bytes(2, "little"))
        f.write(bits_per_sample.to_bytes(2, "little"))

        # data chunk
        f.write(b"data")
        f.write(data_size.to_bytes(4, "little"))
        f.write(b"\x00" * data_size)  # silence

        temp_wav = f.name

    try:
        # Add track first
        track_response = engine_client.add_track()
        track_id = track_response["track_index"]

        # Insert clip
        response = engine_client.insert_audio_clip(
            track_id=track_id,
            file_path=temp_wav,
            start_time=0.0
        )

        assert response["status"] == "ok"
        assert response["track_id"] == track_id

    finally:
        # Clean up temp file
        Path(temp_wav).unlink(missing_ok=True)


def test_get_tracks_lists_clips(engine_client):
    """Test get_tracks returns track list with clip info."""
    # Add a track
    engine_client.add_track()

    response = engine_client.get_tracks()
    assert response["status"] == "ok"
    assert "tracks" in response
    assert isinstance(response["tracks"], list)

    # Should have at least one track from previous test or this one
    assert len(response["tracks"]) > 0

    # Check track structure
    track = response["tracks"][0]
    assert "index" in track
    assert "name" in track
    assert "clips" in track


def test_invalid_action_returns_error(engine_client):
    """Test that invalid action returns error response."""
    response = engine_client.send_command({"action": "invalid_nonexistent_action"})
    assert response["status"] == "error"
    assert "message" in response
    assert "Unknown action" in response["message"]


def test_set_track_volume(engine_client):
    """Test setting track volume to -6 dB."""
    # Add a track to operate on
    track_response = engine_client.add_track()
    track_id = track_response["track_index"]

    response = engine_client.set_track_volume(track_id, -6.0)
    assert response["status"] == "ok"
    assert response["track_id"] == track_id
    assert response["volume_db"] == -6.0


def test_set_track_pan(engine_client):
    """Test setting track pan to 0.5 (half-right)."""
    track_response = engine_client.add_track()
    track_id = track_response["track_index"]

    response = engine_client.set_track_pan(track_id, 0.5)
    assert response["status"] == "ok"
    assert response["track_id"] == track_id
    assert response["pan"] == 0.5


def test_remove_track(engine_client):
    """Test adding then removing a track."""
    track_response = engine_client.add_track()
    track_id = track_response["track_index"]

    response = engine_client.remove_track(track_id)
    assert response["status"] == "ok"


def test_insert_midi_clip(engine_client):
    """Test inserting a MIDI clip onto a track."""
    from midiutil import MIDIFile

    # Create a minimal MIDI file
    midi = MIDIFile(1)
    midi.addTempo(0, 0, 120)
    midi.addNote(0, 0, 60, 0, 1, 100)  # track, channel, pitch, time, duration, velocity

    with tempfile.NamedTemporaryFile(suffix=".mid", delete=False) as f:
        midi.writeFile(f)
        temp_mid = f.name

    try:
        track_response = engine_client.add_track()
        track_id = track_response["track_index"]

        response = engine_client.insert_midi_clip(
            track_id=track_id,
            file_path=temp_mid,
            start_time=0.0,
        )

        assert response["status"] == "ok"
        assert "duration" in response
        assert "clip_name" in response
    finally:
        Path(temp_mid).unlink(missing_ok=True)


def test_transport_play_stop(engine_client):
    """Test starting and stopping transport playback."""
    play_response = engine_client.transport_play()
    assert play_response["status"] == "ok"

    stop_response = engine_client.transport_stop()
    assert stop_response["status"] == "ok"


def test_transport_seek(engine_client):
    """Test seeking transport to a specific position."""
    response = engine_client.transport_seek(5.0)
    assert response["status"] == "ok"
    assert response["position"] == 5.0


def test_get_edit_state_returns_xml(engine_client):
    """Test that get_edit_state returns non-empty XML."""
    response = engine_client.get_edit_state()
    assert response["status"] == "ok"
    assert "edit_xml" in response
    assert isinstance(response["edit_xml"], str)
    assert len(response["edit_xml"]) > 0


def test_set_track_volume_invalid_track(engine_client):
    """Test that set_track_volume on a non-existent track returns error."""
    response = engine_client.set_track_volume(9999, -6.0)
    assert response["status"] == "error"
    assert "message" in response


def test_insert_audio_clip_missing_file(engine_client):
    """Test that insert_audio_clip with a non-existent file returns error."""
    track_response = engine_client.add_track()
    track_id = track_response["track_index"]

    response = engine_client.insert_audio_clip(
        track_id=track_id,
        file_path="/tmp/nonexistent_file_waive_test.wav",
        start_time=0.0,
    )
    assert response["status"] == "error"
    assert "message" in response
