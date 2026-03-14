#!/bin/sh
# E8: k-value sweep — throughput and latency for different k
# Usage: e8_k_sweep.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e8_k_sweep.csv
#
# For each k value, patch both TX and RX configs, restart containers,
# measure ping RTT (latency) and decode success rate at 20% loss.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e8_k_sweep.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"
TX_CONF="/app/config/docker-tx.conf"
RX_CONF="/app/config/docker-rx.conf"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "k,avg_rtt_ms,success_rate_20pct_loss,config"

for k_val in 1 2 4 8; do
    echo "=== k=${k_val} ==="

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 5

    # Patch k in both configs
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        sed -i "s/^k = .*/k = ${k_val}/" "$TX_CONF"
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        sed -i "s/^k = .*/k = ${k_val}/" "$RX_CONF"

    # Restart gateways via SIGHUP — but k is not hot-reloadable, need full restart.
    # Kill and let docker restart policy handle it... but containers have no restart.
    # So rebuild with patched config baked in? No — simpler: just restart containers.
    docker compose -f "$COMPOSE_FILE" restart 2>/dev/null
    sleep 6

    # Baseline RTT measurement (no loss)
    RTT_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)

    AVG_RTT=$(echo "$RTT_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $8}')
    [ -z "$AVG_RTT" ] && AVG_RTT="N/A"

    # Now inject 20% loss for decode success measurement
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc add dev eth0 root netem loss 20%
    sleep 2

    PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 40 -i 0.5 -W 2 10.0.0.2 2>&1 || true)

    LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
        for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
    }')
    [ -z "$LOSS_PCT" ] && LOSS_PCT=100
    RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")

    echo "  k=${k_val}: avg_rtt=${AVG_RTT}ms success@20%loss=${RATE}%"
    csv_row "$CSV" "$k_val" "$AVG_RTT" "$RATE" "default"

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    sleep 1
done

echo "[E8] Done. Results: $CSV"
