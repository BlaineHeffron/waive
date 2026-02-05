"""Unit tests for WaiveClient wire protocol."""

import json
import struct
import pytest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from waive_client import WaiveClient, JUCE_MAGIC


def test_wire_format_encoding(mock_client, mock_socket):
    """Test that commands are encoded with correct JUCE wire format."""
    command = {"action": "ping"}
    expected_payload = json.dumps(command).encode("utf-8")
    expected_header = struct.pack("<II", JUCE_MAGIC, len(expected_payload))

    # Mock response
    response = {"status": "ok"}
    response_payload = json.dumps(response).encode("utf-8")
    response_header = struct.pack("<II", JUCE_MAGIC, len(response_payload))
    mock_socket.recv.side_effect = [
        response_header[:8],
        response_payload
    ]

    mock_client.connect()
    result = mock_client.send_command(command)

    # Verify header + payload sent
    sent_data = mock_socket.sendall.call_args[0][0]
    assert sent_data[:8] == expected_header
    assert sent_data[8:] == expected_payload
    assert result == response


def test_wire_format_decoding(mock_client, mock_socket):
    """Test that responses are decoded correctly."""
    response = {"status": "ok", "message": "pong"}
    response_payload = json.dumps(response).encode("utf-8")
    response_header = struct.pack("<II", JUCE_MAGIC, len(response_payload))

    # Mock recv to return header then payload
    mock_socket.recv.side_effect = [
        response_header[:8],
        response_payload
    ]

    mock_client.connect()
    result = mock_client.send_command({"action": "ping"})

    assert result == response


def test_convenience_methods(mock_client, mock_socket):
    """Test that convenience methods build correct commands."""
    test_cases = [
        ("ping", {}, {"action": "ping"}),
        ("get_tracks", {}, {"action": "get_tracks"}),
        ("add_track", {}, {"action": "add_track"}),
        (
            "set_track_volume",
            {"track_id": 0, "value_db": -6.0},
            {"action": "set_track_volume", "track_id": 0, "value_db": -6.0}
        ),
        (
            "insert_audio_clip",
            {"track_id": 1, "file_path": "/tmp/test.wav", "start_time": 5.0},
            {"action": "insert_audio_clip", "track_id": 1, "file_path": "/tmp/test.wav", "start_time": 5.0}
        ),
    ]

    for method_name, kwargs, expected_command in test_cases:
        # Reset mock
        mock_socket.reset_mock()

        # Mock response
        response = {"status": "ok"}
        response_payload = json.dumps(response).encode("utf-8")
        response_header = struct.pack("<II", JUCE_MAGIC, len(response_payload))
        mock_socket.recv.side_effect = [response_header[:8], response_payload]

        # Call method
        mock_client.connect()
        method = getattr(mock_client, method_name)
        method(**kwargs)

        # Verify command payload
        sent_data = mock_socket.sendall.call_args[0][0]
        sent_payload = sent_data[8:]  # skip header
        sent_command = json.loads(sent_payload.decode("utf-8"))

        assert sent_command == expected_command


def test_recv_exact_handles_partial_reads(mock_client, mock_socket):
    """Test that _recv_exact correctly handles chunked socket reads."""
    # Simulate partial reads
    mock_socket.recv.side_effect = [
        b"\x00\x00",
        b"\xAD\x10",
        b"\x00\x00",
        b"\x00\x0F",
        b'{"status":',
        b'"ok"}',
    ]

    mock_client.connect()

    # Read 8-byte header (will take 4 recv calls)
    header = mock_client._recv_exact(8)
    assert len(header) == 8

    # Read 15-byte payload (will take 2 more recv calls)
    payload = mock_client._recv_exact(15)
    assert payload == b'{"status":"ok"}'
