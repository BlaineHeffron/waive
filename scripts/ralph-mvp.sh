#!/usr/bin/env bash
# Launch Ralph Loop to build Waive to MVP.
# Usage: bash scripts/ralph-mvp.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "=== Waive MVP Ralph Loop ==="
echo "Project: $PROJECT_DIR"
echo "Prompt:  RALPH_MVP.md"
echo ""
echo "Starting Ralph Loop — Ctrl+C to stop at any time."
echo "Run '/cancel-ralph' inside the session to gracefully stop."
echo ""

cat RALPH_MVP.md | claude --continue \
  --allowedTools "Bash(build C++ engine:cmake),Bash(run tests:pytest),Bash(git operations:git),Edit,Write,Read,Glob,Grep,Task,TaskCreate,TaskUpdate,TaskGet,TaskList,Skill" \
  --print \
  -p "/ralph-loop:ralph-loop \"$(cat RALPH_MVP.md)\" --max-iterations 30 --completion-promise \"MVP COMPLETE\""
