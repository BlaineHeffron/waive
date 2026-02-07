# Phase 04: Built-in Python Tools

## Objective
Create two example Python-based external tools (timbre transfer and music generation) that follow the external tool I/O contract from Phase 3, plus a setup script. These live in the `tools/` directory at the project root.

## Build System
- This phase creates NO C++ files. No CMake changes needed.
- Validation: just verify the JSON manifests parse correctly and the Python scripts have no syntax errors.

## Architecture Context

### External Tool I/O Contract (from Phase 3)
External tools receive:
- `--input-dir <path>` — directory containing `params.json` and optionally `input.wav`
- `--output-dir <path>` — directory where the tool writes `result.json` and optionally `output.wav`

**Input:**
- `params.json`: JSON object with tool parameters
- `input.wav`: (optional) input audio file if the tool accepts audio input

**Output:**
- `result.json`: JSON object with at least `{"success": true/false, "message": "..."}`. Can include additional metadata.
- `output.wav`: (optional) output audio file if the tool produces audio

### Manifest Format
```json
{
  "name": "tool_name",
  "displayName": "Human Readable Name",
  "version": "1.0.0",
  "description": "What this tool does",
  "inputSchema": { "type": "object", "properties": {...}, "required": [...] },
  "executable": "python3",
  "arguments": ["script.py"],
  "acceptsAudioInput": true/false,
  "producesAudioOutput": true/false,
  "timeoutMs": 600000
}
```

## Implementation Tasks

### 1. Create `tools/timbre_transfer/timbre_transfer.waive-tool.json`

```json
{
  "name": "timbre_transfer",
  "displayName": "Timbre Transfer",
  "version": "1.0.0",
  "description": "Transform the timbre of an audio clip to sound like a different instrument. Select a clip and choose the target instrument.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "target_instrument": {
        "type": "string",
        "description": "Target instrument timbre",
        "enum": ["trumpet", "violin", "flute", "saxophone", "cello", "piano"]
      }
    },
    "required": ["target_instrument"]
  },
  "executable": "python3",
  "arguments": ["timbre_transfer.py"],
  "acceptsAudioInput": true,
  "producesAudioOutput": true,
  "timeoutMs": 600000
}
```

### 2. Create `tools/timbre_transfer/timbre_transfer.py`

A Python script that:
1. Parses `--input-dir` and `--output-dir` from command line args (use `argparse`)
2. Reads `params.json` from input dir
3. Reads `input.wav` from input dir
4. Attempts to use `librosa` for pitch/onset detection + additive synthesis with instrument-specific harmonics
5. Falls back to basic spectral filtering (using only `numpy` and `scipy`) if librosa is not installed
6. Writes `output.wav` to output dir
7. Writes `result.json` to output dir

**Full mode (with librosa):**
- Load audio with `librosa.load()`
- Detect pitch contour with `librosa.pyin()`
- Detect onsets with `librosa.onset.onset_detect()`
- Resynthesize using additive synthesis with instrument-specific harmonic profiles:
  - trumpet: strong odd harmonics, bright
  - violin: rich harmonics with vibrato
  - flute: fundamental-heavy, few harmonics
  - saxophone: strong low harmonics, reed-like
  - cello: warm, strong low harmonics
  - piano: percussive attack, decaying harmonics
- Write output with `soundfile.write()`

**Fallback mode (numpy/scipy only):**
- Read WAV with `scipy.io.wavfile.read()`
- Apply basic spectral filtering: FFT → shape magnitude spectrum toward instrument profile → IFFT
- Write output with `scipy.io.wavfile.write()`

**Error handling:**
- Wrap everything in try/except
- Always write `result.json` even on failure
- result.json format: `{"success": true/false, "message": "...", "mode": "full"|"fallback"}`

### 3. Create `tools/timbre_transfer/requirements.txt`

```
librosa>=0.10.0
soundfile>=0.12.0
numpy>=1.24.0
scipy>=1.10.0
```

### 4. Create `tools/music_generation/music_generation.waive-tool.json`

```json
{
  "name": "music_generation",
  "displayName": "Music Generation",
  "version": "1.0.0",
  "description": "Generate audio from a text prompt. Produces a clip of the specified duration.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "prompt": {
        "type": "string",
        "description": "Text description of the music to generate"
      },
      "duration_seconds": {
        "type": "number",
        "description": "Duration of generated audio in seconds (1-30)",
        "minimum": 1,
        "maximum": 30
      },
      "temperature": {
        "type": "number",
        "description": "Generation temperature (0.1-2.0, higher = more creative)",
        "minimum": 0.1,
        "maximum": 2.0
      }
    },
    "required": ["prompt"]
  },
  "executable": "python3",
  "arguments": ["music_generation.py"],
  "acceptsAudioInput": false,
  "producesAudioOutput": true,
  "timeoutMs": 600000
}
```

### 5. Create `tools/music_generation/music_generation.py`

A Python script that:
1. Parses `--input-dir` and `--output-dir` from command line args
2. Reads `params.json` from input dir
3. Extracts: `prompt` (required), `duration_seconds` (default 8), `temperature` (default 1.0)
4. Attempts to use `audiocraft` (MusicGen) for generation
5. Falls back to basic synthesis if audiocraft is not installed
6. Writes `output.wav` and `result.json` to output dir

**Full mode (with audiocraft):**
- `from audiocraft.models import MusicGen`
- `model = MusicGen.get_pretrained('facebook/musicgen-small')`
- `model.set_generation_params(duration=duration_seconds, temperature=temperature)`
- `wav = model.generate([prompt])`
- Save output as WAV at 32000 Hz (MusicGen default sample rate)

**Fallback mode (numpy only):**
- Parse the prompt for keywords to determine synthesis type:
  - "drum", "beat", "percussion" → generate a simple drum pattern (kick + snare + hi-hat using sine waves + noise)
  - "bass" → generate a bass line (low frequency sine waves with rhythm)
  - "ambient", "pad" → generate a chord pad (layered sine waves with slow LFO)
  - default → generate a simple melodic pattern (random notes from a pentatonic scale)
- Use `numpy` for synthesis at 44100 Hz
- Apply basic envelope (attack/release) to avoid clicks
- Write with `scipy.io.wavfile.write()` or raw WAV writing with `struct`

**Error handling:**
- Same pattern as timbre_transfer: always write result.json
- result.json: `{"success": true/false, "message": "...", "mode": "full"|"fallback", "duration": <actual_seconds>}`

### 6. Create `tools/music_generation/requirements.txt`

```
audiocraft>=1.0.0
torch>=2.0.0
soundfile>=0.12.0
numpy>=1.24.0
scipy>=1.10.0
```

### 7. Create `tools/setup.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Setting up Waive external tools..."
echo ""

# Create virtual environment if it doesn't exist
if [ ! -d "$SCRIPT_DIR/.venv" ]; then
    echo "Creating Python virtual environment..."
    python3 -m venv "$SCRIPT_DIR/.venv"
fi

echo "Activating virtual environment..."
source "$SCRIPT_DIR/.venv/bin/activate"

echo "Installing timbre_transfer dependencies..."
pip install -r "$SCRIPT_DIR/timbre_transfer/requirements.txt"

echo "Installing music_generation dependencies..."
pip install -r "$SCRIPT_DIR/music_generation/requirements.txt"

echo ""
echo "Setup complete! Tools are ready to use."
echo "Virtual environment: $SCRIPT_DIR/.venv"
```

Make it executable.

## Files Expected To Change
- `tools/timbre_transfer/timbre_transfer.waive-tool.json` (NEW)
- `tools/timbre_transfer/timbre_transfer.py` (NEW)
- `tools/timbre_transfer/requirements.txt` (NEW)
- `tools/music_generation/music_generation.waive-tool.json` (NEW)
- `tools/music_generation/music_generation.py` (NEW)
- `tools/music_generation/requirements.txt` (NEW)
- `tools/setup.sh` (NEW)

## Validation

```bash
# Check JSON manifests are valid
python3 -c "import json; json.load(open('tools/timbre_transfer/timbre_transfer.waive-tool.json'))"
python3 -c "import json; json.load(open('tools/music_generation/music_generation.waive-tool.json'))"

# Check Python scripts have no syntax errors
python3 -m py_compile tools/timbre_transfer/timbre_transfer.py
python3 -m py_compile tools/music_generation/music_generation.py

# Verify setup.sh is executable
test -x tools/setup.sh && echo "setup.sh is executable"
```

## Exit Criteria
- Both manifest JSON files are valid and contain all required fields.
- Both Python scripts run without import errors in fallback mode (no ML dependencies needed).
- Scripts follow the I/O contract: read from `--input-dir`, write to `--output-dir`.
- `result.json` is always written, even on error.
- `setup.sh` creates a venv and installs all dependencies.
- No C++ build changes needed.
