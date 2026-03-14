#!/bin/sh
# E9: Tunnel latency overhead — direct vs tunneled ping RTT
# Usage: e9_tunnel_latency.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e9_tunnel_latency.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e9_tunnel_latency.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "mode,avg_rtt_ms,min_rtt_ms,max_rtt_ms,config"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 5

# Direct ping (Docker bridge, no tunnel)
DIRECT_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    ping -c 50 -i 0.1 -W 2 172.20.0.3 2>&1 || true)

DIRECT_AVG=$(echo "$DIRECT_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $8}')
DIRECT_MIN=$(echo "$DIRECT_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $7}')
DIRECT_MAX=$(echo "$DIRECT_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $9}')
[ -z "$DIRECT_AVG" ] && DIRECT_AVG="N/A"
[ -z "$DIRECT_MIN" ] && DIRECT_MIN="N/A"
[ -z "$DIRECT_MAX" ] && DIRECT_MAX="N/A"

echo "  direct: avg=${DIRECT_AVG}ms min=${DIRECT_MIN}ms max=${DIRECT_MAX}ms"
csv_row "$CSV" "direct" "$DIRECT_AVG" "$DIRECT_MIN" "$DIRECT_MAX" "default"

# Tunneled ping (through TUN + encode/decode)
TUNNEL_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    ping -c 50 -i 0.1 -W 2 10.0.0.2 2>&1 || true)

TUNNEL_AVG=$(echo "$TUNNEL_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $8}')
TUNNEL_MIN=$(echo "$TUNNEL_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $7}')
TUNNEL_MAX=$(echo "$TUNNEL_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $9}')
[ -z "$TUNNEL_AVG" ] && TUNNEL_AVG="N/A"
[ -z "$TUNNEL_MIN" ] && TUNNEL_MIN="N/A"
[ -z "$TUNNEL_MAX" ] && TUNNEL_MAX="N/A"

echo "  tunnel: avg=${TUNNEL_AVG}ms min=${TUNNEL_MIN}ms max=${TUNNEL_MAX}ms"
csv_row "$CSV" "tunnel" "$TUNNEL_AVG" "$TUNNEL_MIN" "$TUNNEL_MAX" "default"

# Compute overhead
if [ "$DIRECT_AVG" != "N/A" ] && [ "$TUNNEL_AVG" != "N/A" ]; then
    OVERHEAD=$(awk "BEGIN { printf \"%.3f\", $TUNNEL_AVG - $DIRECT_AVG }")
    echo "  overhead: ${OVERHEAD}ms"
    csv_row "$CSV" "overhead" "$OVERHEAD" "0" "0" "default"
fi

echo "[E9] Done. Results: $CSV"
