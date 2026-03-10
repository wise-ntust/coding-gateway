#!/bin/sh
# E2: Effective throughput vs loss rate
# Usage: e2_throughput.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e2_throughput.csv
#
# Strategy: measure throughput from iperf3 SERVER-SIDE JSON, which avoids
# the iperf3 client SIGSEGV bug on aarch64/Alpine. The server writes JSON to
# /tmp/server.json inside the rx-node container. We then read the first valid
# interval's bits_per_second as the throughput measurement.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e2_throughput.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "loss_pct,throughput_mbps,config"

for loss in 0 10 20 30 40 50; do
    # Fresh containers for each measurement to reset rx_window state
    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 8

    # Start iperf3 server on rx-node bound to IPv4 TUN address,
    # writing JSON results to file (server-side JSON is stable even if client crashes)
    docker compose -f "$COMPOSE_FILE" exec -T rx-node sh -c \
        'pkill iperf3 2>/dev/null; iperf3 -s -B 10.0.0.2 --json > /tmp/server.json 2>&1 &
         sleep 1; echo "server ready: $(pgrep iperf3 && echo yes || echo NO)"'
    sleep 1

    # Inject loss on tx-node eth0
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc del dev eth0 root 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc add dev eth0 root netem loss "${loss}%"
    sleep 1

    # Run iperf3 client through TUN tunnel: TCP, 5 seconds
    # Client may crash (SIGSEGV on aarch64 Alpine) but sends data first;
    # we capture results from the server side instead.
    docker compose -f "$COMPOSE_FILE" exec -T tx-node sh -c \
        'iperf3 -c 10.0.0.2 -t 5 > /tmp/client.txt 2>&1; echo "client done exit:$?"' \
        2>/dev/null || true
    sleep 2

    # Read server-side JSON: first non-zero bits_per_second value
    BPS=$(docker compose -f "$COMPOSE_FILE" exec -T rx-node sh -c '
        JSON=/tmp/server.json
        [ -f "$JSON" ] || { echo 0; exit; }
        grep "bits_per_second" "$JSON" | \
            awk -F"[:\t ]" "{for(i=1;i<=NF;i++) if(\$i+0>0){print \$i+0; exit}}" | \
            head -1
    ' 2>/dev/null)
    [ -z "$BPS" ] && BPS=0

    MBPS=$(awk "BEGIN { printf \"%.2f\", $BPS / 1000000 }")
    echo "  loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV" "$loss" "$MBPS" "default"
done

echo "[E2] Done. Results: $CSV"
