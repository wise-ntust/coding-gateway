#!/bin/sh
# E3: Blockage recovery latency
# Usage: e3_blockage_recovery.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e3_blockage_recovery.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e3_blockage_recovery.csv"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

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

    # Block all traffic on rx-node eth0
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        iptables -A INPUT -i eth0 -j DROP

    # Measure: run 100 pings at 100ms interval while blocked, count lost
    PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 100 -i 0.1 -W 1 10.0.0.2 2>&1 || true)

    # Wait for blockage_ms then unblock
    sleep "$(awk "BEGIN { printf \"%.3f\", $blockage_ms / 1000 }")"
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        iptables -D INPUT -i eth0 -j DROP 2>/dev/null || true

    # Parse packet loss % from ping output (works on BusyBox and GNU ping)
    LOSS_PCT=$(echo "$PING_OUT" | grep -o '[0-9]*% packet loss' | grep -o '^[0-9]*')
    [ -z "$LOSS_PCT" ] && LOSS_PCT=100

    # Approximate gap: loss% of 100 pings × 100ms interval
    LOST=$(awk "BEGIN { printf \"%d\", $LOSS_PCT }")
    GAP_MS=$(awk "BEGIN { printf \"%d\", $LOST * 100 }")

    echo "  blockage=${blockage_ms}ms lost=${LOST}% gap=${GAP_MS}ms"
    csv_row "$CSV" "$blockage_ms" "$LOST" "$GAP_MS" "default"

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    sleep 1
done

echo "[E3] Done. Results: $CSV"
