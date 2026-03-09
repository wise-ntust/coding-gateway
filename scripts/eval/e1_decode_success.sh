#!/bin/sh
# E1: Decode success rate vs loss rate
# Usage: e1_decode_success.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e1_decode_success.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e1_decode_success.csv"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "loss_pct,success_rate,config"

# Configs: label:k:ratio pairs (labels only; actual coding params controlled by config files)
CONFIGS="coding_k2r15 coding_k4r15 no_coding"

for loss in 0 10 20 30 40 50 60 70; do
    for cfg in $CONFIGS; do
        docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
        sleep 3

        # Inject loss on tx-node eth0 (replace if qdisc exists, add if not)
        docker compose -f "$COMPOSE_FILE" exec tx-node \
            tc qdisc replace dev eth0 root netem loss "${loss}%" 2>/dev/null || \
        docker compose -f "$COMPOSE_FILE" exec tx-node \
            tc qdisc add dev eth0 root netem loss "${loss}%" 2>/dev/null || true

        sleep 1

        # 200 pings; capture output even on non-zero exit
        PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec tx-node \
            ping -c 200 -W 2 10.0.0.2 2>&1 || true)

        # Parse "X received" from ping statistics
        RECV=$(echo "$PING_OUT" | grep -o '[0-9]* received' | grep -o '^[0-9]*')
        [ -z "$RECV" ] && RECV=0

        # success_rate = received / 200 * 100
        RATE=$(awk "BEGIN { printf \"%.1f\", ($RECV / 200.0) * 100 }")

        echo "  loss=${loss}% config=${cfg} recv=${RECV}/200 rate=${RATE}%"
        csv_row "$CSV" "$loss" "$RATE" "$cfg"

        docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
        sleep 1
    done
done

echo "[E1] Done. Results: $CSV"
