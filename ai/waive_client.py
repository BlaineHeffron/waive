"""
Waive Python Client — "Hello AI" Bridge

Connects to the Waive C++ Engine over TCP and sends JSON commands.
Uses JUCE InterprocessConnection wire format: 4-byte magic + 4-byte length prefix.
"""

import json
import socket
import struct
import sys
from typing import Any

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 9090

# JUCE InterprocessConnection magic number (0xADIO in the C++ code = 0x0000AD10)
JUCE_MAGIC = 0x0000AD10


class WaiveClient:
    """Thin client for sending JSON commands to the Waive engine."""

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT):
        self.host = host
        self.port = port
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        print(f"Connected to Waive engine at {self.host}:{self.port}")

    def disconnect(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None
            print("Disconnected.")

    def send_command(self, command: dict[str, Any]) -> dict[str, Any]:
        """Send a JSON command and return the parsed JSON response."""
        if not self.sock:
            raise ConnectionError("Not connected. Call connect() first.")

        payload = json.dumps(command).encode("utf-8")

        # JUCE InterprocessConnection wire format:
        # [4 bytes: magic number] [4 bytes: payload length (big-endian)] [payload]
        header = struct.pack(">II", JUCE_MAGIC, len(payload))
        self.sock.sendall(header + payload)

        # Read response header
        resp_header = self._recv_exact(8)
        _magic, resp_len = struct.unpack(">II", resp_header)

        # Read response payload
        resp_data = self._recv_exact(resp_len)
        return json.loads(resp_data.decode("utf-8"))

    def _recv_exact(self, n: int) -> bytes:
        """Read exactly n bytes from the socket."""
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("Connection closed by engine.")
            data += chunk
        return data

    # ── Convenience methods ──────────────────────────────────────────────

    def ping(self) -> dict:
        return self.send_command({"action": "ping"})

    def get_tracks(self) -> dict:
        return self.send_command({"action": "get_tracks"})

    def get_edit_state(self) -> dict:
        return self.send_command({"action": "get_edit_state"})

    def add_track(self) -> dict:
        return self.send_command({"action": "add_track"})

    def set_track_volume(self, track_id: int, value_db: float) -> dict:
        return self.send_command({
            "action": "set_track_volume",
            "track_id": track_id,
            "value_db": value_db,
        })

    def set_track_pan(self, track_id: int, value: float) -> dict:
        return self.send_command({
            "action": "set_track_pan",
            "track_id": track_id,
            "value": value,
        })

    def insert_audio_clip(
        self, track_id: int, file_path: str, start_time: float = 0.0
    ) -> dict:
        return self.send_command({
            "action": "insert_audio_clip",
            "track_id": track_id,
            "file_path": file_path,
            "start_time": start_time,
        })

    def transport_play(self) -> dict:
        return self.send_command({"action": "transport_play"})

    def transport_stop(self) -> dict:
        return self.send_command({"action": "transport_stop"})

    def transport_seek(self, position: float) -> dict:
        return self.send_command({"action": "transport_seek", "position": position})


# ── Demo: "Hello AI" bridge ──────────────────────────────────────────────────

def main():
    client = WaiveClient()

    try:
        client.connect()

        # 1. Ping the engine
        print("\n--- Ping ---")
        print(client.ping())

        # 2. List tracks
        print("\n--- Tracks ---")
        print(json.dumps(client.get_tracks(), indent=2))

        # 3. Move the volume fader on track 0 to -6 dB
        print("\n--- Set Track 0 Volume to -6 dB ---")
        result = client.set_track_volume(track_id=0, value_db=-6.0)
        print(json.dumps(result, indent=2))

        # 4. Verify the change
        print("\n--- Tracks (after volume change) ---")
        print(json.dumps(client.get_tracks(), indent=2))

        print("\nHello AI bridge test complete.")

    except ConnectionRefusedError:
        print(
            f"Could not connect to engine at {client.host}:{client.port}.\n"
            "Make sure the Waive Engine is running first.",
            file=sys.stderr,
        )
        sys.exit(1)
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
