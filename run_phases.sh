#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# run_phases.sh — Sequentially run zeroshot phases
#
# Usage:
#   ./run_phases.sh [start_phase]    # default: auto-detect from state file
#
# The script:
#   1. Runs `zeroshot run <phase.md> -d` for each phase
#   2. Polls cluster status + file activity — if no file changes for
#      STALE_MINUTES, stops the cluster and moves on
#   3. Runs build + tests after each phase
#   4. Commits the phase's work and moves to the next phase
#
# State is tracked in .phase_state so you can stop/restart safely.
# =============================================================================

PHASES_DIR="docs/zeroshot_phases_final"
STATE_FILE=".phase_state"
PHASE_LOG_DIR="logs/phases"
STALE_MINUTES="${STALE_MINUTES:-8}"      # stop after N minutes of no file changes
PHASE_TIMEOUT="${PHASE_TIMEOUT:-45}"     # hard timeout per phase in minutes
POLL_INTERVAL="${POLL_INTERVAL:-30}"     # seconds between status checks

# Ordered phase files (v3 — post-second-audit)
PHASE_FILES=(
    "phase_01_security_hardening.md"
    "phase_02_ux_foundations.md"
    "phase_03_core_missing_features.md"
    "phase_04_performance_optimization.md"
)

mkdir -p "$PHASE_LOG_DIR"

# ── Helpers ──────────────────────────────────────────────────────────────────

log() { echo "[$(date '+%H:%M:%S')] $*"; }
die() { log "FATAL: $*"; exit 1; }

# Compute a safe -j value: min(nproc/2, available_ram / 2GB, MAX_BUILD_JOBS)
# JUCE/Tracktion template-heavy C++ can use 1-2GB per compiler process.
MAX_BUILD_JOBS="${MAX_BUILD_JOBS:-12}"
safe_jobs() {
    local cores
    cores=$(nproc)
    local half_cores=$(( cores / 2 ))
    (( half_cores < 1 )) && half_cores=1

    # Memory-based limit: ~2GB per compiler process
    local avail_mb
    avail_mb=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo 2>/dev/null || echo "0")
    local mem_jobs=4  # fallback
    if (( avail_mb > 0 )); then
        mem_jobs=$(( avail_mb / 2048 ))
        (( mem_jobs < 1 )) && mem_jobs=1
    fi

    # Take the minimum of all limits
    local jobs=$half_cores
    (( mem_jobs < jobs )) && jobs=$mem_jobs
    (( MAX_BUILD_JOBS < jobs )) && jobs=$MAX_BUILD_JOBS
    (( jobs < 1 )) && jobs=1

    echo "$jobs"
}

get_current_phase() {
    if [[ -f "$STATE_FILE" ]]; then
        cat "$STATE_FILE"
    else
        echo "1"
    fi
}

save_phase() {
    echo "$1" > "$STATE_FILE"
}

# Snapshot of tracked-file modification times (md5 of stat output)
file_fingerprint() {
    git ls-files -m --others --exclude-standard 2>/dev/null \
        | head -200 \
        | xargs -r stat --format='%n %Y' 2>/dev/null \
        | md5sum \
        | awk '{print $1}'
}

# Get cluster state from `zeroshot status` (text output)
get_cluster_state() {
    local cluster_id=$1
    zeroshot status "$cluster_id" 2>/dev/null \
        | grep -oP '^State:\s+\K\S+' \
        || echo "unknown"
}

# ── Run one phase ────────────────────────────────────────────────────────────

run_phase() {
    local phase_num=$1
    local phase_file="${PHASE_FILES[$((phase_num - 1))]}"
    local phase_path="$PHASES_DIR/$phase_file"
    local logfile="$PHASE_LOG_DIR/phase_$(printf '%02d' "$phase_num").log"

    if [[ ! -f "$phase_path" ]]; then
        die "Phase file not found: $phase_path"
    fi

    log "═══════════════════════════════════════════════════════════════"
    log "  PHASE $phase_num: $phase_file"
    log "  Timeout: ${PHASE_TIMEOUT}m | Stale kill: ${STALE_MINUTES}m"
    log "═══════════════════════════════════════════════════════════════"

    # Launch zeroshot in detached mode and capture cluster ID
    local run_output
    run_output=$(zeroshot run "$phase_path" -d 2>&1)
    echo "$run_output"

    local cluster_id
    cluster_id=$(echo "$run_output" | grep -oP '^Started \K\S+') || true

    if [[ -z "$cluster_id" ]]; then
        log "ERROR: Could not extract cluster ID from zeroshot output:"
        log "$run_output"
        return 1
    fi

    log "Cluster started: $cluster_id"
    log "Logs: zeroshot logs $cluster_id -f"
    echo "$cluster_id" > "$PHASE_LOG_DIR/phase_$(printf '%02d' "$phase_num").cluster_id"

    # Stream logs to file in background
    zeroshot logs "$cluster_id" -f > "$logfile" 2>&1 &
    local logs_pid=$!

    # ── Poll loop: wait for completion or staleness ──
    local stale_seconds=$((STALE_MINUTES * 60))
    local timeout_seconds=$((PHASE_TIMEOUT * 60))
    local start_time
    start_time=$(date +%s)
    local last_fp
    last_fp=$(file_fingerprint)
    local last_change=$start_time

    while true; do
        sleep "$POLL_INTERVAL"

        local now
        now=$(date +%s)
        local elapsed=$(( now - start_time ))

        # Check if zeroshot finished on its own
        local state
        state=$(get_cluster_state "$cluster_id")

        if [[ "$state" != "running" ]]; then
            log "Cluster $cluster_id finished naturally (state: $state)"
            break
        fi

        # Check file activity
        local current_fp
        current_fp=$(file_fingerprint)

        if [[ "$current_fp" != "$last_fp" ]]; then
            last_fp="$current_fp"
            last_change=$now
            local mins=$((elapsed / 60))
            log "  [${mins}m] Activity detected, resetting stale timer"
        fi

        local idle=$(( now - last_change ))

        # Stale check
        if (( idle >= stale_seconds )); then
            log "No file changes for ${STALE_MINUTES}m — stopping cluster"
            zeroshot stop "$cluster_id" 2>&1 || true
            break
        fi

        # Hard timeout
        if (( elapsed >= timeout_seconds )); then
            log "Hard timeout (${PHASE_TIMEOUT}m) — stopping cluster"
            zeroshot stop "$cluster_id" 2>&1 || true
            break
        fi

        # Status line
        local idle_mins=$((idle / 60))
        local elapsed_mins=$((elapsed / 60))
        log "  [${elapsed_mins}m elapsed, ${idle_mins}m idle] cluster: $state"
    done

    # Clean up log streamer
    kill "$logs_pid" 2>/dev/null || true
    wait "$logs_pid" 2>/dev/null || true

    log "Phase $phase_num cluster done."
    return 0
}

# ── Post-phase validation ────────────────────────────────────────────────────

validate_phase() {
    local phase_num=$1
    log "Running build + tests for phase $phase_num..."

    local jobs
    jobs=$(safe_jobs)
    log "Building with -j$jobs (cores: $(nproc), max: $MAX_BUILD_JOBS)"

    if cmake --build build --target Waive -j"$jobs" 2>&1 | tail -5; then
        log "Build: PASSED"
    else
        log "Build: FAILED (continuing anyway — next phase may fix it)"
    fi

    if ctest --test-dir build --output-on-failure 2>&1 | tail -20; then
        log "Tests: PASSED"
    else
        log "Tests: SOME FAILURES (continuing)"
    fi
}

commit_phase() {
    local phase_num=$1
    local phase_file="${PHASE_FILES[$((phase_num - 1))]}"

    if git diff --quiet && git diff --cached --quiet && [[ -z "$(git ls-files --others --exclude-standard)" ]]; then
        log "No changes to commit for phase $phase_num"
        return 0
    fi

    git add -A
    git commit --no-gpg-sign -m "$(cat <<EOF
Phase $phase_num: ${phase_file%.md}

Automated zeroshot implementation via run_phases.sh
EOF
    )" || log "Commit failed (maybe nothing to commit)"
}

# ── Main loop ────────────────────────────────────────────────────────────────

main() {
    local start_phase="${1:-$(get_current_phase)}"
    local total=${#PHASE_FILES[@]}

    if (( start_phase > total )); then
        log "All $total phases already complete!"
        exit 0
    fi

    log "Starting from phase $start_phase of $total"
    log "Config: STALE_MINUTES=$STALE_MINUTES PHASE_TIMEOUT=${PHASE_TIMEOUT}m POLL_INTERVAL=${POLL_INTERVAL}s"
    echo ""

    for (( i = start_phase; i <= total; i++ )); do
        save_phase "$i"
        run_phase "$i"
        validate_phase "$i"
        commit_phase "$i"

        if (( i < total )); then
            log ""
            log "Phase $i done. Starting phase $((i + 1)) in 10 seconds... (Ctrl+C to stop)"
            sleep 10
        fi
    done

    save_phase "$(( total + 1 ))"
    log ""
    log "All $total phases complete!"
}

main "$@"
