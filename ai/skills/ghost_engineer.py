"""
Ghost Engineer AI Skill — AI-driven auto-mixing.

Analyzes multitrack audio for loudness, spectral content, and stereo characteristics.
Calculates and applies gain staging, panning, and basic EQ to achieve a balanced mix.
"""

import json
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional

import librosa
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from waive_client import WaiveClient


@dataclass
class TrackAnalysis:
    """Analysis results for a single track."""
    track_index: int
    track_name: str
    file_path: str
    rms_db: float = 0.0
    peak_db: float = 0.0
    lufs: float = 0.0
    spectral_centroid_hz: float = 0.0
    spectral_bandwidth_hz: float = 0.0
    low_energy_ratio: float = 0.0     # proportion of energy below 250Hz
    mid_energy_ratio: float = 0.0     # proportion of energy 250Hz-4kHz
    high_energy_ratio: float = 0.0    # proportion of energy above 4kHz
    category: str = "other"           # inferred: bass, drums, vocals, keys, other


@dataclass
class MixDecision:
    """Mixing decisions for a single track."""
    track_index: int
    track_name: str
    volume_db: float = 0.0
    pan: float = 0.0  # -1.0 to 1.0
    eq_low_cut_hz: Optional[float] = None
    eq_boost_db: Optional[float] = None
    eq_boost_freq_hz: Optional[float] = None
    reasoning: str = ""


# Target loudness for mix (approximate LUFS)
TARGET_MIX_LUFS = -14.0

# Panning presets by inferred track role
PAN_PRESETS = {
    "bass": 0.0,       # center
    "kick": 0.0,       # center
    "snare": 0.0,      # center
    "vocals": 0.0,     # center
    "drums": 0.0,      # center (overheads may be spread)
    "guitar_left": -0.5,
    "guitar_right": 0.5,
    "keys": 0.3,
    "synth": -0.3,
    "other": 0.0,
}

# Spectral centroid thresholds for categorization
CATEGORY_RULES = [
    # (max_centroid_hz, low_energy_threshold, category)
    (400, 0.6, "bass"),
    (800, 0.4, "drums"),
    (3500, 0.1, "vocals"),
    (2000, 0.3, "keys"),
]


class GhostEngineer:
    """AI mixing engineer that analyzes and auto-mixes a multitrack session."""

    def __init__(self, client: WaiveClient,
                 target_lufs: float = TARGET_MIX_LUFS):
        self.client = client
        self.target_lufs = target_lufs

    def run(self, audio_files: dict[int, str] | None = None) -> dict:
        """
        Analyze tracks and apply auto-mix settings.

        Args:
            audio_files: Optional mapping of track_index -> file_path for analysis.
                         If None, queries the engine for track/clip info via Edit XML.

        Returns:
            dict with analysis results and applied mix decisions.
        """
        # Step 1: Get track info from the engine
        tracks_response = self.client.get_tracks()
        if tracks_response.get("status") != "ok":
            raise RuntimeError(f"Failed to get tracks: {tracks_response}")

        tracks = tracks_response.get("tracks", [])
        if not tracks:
            return {"status": "ok", "message": "No tracks to mix.", "decisions": []}

        # Build audio file mapping from Edit XML if not provided
        if audio_files is None:
            audio_files = self._discover_audio_files(tracks)

        # Step 2: Analyze each track
        analyses = []
        for track in tracks:
            idx = track["index"]
            name = track["name"]
            file_path = audio_files.get(idx)
            if file_path and Path(file_path).exists():
                analysis = self._analyze_track(idx, name, file_path)
                analyses.append(analysis)

        if not analyses:
            return {
                "status": "ok",
                "message": "No audio files available for analysis. "
                           "Provide audio_files mapping.",
                "decisions": [],
            }

        # Step 3: Calculate mix decisions
        decisions = self._calculate_mix(analyses)

        # Step 4: Apply decisions to the engine
        applied = self._apply_mix(decisions)

        return {
            "status": "ok",
            "analyses": [asdict(a) for a in analyses],
            "decisions": [asdict(d) for d in decisions],
            "applied": applied,
        }

    def _analyze_track(self, track_index: int, name: str,
                       file_path: str) -> TrackAnalysis:
        """Perform spectral and loudness analysis on a single track."""
        # Load at a consistent sample rate for analysis; librosa handles resampling
        y, sr = librosa.load(file_path, sr=22050, mono=True)

        if len(y) == 0:
            return TrackAnalysis(track_index=track_index, track_name=name,
                                file_path=file_path)

        # Loudness metrics
        rms = librosa.feature.rms(y=y)[0]
        rms_mean = float(np.mean(rms))
        rms_db = float(20 * np.log10(rms_mean + 1e-10))

        peak = float(np.max(np.abs(y)))
        peak_db = float(20 * np.log10(peak + 1e-10))

        # Approximate LUFS (simplified — real LUFS uses K-weighting)
        lufs = rms_db - 0.691  # rough approximation

        # Spectral analysis
        centroid = librosa.feature.spectral_centroid(y=y, sr=sr)[0]
        centroid_hz = float(np.mean(centroid))

        bandwidth = librosa.feature.spectral_bandwidth(y=y, sr=sr)[0]
        bandwidth_hz = float(np.mean(bandwidth))

        # Energy distribution across frequency bands
        S = np.abs(librosa.stft(y))
        freqs = librosa.fft_frequencies(sr=sr)

        total_energy = np.sum(S ** 2)
        if total_energy > 0:
            low_mask = freqs < 250
            mid_mask = (freqs >= 250) & (freqs < 4000)
            high_mask = freqs >= 4000

            low_energy = float(np.sum(S[low_mask] ** 2) / total_energy)
            mid_energy = float(np.sum(S[mid_mask] ** 2) / total_energy)
            high_energy = float(np.sum(S[high_mask] ** 2) / total_energy)
        else:
            low_energy = mid_energy = high_energy = 0.33

        # Categorize the track
        category = self._categorize_track(
            name, centroid_hz, low_energy, mid_energy, high_energy
        )

        return TrackAnalysis(
            track_index=track_index,
            track_name=name,
            file_path=file_path,
            rms_db=round(rms_db, 1),
            peak_db=round(peak_db, 1),
            lufs=round(lufs, 1),
            spectral_centroid_hz=round(centroid_hz, 1),
            spectral_bandwidth_hz=round(bandwidth_hz, 1),
            low_energy_ratio=round(low_energy, 3),
            mid_energy_ratio=round(mid_energy, 3),
            high_energy_ratio=round(high_energy, 3),
            category=category,
        )

    def _categorize_track(self, name: str, centroid: float,
                          low_ratio: float, mid_ratio: float,
                          high_ratio: float) -> str:
        """Infer track role from name and spectral characteristics."""
        name_lower = name.lower()

        # Name-based detection first (most reliable)
        name_map = {
            "bass": "bass", "kick": "bass", "sub": "bass",
            "drum": "drums", "snare": "drums", "hat": "drums",
            "hihat": "drums", "cymbal": "drums", "tom": "drums",
            "perc": "drums",
            "vocal": "vocals", "vox": "vocals", "voice": "vocals",
            "sing": "vocals",
            "guitar": "other", "gtr": "other",
            "key": "keys", "piano": "keys", "organ": "keys",
            "synth": "keys", "pad": "keys",
        }

        for keyword, category in name_map.items():
            if keyword in name_lower:
                return category

        # Spectral-based fallback
        for max_centroid, low_thresh, category in CATEGORY_RULES:
            if centroid < max_centroid and low_ratio > low_thresh:
                return category

        return "other"

    def _calculate_mix(self, analyses: list[TrackAnalysis]) -> list[MixDecision]:
        """Calculate mixing decisions based on analysis results."""
        decisions = []

        # Calculate target gains — normalize loudness across tracks
        lufs_values = [a.lufs for a in analyses]
        mean_lufs = np.mean(lufs_values) if lufs_values else -20.0
        gain_offset = self.target_lufs - mean_lufs

        # Track how many of each category for auto-panning
        category_counts: dict[str, int] = {}
        for a in analyses:
            category_counts[a.category] = category_counts.get(a.category, 0) + 1

        category_index: dict[str, int] = {}

        for analysis in analyses:
            # Gain staging — bring each track toward a relative level
            # based on its role in the mix
            role_offset = {
                "bass": -2.0,    # slightly below center
                "drums": 0.0,    # at target
                "vocals": 2.0,   # slightly above for clarity
                "keys": -3.0,    # sit back in the mix
                "other": -1.0,
            }

            target_db = gain_offset + role_offset.get(analysis.category, 0.0)
            volume_db = round(target_db + (self.target_lufs - analysis.lufs), 1)
            # Clamp to reasonable range
            volume_db = max(-40.0, min(6.0, volume_db))

            # Panning — spread instruments across the stereo field
            cat = analysis.category
            count = category_counts.get(cat, 1)
            idx = category_index.get(cat, 0)
            category_index[cat] = idx + 1

            if count == 1:
                pan = PAN_PRESETS.get(cat, 0.0)
            elif count == 2:
                pan = -0.4 if idx == 0 else 0.4
            else:
                # Spread evenly
                pan = round(-0.6 + (1.2 * idx / max(count - 1, 1)), 2)

            # Keep bass, kick, and vocals centered
            if cat in ("bass", "vocals"):
                pan = 0.0

            reasoning = (
                f"Category: {cat}. LUFS: {analysis.lufs}dB. "
                f"Centroid: {analysis.spectral_centroid_hz}Hz. "
                f"Gain adjustment: {volume_db}dB to reach target {self.target_lufs} LUFS."
            )

            decisions.append(MixDecision(
                track_index=analysis.track_index,
                track_name=analysis.track_name,
                volume_db=volume_db,
                pan=pan,
                reasoning=reasoning,
            ))

        return decisions

    def _apply_mix(self, decisions: list[MixDecision]) -> list[dict]:
        """Apply mix decisions to the engine."""
        results = []

        for decision in decisions:
            # Apply volume
            vol_result = self.client.set_track_volume(
                decision.track_index, decision.volume_db
            )
            # Apply pan
            pan_result = self.client.set_track_pan(
                decision.track_index, decision.pan
            )

            results.append({
                "track_index": decision.track_index,
                "track_name": decision.track_name,
                "volume_applied": vol_result.get("status") == "ok",
                "pan_applied": pan_result.get("status") == "ok",
            })

        return results

    def _discover_audio_files(self, tracks: list[dict]) -> dict[int, str]:
        """
        Discover audio file paths by parsing Edit XML from get_edit_state.

        Returns a mapping of track_index -> file_path.
        """
        # Get full Edit state XML
        edit_state_response = self.client.get_edit_state()
        if edit_state_response.get("status") != "ok":
            raise RuntimeError(f"Failed to get edit state: {edit_state_response}")

        edit_xml = edit_state_response.get("edit_xml", "")
        if not edit_xml:
            return {}

        # Parse XML
        try:
            root = ET.fromstring(edit_xml)
        except ET.ParseError as e:
            raise RuntimeError(f"Failed to parse edit XML: {e}")

        # Find all AudioClip elements and extract source file paths
        audio_files = {}

        # Tracktion Edit structure: EDIT -> TRANSPORT, MACROPARAMETERS, MARKERTRACK, TEMPOTRACK, TRACK*
        # Each TRACK has AUDIOCLIP or MIDICLIP children
        for track_idx, track_elem in enumerate(root.findall(".//TRACK")):
            for clip in track_elem.findall(".//AUDIOCLIP"):
                source = clip.get("source")
                if source and Path(source).exists():
                    # Map to track by index (matches order in get_tracks)
                    audio_files[track_idx] = source
                    break  # Use first clip on track

        return audio_files


def main():
    """CLI entry point for auto-mixing."""
    import argparse

    parser = argparse.ArgumentParser(description="AI auto-mix engine tracks")
    parser.add_argument("--host", default="127.0.0.1", help="Engine host")
    parser.add_argument("--port", type=int, default=9090, help="Engine port")
    parser.add_argument("--target-lufs", type=float, default=-14.0,
                        help="Target loudness in LUFS")
    parser.add_argument("--files", nargs="*",
                        help="track_id:file_path pairs (e.g., 0:/path/vocals.wav)")
    args = parser.parse_args()

    # Parse file mappings
    audio_files = {}
    if args.files:
        for mapping in args.files:
            parts = mapping.split(":", 1)
            if len(parts) == 2:
                audio_files[int(parts[0])] = parts[1]

    client = WaiveClient(host=args.host, port=args.port)
    client.connect()

    try:
        engineer = GhostEngineer(client=client, target_lufs=args.target_lufs)
        result = engineer.run(audio_files=audio_files or None)
        print(json.dumps(result, indent=2, default=str))
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
