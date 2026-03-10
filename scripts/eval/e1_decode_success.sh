#!/bin/sh
# E1: Decode success rate vs loss rate
# Usage: e1_decode_success.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e1_decode_success.csv
# Strategy: start containers once, sweep loss levels without restarting.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e1_decode_success.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "loss_pct,success_rate,config"

# Start containers once and wait for gateway to be fully ready
docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 8

# Warmup: verify baseline connectivity before sweeping
if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E1] FAIL: baseline connectivity check failed after 8s warmup"
    exit 1
fi
echo "  baseline OK — starting loss sweep"

for loss in 0 10 20 30 40 50 60 70; do
    # Update netem loss without restarting containers
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc del dev eth0 root 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc add dev eth0 root netem loss "${loss}%"

    sleep 2

    # 60 pings at 500ms interval = 30s measurement window
    PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 60 -i 0.5 -W 2 10.0.0.2 2>&1 || true)

    # Parse loss% — awk handles both integer "10%" and decimal "3.33333%"
    LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
        for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
    }')
    [ -z "$LOSS_PCT" ] && LOSS_PCT=100
    RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")

    echo "  loss=${loss}% loss_pct=${LOSS_PCT} rate=${RATE}%"
    csv_row "$CSV" "$loss" "$RATE" "default"
done

echo "[E1] Done. Results: $CSV"
