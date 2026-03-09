#!/bin/sh
# E3: Blockage recovery latency
# Usage: e3_blockage_recovery.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e3_blockage_recovery.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e3_blockage_recovery.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "blockage_ms,lost_pings,gap_ms,config"

for blockage_ms in 50 100 200 500; do
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 3

    # Verify baseline connectivity
    if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
        echo "  [SKIP] blockage=${blockage_ms}ms: baseline failed"
        docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
        continue
    fi

    # Start 100 pings in background; let a few succeed before blocking
    PING_LOG=$(mktemp)
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 100 -i 0.1 -W 1 10.0.0.2 > "$PING_LOG" 2>&1 &
    PING_PID=$!
    sleep 1

    # Apply blockage mid-stream
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        iptables -A INPUT -i eth0 -j DROP

    # Hold for blockage_ms, then unblock
    sleep "$(awk "BEGIN { printf \"%.3f\", $blockage_ms / 1000 }")"
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        iptables -D INPUT -i eth0 -j DROP 2>/dev/null || true

    # Wait for ping to finish
    wait "$PING_PID" 2>/dev/null || true
    PING_OUT=$(cat "$PING_LOG")
    rm -f "$PING_LOG"

    # Parse packet loss % (works on BusyBox and GNU ping)
    LOSS_PCT=$(echo "$PING_OUT" | grep -o '[0-9]*% packet loss' | grep -o '^[0-9]*')
    [ -z "$LOSS_PCT" ] && LOSS_PCT=100

    # lost_pings = loss% of 100 total; gap_ms = lost_pings × 100ms interval
    LOST_PINGS=$(( LOSS_PCT ))
    GAP_MS=$(( LOST_PINGS * 100 ))

    echo "  blockage=${blockage_ms}ms lost_pings=${LOST_PINGS} gap=${GAP_MS}ms"
    csv_row "$CSV" "$blockage_ms" "$LOST_PINGS" "$GAP_MS" "default"

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    sleep 1
done

echo "[E3] Done. Results: $CSV"
