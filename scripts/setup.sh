#!/usr/bin/env bash
set -euo pipefail

echo "=== Waive Development Setup ==="

# ── Linux dependencies (Debian/Ubuntu) ──────────────────────────────────────
if command -v apt &>/dev/null; then
    echo "Installing JUCE system dependencies..."
    sudo apt update
    sudo apt install -y \
        build-essential \
        cmake \
        git \
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
    echo "System dependencies installed."
else
    echo "Non-apt system detected. Install JUCE dependencies manually."
    echo "See: https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md"
fi

# ── Build ───────────────────────────────────────────────────────────────────
echo ""
echo "To build the engine:"
echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build --target WaiveEngine -j\$(nproc)"
echo ""
echo "Setup complete."
