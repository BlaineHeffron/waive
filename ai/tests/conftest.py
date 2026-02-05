"""Pytest fixtures for Waive tests."""

import pytest
import sys
from pathlib import Path
from unittest.mock import Mock, MagicMock

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from waive_client import WaiveClient


@pytest.fixture
def mock_socket():
    """Mock socket for testing wire protocol encoding/decoding."""
    mock = Mock()
    mock.recv = Mock(return_value=b"")
    mock.sendall = Mock()
    mock.connect = Mock()
    mock.close = Mock()
    return mock


@pytest.fixture
def mock_client(mock_socket, monkeypatch):
    """WaiveClient with mocked socket connection."""
    client = WaiveClient()

    # Mock socket.socket to return our mock_socket
    def mock_socket_init(*args, **kwargs):
        return mock_socket

    monkeypatch.setattr("socket.socket", mock_socket_init)

    return client
