#!/bin/sh
# E15: Multi-path blockage recovery — 3-path and 4-path topologies
# Usage: e15_blockage_recovery.sh [RESULTS_DIR]
#
# Extends E3-MP (2-path) to 3 and 4 paths.
# Blocks path1 (eth0) at rx-node for each blockage duration;
# N-1 paths remain alive. Measures lost_pings and effective gap_ms.
# With FEC and N-1 live paths, expect 0 ms recovery gap.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e15_blockage_recovery.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"

CURRENT_COMPOSE=""
cleanup() {
    [ -n "$CURRENT_COMPOSE" ] && docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT

echo "paths,blockage_ms,lost_pings,gap_ms" > "$CSV"

run_blockage_test() {
    BT_COMPOSE="$1"
    BT_N="$2"
    CURRENT_COMPOSE="$BT_COMPOSE"

    echo "=== ${BT_N}-path blockage recovery ==="

    for blockage_ms in 50 100 200 500; do
        docker compose -f "$BT_COMPOSE" down 2>/dev/null || true
        docker compose -f "$BT_COMPOSE" up -d --build 2>/dev/null
        sleep 8

        if ! docker compose -f "$BT_COMPOSE" exec -T tx-node \
                ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
            echo "  [SKIP] ${BT_N}p blockage=${blockage_ms}ms: baseline failed"
            docker compose -f "$BT_COMPOSE" down 2>/dev/null || true
            continue
        fi

        PING_LOG=$(mktemp)
        docker compose -f "$BT_COMPOSE" exec -T tx-node \
            ping -c 100 -i 0.1 -W 1 10.0.0.2 > "$PING_LOG" 2>&1 &
        PING_PID=$!
        sleep 1

        docker compose -f "$BT_COMPOSE" exec -T rx-node \
            iptables -A INPUT -i eth0 -p udp -j DROP

        sleep "$(awk "BEGIN { printf \"%.3f\", $blockage_ms / 1000 }")"

        docker compose -f "$BT_COMPOSE" exec -T rx-node \
            iptables -D INPUT -i eth0 -p udp -j DROP 2>/dev/null || true

        wait "$PING_PID" 2>/dev/null || true
        PING_OUT=$(cat "$PING_LOG")
        rm -f "$PING_LOG"

        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        # ping -c 100 so loss% == lost packet count; GAP_MS = lost packets * 100ms interval
        LOST_PINGS=$LOSS_PCT
        GAP_MS=$((LOST_PINGS * 100))

        echo "  ${BT_N}p blockage=${blockage_ms}ms lost=${LOST_PINGS} gap=${GAP_MS}ms"
        echo "${BT_N},${blockage_ms},${LOST_PINGS},${GAP_MS}" >> "$CSV"

        docker compose -f "$BT_COMPOSE" down 2>/dev/null || true
        sleep 1
    done
}

run_blockage_test "$REPO/docker-compose.tripath.yml"  3
run_blockage_test "$REPO/docker-compose.quadpath.yml" 4

echo "[E15] Done. Results: $CSV"
cat "$CSV"
