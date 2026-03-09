#!/bin/sh
# E4: Adaptive strategy step response
# Usage: e4_adaptive_step.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e4_adaptive_step.csv (injection events)
#         RESULTS_DIR/e4_gateway.log (gateway stderr for redundancy_ratio lines)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e4_adaptive_step.csv"
LOG="$RESULTS_DIR/e4_gateway.log"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() {
    kill "$LOG_PID" 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
}
trap cleanup EXIT

csv_header "$CSV" "elapsed_s,loss_pct,injected"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 3

# Capture gateway logs in background
docker compose -f "$COMPOSE_FILE" logs -f tx-node > "$LOG" 2>&1 &
LOG_PID=$!

T_START=$(date +%s)

# Step sequence: loss_pct:duration_s
# 0%→30s, 40%→30s, 0%→30s, 70%→30s, 0%→30s
for step in "0:30" "40:30" "0:30" "70:30" "0:30"; do
    loss="${step%%:*}"
    dur="${step#*:}"
    elapsed=$(( $(date +%s) - T_START ))

    echo "  t=${elapsed}s: setting loss=${loss}%"

    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc del dev eth0 root 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc add dev eth0 root netem loss "${loss}%" 2>/dev/null || true

    csv_row "$CSV" "$elapsed" "$loss" "1"
    sleep "$dur"
done

kill "$LOG_PID" 2>/dev/null || true

echo "[E4] Done."
echo "[E4] Step injection CSV: $CSV"
echo "[E4] Gateway log: $LOG"
grep "redundancy_ratio" "$LOG" | head -20 || echo "  (no redundancy_ratio lines in log)"
