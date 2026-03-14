#!/bin/sh
# E3-MP: Blockage recovery latency — MULTIPATH topology
# Same as E3 but uses docker-compose.multipath.yml (2 paths).
# Blockage is applied to path1 (eth0) only — path2 (eth1) remains live.
# With coding redundancy, shards on path2 should sustain connectivity
# even while path1 is blocked.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e3_multipath_blockage.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.multipath.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "blockage_ms,lost_pings,gap_ms,config"

for blockage_ms in 50 100 200 500; do
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 8

    # Verify baseline
    if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
        echo "  [SKIP] blockage=${blockage_ms}ms: baseline failed"
        docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
        continue
    fi

    # Start pings in background
    PING_LOG=$(mktemp)
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 100 -i 0.1 -W 1 10.0.0.2 > "$PING_LOG" 2>&1 &
    PING_PID=$!
    sleep 1

    # Block path1 only (eth0 on rx-node); path2 (eth1) stays open
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        iptables -A INPUT -i eth0 -p udp -j DROP

    sleep "$(awk "BEGIN { printf \"%.3f\", $blockage_ms / 1000 }")"

    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        iptables -D INPUT -i eth0 -p udp -j DROP 2>/dev/null || true

    wait "$PING_PID" 2>/dev/null || true
    PING_OUT=$(cat "$PING_LOG")
    rm -f "$PING_LOG"

    LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
        for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
    }')
    [ -z "$LOSS_PCT" ] && LOSS_PCT=100

    LOST_PINGS=$(( LOSS_PCT ))
    GAP_MS=$(( LOST_PINGS * 100 ))

    echo "  blockage=${blockage_ms}ms lost_pings=${LOST_PINGS} gap=${GAP_MS}ms"
    csv_row "$CSV" "$blockage_ms" "$LOST_PINGS" "$GAP_MS" "multipath"

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    sleep 1
done

echo "[E3-MP] Done. Results: $CSV"
