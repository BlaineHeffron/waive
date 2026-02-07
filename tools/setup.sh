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
