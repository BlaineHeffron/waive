# Waive

**AI-assisted DAW built with JUCE and Tracktion Engine**

## Features

- **48 commands** for timeline, clip, and track editing
- **7 built-in tools** for audio analysis and manipulation
- **10 Python AI tools** for intelligent audio processing
- **Multi-provider AI chat** integrated into the workflow
- Full undo/redo support with command coalescing
- Real-time audio preview and playback
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

# Build (use half the available cores to avoid OOM)
cmake --build build --target Waive -j$(($(nproc)/2))

# Binary location
build/gui/Waive_artefacts/Release/Waive
```

**Note:** JUCE and Tracktion Engine are fetched via CMake FetchContent. First build may take several minutes.

## Running

```bash
./build/gui/Waive_artefacts/Release/Waive
```

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
pip install -r ai/requirements.txt
pip install -r tools/tests/requirements.txt

# Run AI tests
python -m pytest ai/tests/ -v

# Run audio tool tests
python -m pytest tools/tests/ -v
```

## Keyboard Shortcuts

| Command | Shortcut |
|---------|----------|
| **File** | |
| New | `Cmd+N` |
| Open | `Cmd+O` |
| Save | `Cmd+S` |
| Save As | `Cmd+Shift+S` |
| **Edit** | |
| Undo | `Cmd+Z` |
| Redo | `Cmd+Shift+Z` |
| Delete | `Delete` |
| Duplicate | `Cmd+D` |
| Split at Playhead | `Cmd+E` |
| Delete Track | `Cmd+Backspace` |
| **Transport** | |
| Play/Stop | `Space` |
| Record | `R` |
| Go to Start | `Home` |
| **View** | |
| Toggle Tool Sidebar | `Cmd+T` |
| AI Chat | `Cmd+Shift+C` |
| Keyboard Shortcuts | `Cmd+/` |

## Architecture

See [docs/architecture.md](docs/architecture.md) for detailed architecture documentation.

**Key Components:**
- `engine/` — Core CommandHandler, no GUI dependencies
- `gui/` — JUCE-based UI with SessionComponent, timeline, tools
- `ai/` — Python AI integration (chat, audio analysis)
- `tools/` — Built-in and Python audio processing tools

## License

MIT License - see [LICENSE](LICENSE) for details.
