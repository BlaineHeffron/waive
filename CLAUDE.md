# Waive

Open-source AI-driven DAW. Native C++ engine (JUCE + Tracktion Engine) for real-time audio/VST3 hosting. Python "Headless Producer" layer for AI-driven transforms, arrangement, and stem separation.

## Architecture

```
Python AI Layer ──JSON/TCP:9090──► C++ Engine (JUCE)
                                   CommandServer → CommandHandler → Edit
```

- C++ owns real-time audio, VST3 hosting, and Edit state (Tracktion Engine `ValueTree`/XML)
- Python owns AI inference, generates files/MIDI, sends commands to mutate the Edit
- All AI runs locally — no audio leaves the machine

See [docs/architecture.md](docs/architecture.md) for the full design.

## Build & Run

```bash
# Prerequisites: CMake >= 3.22, C++17 compiler, Python >= 3.10
# Linux deps: scripts/setup.sh

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target WaiveEngine -j$(nproc)

# Run engine (listens on TCP :9090)
./build/engine/WaiveEngine_artefacts/Release/WaiveEngine

# Run Python client
cd ai && pip install -r requirements.txt && python waive_client.py
```

## Project Structure

```
engine/src/
  Main.cpp              # Entry point — Engine + Edit init, starts CommandServer
  CommandServer.h/cpp   # TCP server (JUCE InterprocessConnection)
  CommandHandler.h/cpp  # JSON → Tracktion Engine API dispatch
ai/
  waive_client.py       # Python client with convenience methods
docs/
  architecture.md       # Architecture deep-dive
  command_protocol.md   # Command table, wire format, examples
  command_schema.json   # Formal JSON Schema
  tracktion_cheatsheet.md  # Tracktion Engine API quick reference
```

## Development Rules

- **Single source of truth**: all Edit mutations go through `CommandHandler` — never modify Edit state outside this path.
- **C++17**, no exceptions in the audio thread.
- **Python client is thin**: inference and file generation happen in Python; only finished commands cross the bridge.
- Validate file paths in Python before sending `insert_audio_clip`.
- Use `juce::InterprocessConnection` for the socket server, not raw sockets.

## AI Agent Rules

- **Don't read large Tracktion Engine source files.** Use grep for specific method signatures.
- **Save cheat sheets** in `docs/` to avoid re-fetching library docs.
- **Focus on the command boundary**: most work touches `CommandHandler.cpp` and `waive_client.py`.
- Command protocol details: [docs/command_protocol.md](docs/command_protocol.md)
- Tracktion API patterns: [docs/tracktion_cheatsheet.md](docs/tracktion_cheatsheet.md)
