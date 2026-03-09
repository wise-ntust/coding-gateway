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

for loss in 0 10 20 30 40 50 60 70; do
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 3

    # Inject loss on tx-node eth0: delete existing root qdisc then add netem
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        tc qdisc del dev eth0 root 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        tc qdisc add dev eth0 root netem loss "${loss}%"

    sleep 1

    # 200 pings; capture output even on non-zero exit
    PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec tx-node \
        ping -c 200 -W 2 10.0.0.2 2>&1 || true)

    # Parse received count — handles both BusyBox and GNU ping formats
    RECV=$(echo "$PING_OUT" | awk '/received/{for(i=1;i<=NF;i++) if($i=="received") {print $(i-1); exit}}')
    [ -z "$RECV" ] && RECV=0

    # success_rate = received / 200 * 100
    RATE=$(awk "BEGIN { printf \"%.1f\", ($RECV / 200.0) * 100 }")

    echo "  loss=${loss}% config=default recv=${RECV}/200 rate=${RATE}%"
    csv_row "$CSV" "$loss" "$RATE" "default"

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    sleep 1
done

echo "[E1] Done. Results: $CSV"
