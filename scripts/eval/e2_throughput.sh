#!/bin/sh
# E2: Effective throughput vs loss rate
# Usage: e2_throughput.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e2_throughput.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e2_throughput.csv"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "loss_pct,throughput_mbps,config"

for loss in 0 10 20 30 40 50; do
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 3

    # Start iperf3 server on rx-node (daemonized)
    docker compose -f "$COMPOSE_FILE" exec -d rx-node iperf3 -s
    sleep 1

    # Inject loss on tx-node eth0
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        tc qdisc del dev eth0 root 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        tc qdisc add dev eth0 root netem loss "${loss}%"
    sleep 1

    # Run iperf3 client through TUN tunnel: UDP, 10 Mbps, 15 seconds, JSON output
    IPERF_OUT=$(docker compose -f "$COMPOSE_FILE" exec tx-node \
        iperf3 -c 10.0.0.2 -u -b 10M -t 15 --json 2>/dev/null || echo '{}')

    # Parse receiver bits_per_second: last occurrence of "bits_per_second" in JSON
    BPS=$(echo "$IPERF_OUT" | grep -o '"bits_per_second":[0-9.e+]*' | tail -1 | \
        grep -o '[0-9.e+]*$')
    [ -z "$BPS" ] && BPS=0
    MBPS=$(awk "BEGIN { printf \"%.2f\", $BPS / 1000000 }")

    echo "  loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV" "$loss" "$MBPS" "default"

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    sleep 1
done

echo "[E2] Done. Results: $CSV"
