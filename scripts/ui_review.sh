#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# ui_review.sh — Capture Waive screenshots and send to Claude for UI/UX review
#
# Usage:
#   ./scripts/ui_review.sh [--skip-build] [--quality 0.70] [--max-width 1400]
#
# Requirements:
#   - xvfb-run (apt install xvfb)
#   - ANTHROPIC_API_KEY environment variable
#   - ImageMagick (optional, for additional compression)
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
BINARY="$BUILD_DIR/gui/Waive_artefacts/Release/Waive"
OUTPUT_DIR="$PROJECT_DIR/screenshots"
REVIEW_FILE="$OUTPUT_DIR/review.md"

SKIP_BUILD=false
JPEG_QUALITY="0.70"
MAX_WIDTH="1400"

# ── Parse args ──────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build)  SKIP_BUILD=true; shift ;;
        --quality)     JPEG_QUALITY="$2"; shift 2 ;;
        --max-width)   MAX_WIDTH="$2"; shift 2 ;;
        *)             echo "Unknown arg: $1"; exit 1 ;;
    esac
done

log() { echo "[$(date '+%H:%M:%S')] $*"; }

# ── Build ───────────────────────────────────────────────────────────────────
if [[ "$SKIP_BUILD" == "false" ]]; then
    log "Building Waive..."
    cmake -B "$BUILD_DIR" "$PROJECT_DIR" 2>&1 | tail -3
    cmake --build "$BUILD_DIR" --target Waive -j$(($(nproc)/2)) 2>&1 | tail -5
    log "Build complete."
fi

if [[ ! -x "$BINARY" ]]; then
    log "ERROR: Binary not found at $BINARY"
    exit 1
fi

# ── Capture screenshots ────────────────────────────────────────────────────
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

log "Capturing screenshots under xvfb..."
xvfb-run -a -s "-screen 0 1920x1080x24" \
    "$BINARY" --screenshot "$OUTPUT_DIR" 2>&1 | tail -5 || true

# Count captured images
IMG_COUNT=$(find "$OUTPUT_DIR" -name "*.jpg" | wc -l)
log "Captured $IMG_COUNT screenshots."

if [[ "$IMG_COUNT" -eq 0 ]]; then
    log "ERROR: No screenshots were captured."
    exit 1
fi

# ── Optional: further compress with ImageMagick ────────────────────────────
if command -v convert &>/dev/null; then
    log "Compressing with ImageMagick (quality ${JPEG_QUALITY}, max-width ${MAX_WIDTH})..."
    for img in "$OUTPUT_DIR"/*.jpg; do
        convert "$img" -resize "${MAX_WIDTH}x>" -quality "$(echo "$JPEG_QUALITY * 100" | bc | cut -d. -f1)" "$img"
    done
    log "Compression complete."
fi

# ── Report file sizes ──────────────────────────────────────────────────────
log "Screenshot sizes:"
for img in "$OUTPUT_DIR"/*.jpg; do
    size=$(du -h "$img" | cut -f1)
    echo "  $(basename "$img"): $size"
done

# ── Send to Claude API for review ──────────────────────────────────────────
if [[ -z "${ANTHROPIC_API_KEY:-}" ]]; then
    log "ANTHROPIC_API_KEY not set — skipping AI review."
    log "Screenshots saved to: $OUTPUT_DIR"
    exit 0
fi

log "Sending screenshots to Claude for UI/UX review..."

# Build the content array with all images
CONTENT='[]'
for img in "$OUTPUT_DIR"/*.jpg; do
    base64_data=$(base64 -w0 "$img")
    filename=$(basename "$img" .jpg)
    # Add a text block naming the image, then the image block
    CONTENT=$(echo "$CONTENT" | jq --arg name "$filename" --arg data "$base64_data" \
        '. + [
            {"type": "text", "text": ("--- Screenshot: " + $name + " ---")},
            {"type": "image", "source": {"type": "base64", "media_type": "image/jpeg", "data": $data}}
        ]')
done

# Add the review prompt at the end
REVIEW_PROMPT='You are reviewing the UI/UX of "Waive", a desktop DAW (Digital Audio Workstation) built with JUCE. The app uses a dark theme with a custom color palette.

For each screenshot, analyze:
1. **Layout & Spacing** — Are elements well-aligned? Is spacing consistent? Are there crowded or empty areas?
2. **Visual Hierarchy** — Is it clear what is most important? Do headers, labels, and controls have appropriate weight?
3. **Color & Contrast** — Is text readable? Are interactive elements distinguishable? Does the palette feel cohesive?
4. **Component Design** — Do buttons, sliders, meters, and panels look polished? Any rough edges?
5. **Overall Impressions** — Does the app look professional? What are the top 3 improvements you would make?

For each issue, be specific: name the component, describe the problem, and suggest a concrete fix (including approximate pixel values, colors, or layout changes where applicable).

Output your review as structured Markdown with a section per screenshot, then a final "Priority Fixes" section ranking the top 5 actionable improvements.'

CONTENT=$(echo "$CONTENT" | jq --arg prompt "$REVIEW_PROMPT" \
    '. + [{"type": "text", "text": $prompt}]')

# Build the full API request
REQUEST=$(jq -n \
    --arg model "claude-sonnet-4-5-20250929" \
    --argjson content "$CONTENT" \
    '{
        model: $model,
        max_tokens: 4096,
        messages: [{role: "user", content: $content}]
    }')

RESPONSE=$(curl -s https://api.anthropic.com/v1/messages \
    -H "Content-Type: application/json" \
    -H "x-api-key: $ANTHROPIC_API_KEY" \
    -H "anthropic-version: 2023-06-01" \
    -d "$REQUEST")

# Extract text from response
REVIEW=$(echo "$RESPONSE" | jq -r '.content[0].text // .error.message // "Failed to parse response"')

echo "$REVIEW" > "$REVIEW_FILE"
log "Review saved to: $REVIEW_FILE"
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "$REVIEW"
echo "═══════════════════════════════════════════════════════════════"
