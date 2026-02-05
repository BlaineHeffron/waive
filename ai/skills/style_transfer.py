"""
Style Transfer VST-Chain AI Skill — Prompt-to-plugin parameter mapping.

Maps text descriptions of audio styles to specific plugin chains
and parameter configurations. Applies the chain to tracks in the engine.
"""

import json
import re
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from waive_client import WaiveClient


@dataclass
class PluginSetting:
    """A single plugin with its parameter settings."""
    plugin_id: str       # VST3 plugin identifier or name
    plugin_name: str     # Human-readable name
    parameters: dict[str, float] = field(default_factory=dict)
    description: str = ""


@dataclass
class StylePreset:
    """A complete style preset with plugin chain."""
    name: str
    description: str
    tags: list[str] = field(default_factory=list)
    plugins: list[PluginSetting] = field(default_factory=list)


# ── Built-in Style Presets ──────────────────────────────────────────────────
# These use generic parameter names that map to common VST3 parameters.
# The actual plugin_id values need to match installed plugins on the system.

STYLE_PRESETS: dict[str, StylePreset] = {
    "lofi_cassette": StylePreset(
        name="Lo-Fi Cassette",
        description="Warm, degraded cassette tape sound with wow/flutter and noise",
        tags=["lofi", "cassette", "tape", "90s", "vintage", "warm", "retro"],
        plugins=[
            PluginSetting(
                plugin_id="eq",
                plugin_name="Parametric EQ",
                parameters={
                    "high_shelf_gain": -4.0,      # roll off highs
                    "high_shelf_freq": 8000.0,
                    "low_shelf_gain": 2.0,         # slight bass warmth
                    "low_shelf_freq": 200.0,
                    "mid_freq": 1000.0,
                    "mid_gain": 1.5,               # slight mid presence
                    "mid_q": 0.7,
                },
                description="Roll off high frequencies, add warmth",
            ),
            PluginSetting(
                plugin_id="saturation",
                plugin_name="Tape Saturation",
                parameters={
                    "drive": 0.45,         # moderate saturation
                    "mix": 0.7,            # mostly wet
                    "tone": 0.4,           # darker tone
                    "output": 0.85,
                },
                description="Tape-style harmonic saturation",
            ),
            PluginSetting(
                plugin_id="chorus",
                plugin_name="Chorus/Modulation",
                parameters={
                    "rate": 0.3,           # slow modulation for wow/flutter
                    "depth": 0.15,         # subtle pitch variation
                    "mix": 0.25,
                    "feedback": 0.1,
                },
                description="Subtle wow and flutter effect",
            ),
        ],
    ),

    "bright_modern_pop": StylePreset(
        name="Bright Modern Pop",
        description="Clean, bright, polished pop production sound",
        tags=["bright", "modern", "pop", "clean", "polished", "radio"],
        plugins=[
            PluginSetting(
                plugin_id="eq",
                plugin_name="Parametric EQ",
                parameters={
                    "high_shelf_gain": 3.0,
                    "high_shelf_freq": 10000.0,
                    "low_cut_freq": 80.0,          # clean low end
                    "low_cut_enabled": 1.0,
                    "mid_freq": 3000.0,
                    "mid_gain": 2.0,               # presence boost
                    "mid_q": 1.0,
                },
                description="Bright EQ with presence boost and low cut",
            ),
            PluginSetting(
                plugin_id="compressor",
                plugin_name="Compressor",
                parameters={
                    "threshold": -18.0,
                    "ratio": 3.0,
                    "attack": 10.0,        # ms
                    "release": 100.0,      # ms
                    "makeup_gain": 4.0,
                },
                description="Moderate compression for polished dynamics",
            ),
        ],
    ),

    "dark_ambient": StylePreset(
        name="Dark Ambient",
        description="Ethereal, dark atmospheric sound with heavy reverb",
        tags=["dark", "ambient", "atmospheric", "ethereal", "space", "drone"],
        plugins=[
            PluginSetting(
                plugin_id="eq",
                plugin_name="Parametric EQ",
                parameters={
                    "high_shelf_gain": -6.0,
                    "high_shelf_freq": 6000.0,
                    "low_shelf_gain": 3.0,
                    "low_shelf_freq": 300.0,
                    "mid_freq": 800.0,
                    "mid_gain": -2.0,
                    "mid_q": 0.5,
                },
                description="Dark EQ curve with reduced highs",
            ),
            PluginSetting(
                plugin_id="reverb",
                plugin_name="Reverb",
                parameters={
                    "decay": 0.85,         # long tail
                    "size": 0.9,           # large space
                    "damping": 0.6,        # some high-freq absorption
                    "mix": 0.55,           # mostly wet
                    "predelay": 40.0,      # ms
                },
                description="Large, dark reverb space",
            ),
            PluginSetting(
                plugin_id="delay",
                plugin_name="Delay",
                parameters={
                    "time_ms": 500.0,
                    "feedback": 0.45,
                    "mix": 0.2,
                    "high_cut": 4000.0,
                },
                description="Filtered feedback delay for depth",
            ),
        ],
    ),

    "vinyl_warmth": StylePreset(
        name="Vinyl Warmth",
        description="Warm vinyl record character with gentle crackle",
        tags=["vinyl", "warm", "analog", "record", "vintage"],
        plugins=[
            PluginSetting(
                plugin_id="eq",
                plugin_name="Parametric EQ",
                parameters={
                    "high_shelf_gain": -3.0,
                    "high_shelf_freq": 12000.0,
                    "low_shelf_gain": 1.5,
                    "low_shelf_freq": 150.0,
                    "mid_freq": 2500.0,
                    "mid_gain": 1.0,
                    "mid_q": 0.8,
                },
                description="Gentle vinyl EQ curve",
            ),
            PluginSetting(
                plugin_id="saturation",
                plugin_name="Saturation",
                parameters={
                    "drive": 0.25,
                    "mix": 0.5,
                    "tone": 0.5,
                    "output": 0.9,
                },
                description="Light tube-style saturation",
            ),
        ],
    ),

    "aggressive_distortion": StylePreset(
        name="Aggressive Distortion",
        description="Heavy distorted sound for rock/metal/industrial",
        tags=["distortion", "heavy", "aggressive", "rock", "metal",
              "industrial", "gritty", "dirty"],
        plugins=[
            PluginSetting(
                plugin_id="eq",
                plugin_name="Parametric EQ",
                parameters={
                    "low_cut_freq": 100.0,
                    "low_cut_enabled": 1.0,
                    "mid_freq": 2000.0,
                    "mid_gain": 4.0,
                    "mid_q": 0.6,
                    "high_shelf_gain": 1.0,
                    "high_shelf_freq": 8000.0,
                },
                description="Pre-distortion EQ with mid scoop",
            ),
            PluginSetting(
                plugin_id="saturation",
                plugin_name="Distortion",
                parameters={
                    "drive": 0.8,
                    "mix": 0.9,
                    "tone": 0.6,
                    "output": 0.7,
                },
                description="Heavy distortion/overdrive",
            ),
        ],
    ),

    "clean_acoustic": StylePreset(
        name="Clean Acoustic",
        description="Natural, transparent sound for acoustic instruments",
        tags=["clean", "acoustic", "natural", "transparent", "organic",
              "unplugged", "folk"],
        plugins=[
            PluginSetting(
                plugin_id="eq",
                plugin_name="Parametric EQ",
                parameters={
                    "low_cut_freq": 60.0,
                    "low_cut_enabled": 1.0,
                    "mid_freq": 5000.0,
                    "mid_gain": 1.5,
                    "mid_q": 1.2,
                    "high_shelf_gain": 1.0,
                    "high_shelf_freq": 12000.0,
                },
                description="Gentle clarity EQ for acoustic instruments",
            ),
            PluginSetting(
                plugin_id="reverb",
                plugin_name="Reverb",
                parameters={
                    "decay": 0.4,
                    "size": 0.5,
                    "damping": 0.3,
                    "mix": 0.2,
                    "predelay": 15.0,
                },
                description="Small natural room reverb",
            ),
        ],
    ),

    "80s_synthwave": StylePreset(
        name="80s Synthwave",
        description="Retro 80s synth sound with gated reverb and chorus",
        tags=["80s", "synthwave", "retro", "synth", "neon", "retrowave",
              "outrun"],
        plugins=[
            PluginSetting(
                plugin_id="chorus",
                plugin_name="Chorus",
                parameters={
                    "rate": 0.8,
                    "depth": 0.5,
                    "mix": 0.4,
                    "feedback": 0.2,
                },
                description="Rich 80s-style chorus",
            ),
            PluginSetting(
                plugin_id="reverb",
                plugin_name="Gated Reverb",
                parameters={
                    "decay": 0.3,          # short, gated
                    "size": 0.7,
                    "damping": 0.2,
                    "mix": 0.4,
                    "predelay": 5.0,
                },
                description="Gated reverb for that 80s snare sound",
            ),
            PluginSetting(
                plugin_id="compressor",
                plugin_name="Compressor",
                parameters={
                    "threshold": -15.0,
                    "ratio": 4.0,
                    "attack": 5.0,
                    "release": 50.0,
                    "makeup_gain": 5.0,
                },
                description="Punchy compression for synthwave dynamics",
            ),
        ],
    ),
}


class StyleTransfer:
    """Maps text prompts to plugin chains and applies them to tracks."""

    def __init__(self, client: WaiveClient,
                 custom_presets: dict[str, StylePreset] | None = None):
        self.client = client
        self.presets = dict(STYLE_PRESETS)
        if custom_presets:
            self.presets.update(custom_presets)
        self._available_plugins: list[dict] | None = None

    def discover_plugins(self) -> list[dict]:
        """Query the engine for available VST3 plugins."""
        response = self.client.list_plugins()
        if response.get("status") == "ok":
            self._available_plugins = response.get("plugins", [])
        else:
            self._available_plugins = []
        return self._available_plugins

    def _resolve_plugin_id(self, generic_id: str) -> str:
        """
        Resolve a generic plugin ID (e.g., "eq", "reverb") to an actual
        installed plugin name by matching against available plugins.
        """
        if self._available_plugins is None:
            try:
                self.discover_plugins()
            except Exception:
                return generic_id

        if not self._available_plugins:
            return generic_id

        # Match by category or name substring
        generic_lower = generic_id.lower()
        category_keywords = {
            "eq": ["eq", "equalizer", "parametric"],
            "compressor": ["compressor", "comp", "dynamics"],
            "reverb": ["reverb", "room", "hall", "plate"],
            "delay": ["delay", "echo"],
            "chorus": ["chorus", "ensemble", "modulation"],
            "saturation": ["saturation", "distortion", "overdrive",
                           "tube", "tape", "drive"],
        }

        keywords = category_keywords.get(generic_lower, [generic_lower])

        for plugin in self._available_plugins:
            plugin_name = plugin.get("name", "").lower()
            plugin_cat = plugin.get("category", "").lower()
            for keyword in keywords:
                if keyword in plugin_name or keyword in plugin_cat:
                    return plugin.get("name", generic_id)

        return generic_id

    def run(self, prompt: str, track_ids: list[int] | None = None) -> dict:
        """
        Apply a style to one or more tracks based on a text prompt.

        Args:
            prompt: Style description (e.g., "90s lo-fi cassette",
                    "make it sound like dark ambient")
            track_ids: Track indices to apply the style to.
                       If None, applies to all tracks.

        Returns:
            dict with matched style, applied plugins, and results.
        """
        # Step 1: Match prompt to a style preset
        preset = self._match_prompt(prompt)
        if preset is None:
            return {
                "status": "error",
                "message": f"No matching style found for: '{prompt}'",
                "available_styles": list(self.presets.keys()),
                "hint": "Try keywords like: lofi, bright, dark, vinyl, "
                        "distortion, acoustic, synthwave",
            }

        # Step 2: Get target tracks
        if track_ids is None:
            tracks_response = self.client.get_tracks()
            if tracks_response.get("status") != "ok":
                raise RuntimeError(f"Failed to get tracks: {tracks_response}")
            track_ids = [t["index"] for t in tracks_response.get("tracks", [])]

        if not track_ids:
            return {
                "status": "ok",
                "message": "No tracks to apply style to.",
                "matched_style": preset.name,
            }

        # Step 3: Apply the plugin chain to each track
        results = []
        for track_id in track_ids:
            track_result = self._apply_preset(track_id, preset)
            results.append(track_result)

        return {
            "status": "ok",
            "matched_style": preset.name,
            "style_description": preset.description,
            "prompt": prompt,
            "plugins_applied": [asdict(p) for p in preset.plugins],
            "track_results": results,
        }

    def list_styles(self) -> list[dict]:
        """List all available style presets."""
        return [
            {
                "key": key,
                "name": preset.name,
                "description": preset.description,
                "tags": preset.tags,
                "plugin_count": len(preset.plugins),
            }
            for key, preset in self.presets.items()
        ]

    def _match_prompt(self, prompt: str) -> Optional[StylePreset]:
        """Match a text prompt to the best style preset."""
        prompt_lower = prompt.lower()

        # Score each preset by tag overlap
        best_score = 0
        best_preset = None

        for key, preset in self.presets.items():
            score = 0

            # Check preset name match
            if key.replace("_", " ") in prompt_lower:
                score += 10
            if preset.name.lower() in prompt_lower:
                score += 10

            # Check tag matches
            prompt_words = set(re.findall(r'\w+', prompt_lower))
            for tag in preset.tags:
                if tag in prompt_lower:
                    score += 3
                elif tag in prompt_words:
                    score += 2

            # Check description word overlap
            desc_words = set(re.findall(r'\w+', preset.description.lower()))
            overlap = prompt_words & desc_words
            score += len(overlap)

            if score > best_score:
                best_score = score
                best_preset = preset

        # Require minimum match score
        if best_score >= 2:
            return best_preset
        return None

    def _apply_preset(self, track_id: int, preset: StylePreset) -> dict:
        """Apply a style preset's plugin chain to a single track."""
        plugin_results = []

        for plugin_setting in preset.plugins:
            # Resolve generic plugin ID to an actual installed plugin name
            resolved_id = self._resolve_plugin_id(plugin_setting.plugin_id)

            # Load the plugin
            load_result = self.client.load_plugin(
                track_id=track_id,
                plugin_id=resolved_id,
            )

            plugin_loaded = load_result.get("status") == "ok"
            param_results = []

            if plugin_loaded:
                # Set each parameter
                actual_plugin_id = load_result.get(
                    "plugin_name", plugin_setting.plugin_id
                )

                for param_id, value in plugin_setting.parameters.items():
                    param_result = self.client.set_parameter(
                        track_id=track_id,
                        plugin_id=actual_plugin_id,
                        param_id=param_id,
                        value=value,
                    )
                    param_results.append({
                        "param_id": param_id,
                        "value": value,
                        "applied": param_result.get("status") == "ok",
                    })

            plugin_results.append({
                "plugin_id": plugin_setting.plugin_id,
                "plugin_name": plugin_setting.plugin_name,
                "loaded": plugin_loaded,
                "parameters_set": param_results,
            })

        return {
            "track_id": track_id,
            "plugins": plugin_results,
        }


def main():
    """CLI entry point for style transfer."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Apply audio style via plugin chain"
    )
    subparsers = parser.add_subparsers(dest="command")

    # Apply style
    apply_parser = subparsers.add_parser("apply", help="Apply a style")
    apply_parser.add_argument("prompt", help="Style description")
    apply_parser.add_argument("--tracks", nargs="*", type=int,
                              help="Track indices (default: all)")
    apply_parser.add_argument("--host", default="127.0.0.1")
    apply_parser.add_argument("--port", type=int, default=9090)

    # List styles
    list_parser = subparsers.add_parser("list", help="List available styles")

    args = parser.parse_args()

    if args.command == "list":
        client = WaiveClient()
        transfer = StyleTransfer(client=client)
        styles = transfer.list_styles()
        print(json.dumps(styles, indent=2))

    elif args.command == "apply":
        client = WaiveClient(host=args.host, port=args.port)
        client.connect()
        try:
            transfer = StyleTransfer(client=client)
            result = transfer.run(args.prompt, track_ids=args.tracks)
            print(json.dumps(result, indent=2))
        finally:
            client.disconnect()
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
