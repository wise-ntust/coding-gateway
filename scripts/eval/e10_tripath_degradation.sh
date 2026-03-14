#!/bin/sh
# E10: Three-path graceful degradation — block paths one at a time
# Usage: e10_tripath_degradation.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e10_tripath_degradation.csv
#
# Uses docker-compose.multipath.yml (2 paths). Measures decode success
# with 0, 1, or 2 paths blocked while injecting 30% loss on remaining.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e10_tripath_degradation.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.multipath.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "alive_paths,loss_on_alive,success_rate,config"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 8

if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E10] FAIL: baseline"
    exit 1
fi
echo "  baseline OK"

# Scenario 1: Both paths alive, 30% loss on each
echo "=== 2 paths alive, 30% loss ==="
for iface in eth0 eth1; do
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc del dev "$iface" root 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc add dev "$iface" root netem loss 30%
done
sleep 2

PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    ping -c 40 -i 0.5 -W 2 10.0.0.2 2>&1 || true)
LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}}')
[ -z "$LOSS_PCT" ] && LOSS_PCT=100
RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
echo "  2 paths: success=${RATE}%"
csv_row "$CSV" "2" "30" "$RATE" "multipath"

# Scenario 2: Block path1, path2 alive with 30% loss
echo "=== 1 path alive (path1 blocked), 30% loss ==="
docker compose -f "$COMPOSE_FILE" exec -T rx-node \
    iptables -A INPUT -i eth0 -p udp -j DROP
# Remove loss from blocked path, keep on alive
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    tc qdisc del dev eth0 root 2>/dev/null || true
sleep 2

PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    ping -c 40 -i 0.5 -W 2 10.0.0.2 2>&1 || true)
LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}}')
[ -z "$LOSS_PCT" ] && LOSS_PCT=100
RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
echo "  1 path: success=${RATE}%"
csv_row "$CSV" "1" "30" "$RATE" "multipath"

# Scenario 3: Block both paths
echo "=== 0 paths alive (both blocked) ==="
docker compose -f "$COMPOSE_FILE" exec -T rx-node \
    iptables -A INPUT -i eth1 -p udp -j DROP
sleep 2

PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    ping -c 10 -i 0.5 -W 1 10.0.0.2 2>&1 || true)
LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}}')
[ -z "$LOSS_PCT" ] && LOSS_PCT=100
RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
echo "  0 paths: success=${RATE}%"
csv_row "$CSV" "0" "30" "$RATE" "multipath"

echo "[E10] Done. Results: $CSV"
