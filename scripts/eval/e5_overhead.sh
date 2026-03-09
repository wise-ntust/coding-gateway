#!/bin/sh
# E5: Bandwidth overhead ratio
# Usage: e5_overhead.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e5_overhead.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e5_overhead.csv"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "config,tun_rx_bytes,eth_tx_bytes,overhead_ratio"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 3

# Start iperf3 server on rx-node
docker compose -f "$COMPOSE_FILE" exec -d rx-node iperf3 -s
sleep 1

# Read TX bytes for an interface from /proc/net/dev inside tx-node
# /proc/net/dev format: "  iface: rx_bytes rx_pkts ... tx_bytes ..."
# After removing "iface:", field 1=rx_bytes, field 9=tx_bytes
read_iface_bytes() {
    _iface="$1"
    _field="$2"   # 1=rx_bytes, 9=tx_bytes
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        awk "/[[:space:]]${_iface}:/{gsub(/.*${_iface}:/,\"\"); print \$${_field}}" \
        /proc/net/dev 2>/dev/null || echo 0
}

# Snapshot byte counters before iperf3
TUN_RX_BEFORE=$(read_iface_bytes tun0 1)
ETH_TX_BEFORE=$(read_iface_bytes eth0 9)

# Run iperf3 for 30 seconds: UDP 10Mbps through TUN tunnel
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    iperf3 -c 10.0.0.2 -u -b 10M -t 30 >/dev/null 2>&1 || true

# Snapshot byte counters after iperf3
TUN_RX_AFTER=$(read_iface_bytes tun0 1)
ETH_TX_AFTER=$(read_iface_bytes eth0 9)

TUN_BYTES=$(( TUN_RX_AFTER - TUN_RX_BEFORE ))
ETH_BYTES=$(( ETH_TX_AFTER - ETH_TX_BEFORE ))

RATIO=$(awk "BEGIN {
    if ($TUN_BYTES > 0)
        printf \"%.3f\", $ETH_BYTES / $TUN_BYTES
    else
        print \"N/A\"
}")

echo "  tun_rx=${TUN_BYTES} bytes  eth_tx=${ETH_BYTES} bytes  ratio=${RATIO}"
csv_row "$CSV" "default" "$TUN_BYTES" "$ETH_BYTES" "$RATIO"

echo "[E5] Done. Results: $CSV"
