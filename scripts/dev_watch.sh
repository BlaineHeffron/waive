#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$PROJECT_DIR/build"
TARGET="Waive"
BINARY_PATH=""
POLL_INTERVAL="1"
SKIP_INITIAL_BUILD=false
NO_RUN=false
ONCE=false

declare -a APP_ARGS=()
APP_PID=""

log() { echo "[$(date '+%H:%M:%S')] $*"; }

usage() {
    cat <<'EOF'
Usage: ./scripts/dev_watch.sh [options] [-- <app args>]

Options:
  --build-dir <path>         CMake build directory (default: ./build)
  --target <name>            CMake target to build (default: Waive)
  --binary <path>            Override app binary path
  --poll-interval <seconds>  Poll interval when inotifywait is unavailable (default: 1)
  --skip-initial-build       Start watcher without first build+launch
  --no-run                   Build only; do not run/restart the app
  --once                     Run one build cycle and exit
  -h, --help                 Show this help

Examples:
  ./scripts/dev_watch.sh
  ./scripts/dev_watch.sh --build-dir ./build-debug --target Waive
  ./scripts/dev_watch.sh -- --screenshot /tmp/waive_shots
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --target)
            TARGET="$2"
            shift 2
            ;;
        --binary)
            BINARY_PATH="$2"
            shift 2
            ;;
        --poll-interval)
            POLL_INTERVAL="$2"
            shift 2
            ;;
        --skip-initial-build)
            SKIP_INITIAL_BUILD=true
            shift
            ;;
        --no-run)
            NO_RUN=true
            shift
            ;;
        --once)
            ONCE=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            APP_ARGS=("$@")
            break
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ ! "$POLL_INTERVAL" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "Invalid --poll-interval value: $POLL_INTERVAL" >&2
    exit 1
fi

resolve_binary() {
    if [[ -n "$BINARY_PATH" ]]; then
        echo "$BINARY_PATH"
        return 0
    fi

    local candidates=(
        "$BUILD_DIR/gui/Waive_artefacts/Release/Waive"
        "$BUILD_DIR/gui/Waive_artefacts/RelWithDebInfo/Waive"
        "$BUILD_DIR/gui/Waive_artefacts/Debug/Waive"
        "$BUILD_DIR/gui/Waive_artefacts/MinSizeRel/Waive"
    )

    local c=""
    for c in "${candidates[@]}"; do
        if [[ -x "$c" ]]; then
            echo "$c"
            return 0
        fi
    done

    local discovered=""
    discovered="$(find "$BUILD_DIR/gui/Waive_artefacts" -type f -name Waive -perm -111 2>/dev/null | head -n 1 || true)"
    if [[ -n "$discovered" ]]; then
        echo "$discovered"
        return 0
    fi

    return 1
}

stop_app() {
    if [[ -z "$APP_PID" ]]; then
        return
    fi

    if kill -0 "$APP_PID" 2>/dev/null; then
        log "Stopping Waive (PID $APP_PID)..."
        kill "$APP_PID" 2>/dev/null || true
        for _ in {1..30}; do
            if ! kill -0 "$APP_PID" 2>/dev/null; then
                break
            fi
            sleep 0.1
        done
        if kill -0 "$APP_PID" 2>/dev/null; then
            kill -9 "$APP_PID" 2>/dev/null || true
        fi
    fi

    APP_PID=""
}

launch_app() {
    if [[ "$NO_RUN" == true ]]; then
        return 0
    fi

    local binary=""
    if ! binary="$(resolve_binary)"; then
        log "Build succeeded but no Waive binary was found."
        return 1
    fi

    if [[ ! -x "$binary" ]]; then
        log "Resolved binary is not executable: $binary"
        return 1
    fi

    mkdir -p "$PROJECT_DIR/logs"
    stop_app

    log "Launching Waive..."
    nohup "$binary" "${APP_ARGS[@]}" > "$PROJECT_DIR/logs/waive_dev_watch.log" 2>&1 &
    APP_PID="$!"
    sleep 0.5

    if kill -0 "$APP_PID" 2>/dev/null; then
        log "Waive started (PID $APP_PID). Logs: logs/waive_dev_watch.log"
    else
        APP_PID=""
        log "Waive exited immediately. Check logs/waive_dev_watch.log"
    fi
}

configure_if_needed() {
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        log "Configuring CMake..."
        cmake -B "$BUILD_DIR" "$PROJECT_DIR"
    fi
}

build_project() {
    configure_if_needed
    local jobs=$(( ($(nproc) + 1) / 2 ))
    log "Building $TARGET..."
    cmake --build "$BUILD_DIR" --target "$TARGET" -j"$jobs"
}

snapshot_inputs() {
    find \
        "$PROJECT_DIR/gui" \
        "$PROJECT_DIR/engine" \
        "$PROJECT_DIR/tests" \
        "$PROJECT_DIR/tools" \
        -type f \
        \( \
            -name "*.cpp" -o -name "*.c" -o -name "*.cc" -o \
            -name "*.h" -o -name "*.hpp" -o -name "*.hh" -o \
            -name "*.cmake" -o -name "CMakeLists.txt" -o \
            -name "*.py" -o -name "*.json" \
        \) \
        -not -path "*/build/*" \
        -not -path "*/.git/*" \
        -not -path "*/logs/*" \
        -printf "%p %T@\n" 2>/dev/null \
        | sort \
        | sha256sum \
        | awk '{print $1}'
}

watch_with_inotify() {
    local watch_paths=(
        "$PROJECT_DIR/gui"
        "$PROJECT_DIR/engine"
        "$PROJECT_DIR/tests"
        "$PROJECT_DIR/tools"
        "$PROJECT_DIR/CMakeLists.txt"
        "$PROJECT_DIR/gui/CMakeLists.txt"
        "$PROJECT_DIR/tests/CMakeLists.txt"
    )
    local exclude='(^|/)(build|\.git|logs|screenshots|\.pytest_cache)(/|$)|(~$|\.swp$|\.tmp$|\.o$)'

    log "Watching with inotifywait..."
    while true; do
        inotifywait -q -r \
            -e close_write -e create -e delete -e move \
            --exclude "$exclude" \
            "${watch_paths[@]}" >/dev/null 2>&1 || true

        log "Change detected."
        if build_project; then
            launch_app || true
        else
            log "Build failed. Waiting for next change."
        fi
    done
}

watch_with_polling() {
    log "inotifywait not found; using polling every ${POLL_INTERVAL}s."
    log "Install inotify-tools for faster updates: sudo apt-get install -y inotify-tools"

    local last_snapshot=""
    last_snapshot="$(snapshot_inputs)"

    while true; do
        sleep "$POLL_INTERVAL"
        local current_snapshot=""
        current_snapshot="$(snapshot_inputs)"
        if [[ "$current_snapshot" == "$last_snapshot" ]]; then
            continue
        fi

        last_snapshot="$current_snapshot"
        log "Change detected."
        if build_project; then
            launch_app || true
        else
            log "Build failed. Waiting for next change."
        fi
    done
}

cleanup() {
    stop_app
}

trap cleanup EXIT INT TERM

if [[ "$SKIP_INITIAL_BUILD" == false ]]; then
    log "Starting initial build..."
    if build_project; then
        launch_app || true
    else
        log "Initial build failed."
        exit 1
    fi
fi

if [[ "$ONCE" == true ]]; then
    log "Done (--once)."
    exit 0
fi

if command -v inotifywait >/dev/null 2>&1; then
    watch_with_inotify
else
    watch_with_polling
fi
