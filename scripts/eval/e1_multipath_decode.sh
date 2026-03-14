#!/bin/sh
# E1-MP: Decode success rate vs loss rate — MULTIPATH topology
# Same as E1 but uses docker-compose.multipath.yml (2 paths).
# Loss injected on BOTH paths simultaneously via tc netem.
# With k=1, n=2 (redundancy_ratio=2.0), one full copy goes on each path,
# so the block survives if at least 1 shard arrives from either path.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e1_multipath_decode.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.multipath.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "loss_pct,success_rate,config"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 8

# Warmup
if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E1-MP] FAIL: baseline connectivity check failed"
    exit 1
fi
echo "  baseline OK — starting loss sweep (multipath)"

for loss in 0 10 20 30 40 50 60 70; do
    # Apply loss on BOTH interfaces (eth0=path1, eth1=path2)
    for iface in eth0 eth1; do
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc del dev "$iface" root 2>/dev/null || true
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc add dev "$iface" root netem loss "${loss}%"
    done

    sleep 2

    PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 60 -i 0.5 -W 2 10.0.0.2 2>&1 || true)

    LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
        for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
    }')
    [ -z "$LOSS_PCT" ] && LOSS_PCT=100
    RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")

    echo "  loss=${loss}% loss_pct=${LOSS_PCT} rate=${RATE}%"
    csv_row "$CSV" "$loss" "$RATE" "multipath"
done

echo "[E1-MP] Done. Results: $CSV"
