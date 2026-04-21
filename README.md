# Waive

**AI-assisted DAW built with JUCE and Tracktion Engine**

## Features

- **56 console commands** for timeline, clip, track, automation, preset, and media operations
- **5 AI helper commands** for track/transport queries, tool discovery, and session undo/redo
- **7 built-in tools** for audio analysis and manipulation
- **10 Python tools** for audio analysis and processing
- **Multi-provider AI chat** integrated into the workflow
- Full undo/redo support with command coalescing
- Real-time playback and transport control
- Plugin management and VST/AU support

## Screenshot

_(Screenshot placeholder - GUI shows tabbed interface with Session, Library, Plugins, Console, and Tool Log views)_

## Building from Source

### Ubuntu Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  libasound2-dev \
  libjack-jackd2-dev \
  ladspa-sdk \
  libcurl4-openssl-dev \
  libfreetype6-dev \
  libx11-dev \
  libxcomposite-dev \
  libxcursor-dev \
  libxext-dev \
  libxinerama-dev \
  libxrandr-dev \
  libxrender-dev \
  libwebkit2gtk-4.0-dev \
  libglu1-mesa-dev \
  mesa-common-dev
```

### Build Instructions

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build (use half the available cores to avoid OOM, but never fewer than 1)
cmake --build build --target Waive -j$(( $(nproc) / 2 < 1 ? 1 : $(nproc) / 2 ))

# Binary location
build/gui/Waive_artefacts/Release/Waive
```

**Note:** JUCE and Tracktion Engine are fetched via CMake FetchContent. First build may take several minutes.

## Running

```bash
./build/gui/Waive_artefacts/Release/Waive
```

### Live Rebuild + Relaunch (Linux)

```bash
# Optional, but recommended for fast file watching
sudo apt-get install -y inotify-tools

# Rebuild and relaunch Waive whenever source files change
./scripts/dev_watch.sh

# Pass args to Waive after --
./scripts/dev_watch.sh -- --screenshot /tmp/waive_shots
```

The watcher writes runtime output to `logs/waive_dev_watch.log`.

## Testing

### C++ Tests

```bash
# Run all test suites
ctest --test-dir build --output-on-failure

# Test suites: WaiveCoreTests, WaiveUiTests, WaiveToolTests
```

### Python Tests

```bash
# Install Python dependencies
pip install -r tools/tests/requirements.txt
for req in tools/*/requirements.txt; do
  [ -f "$req" ] && pip install -r "$req"
done

# Run audio tool tests
python3 -m pytest tools/tests/ -v
```

## Keyboard Shortcuts

| Command | Shortcut |
|---------|----------|
| **File** | |
| New | `Ctrl/Cmd+N` |
| Open | `Ctrl/Cmd+O` |
| Save | `Ctrl/Cmd+S` |
| Save As | `Ctrl/Cmd+Shift+S` |
| **Edit** | |
| Undo | `Ctrl/Cmd+Z` |
| Redo | `Ctrl/Cmd+Shift+Z` |
| Delete | `Delete` |
| Duplicate | `Ctrl/Cmd+D` |
| Split at Playhead | `Ctrl/Cmd+E` |
| Delete Track | `Ctrl/Cmd+Backspace` |
| **Transport** | |
| Play/Stop | `Space` |
| Record | `R` |
| Go to Start | `Home` |
| **View** | |
| Toggle Tool Sidebar | `Ctrl/Cmd+T` |
| AI Chat | `Ctrl/Cmd+Shift+C` |
| Keyboard Shortcuts | `Ctrl/Cmd+/` |

## Architecture

See [docs/architecture.md](docs/architecture.md) for detailed architecture documentation.

**Key Components:**
- `engine/` — Core `CommandHandler`, no GUI dependencies
- `gui/` — JUCE UI, session editing, and AI/chat integration under `gui/src/ai/`
- `tools/` — Built-in and external Python audio processing tools

## License

MIT License - see [LICENSE](LICENSE) for details.
