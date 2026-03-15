#!/bin/sh
# E14: Path degradation sweep — 3-path and 4-path topologies
# Usage: e14_path_degradation.sh [RESULTS_DIR] [REPS]
#
# For each topology (3-path, 4-path):
#   Block paths one at a time. At each stage, measure success rate
#   with 30% loss on the remaining alive paths (30 reps).
# Extends E10 (which only covered 2-path).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e14_path_degradation.csv"
SUMMARY="$RESULTS_DIR/e14_path_degradation_summary.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"

CURRENT_COMPOSE=""
cleanup() {
    [ -n "$CURRENT_COMPOSE" ] && docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT

echo "paths,alive_paths,loss_on_alive,rep,success_rate" > "$CSV"

measure_success() {
    # $1 = compose file, $2 = n_paths (total), $3 = alive_count
    MS_COMPOSE="$1"
    MS_N="$2"
    MS_ALIVE="$3"
    MS_REP=1

    while [ "$MS_REP" -le "$REPS" ]; do
        PING_OUT=$(docker compose -f "$MS_COMPOSE" exec -T tx-node \
            ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)
        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
        echo "${MS_N},${MS_ALIVE},30,${MS_REP},${RATE}" >> "$CSV"
        MS_REP=$((MS_REP + 1))
    done
    echo "  [${MS_N}p] alive=${MS_ALIVE}: ${REPS} reps done"
}

# ---- 3-path topology ----
COMPOSE="$REPO/docker-compose.tripath.yml"
CURRENT_COMPOSE="$COMPOSE"
echo "=== 3-path degradation ==="

docker compose -f "$COMPOSE" down 2>/dev/null || true
docker compose -f "$COMPOSE" up -d --build 2>/dev/null
sleep 6

if ! docker compose -f "$COMPOSE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E14] FAIL: 3-path baseline"
else
    for iface in eth0 eth1 eth2; do
        docker compose -f "$COMPOSE" exec -T tx-node \
            tc qdisc del dev "$iface" root 2>/dev/null || true
        docker compose -f "$COMPOSE" exec -T tx-node \
            tc qdisc add dev "$iface" root netem loss 30%
    done
    sleep 2
    measure_success "$COMPOSE" 3 3

    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth2 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth2 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 3 2

    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth1 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth1 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 3 1

    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth0 -p udp -j DROP
    sleep 2
    measure_success "$COMPOSE" 3 0
fi
docker compose -f "$COMPOSE" down 2>/dev/null || true

# ---- 4-path topology ----
COMPOSE="$REPO/docker-compose.quadpath.yml"
CURRENT_COMPOSE="$COMPOSE"
echo "=== 4-path degradation ==="

docker compose -f "$COMPOSE" down 2>/dev/null || true
docker compose -f "$COMPOSE" up -d --build 2>/dev/null
sleep 6

if ! docker compose -f "$COMPOSE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E14] FAIL: 4-path baseline"
else
    for iface in eth0 eth1 eth2 eth3; do
        docker compose -f "$COMPOSE" exec -T tx-node \
            tc qdisc del dev "$iface" root 2>/dev/null || true
        docker compose -f "$COMPOSE" exec -T tx-node \
            tc qdisc add dev "$iface" root netem loss 30%
    done
    sleep 2
    measure_success "$COMPOSE" 4 4

    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth3 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth3 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 4 3

    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth2 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth2 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 4 2

    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth1 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth1 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 4 1

    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth0 -p udp -j DROP
    sleep 2
    measure_success "$COMPOSE" 4 0
fi
docker compose -f "$COMPOSE" down 2>/dev/null || true

echo "paths,alive_paths,mean,std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2
    sum[key]+=$5; sumsq[key]+=$5*$5; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if(v<0)v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n -k2 -n >> "$SUMMARY"

echo "[E14] Done. Summary:"
cat "$SUMMARY"
