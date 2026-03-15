#!/bin/sh
# E12: MPTCP-equivalent comparison — multi-path with vs without FEC
# Usage: e12_mptcp_compare.sh [RESULTS_DIR] [REPS]
#
# MPTCP distributes traffic across paths but has no coding redundancy.
# When a path drops a packet, the packet is lost until TCP retransmits.
# We simulate this as: multi-path topology + redundancy_ratio=1.0 (no FEC).
#
# Comparison:
#   "mptcp_equiv" = multi-path, ratio=1.0 (distribute only, no coding)
#   "fec_2x"      = multi-path, ratio=2.0 (distribute + code)
#
# Test scenarios:
#   A) Loss on both paths (uniform degradation)
#   B) One path blocked, other alive (asymmetric failure = mmWave blockage)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e12_mptcp_compare.csv"
SUMMARY="$RESULTS_DIR/e12_mptcp_compare_summary.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.multipath.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

echo "mode,scenario,loss_pct,rep,success_rate" > "$CSV"

for ratio in 1.0 2.0; do
    if [ "$ratio" = "1.0" ]; then
        MODE="mptcp_equiv"
    else
        MODE="fec_2x"
    fi

    echo "=== ${MODE} (ratio=${ratio}) ==="

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 5

    # Patch ratio
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" /app/config/multipath-tx.conf
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" /app/config/multipath-rx.conf
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
    sleep 3

    if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
        echo "  [FAIL] ${MODE}: baseline failed"
        continue
    fi

    # Scenario A: symmetric loss on both paths
    for loss in 0 10 20 30 40; do
        for iface in eth0 eth1; do
            docker compose -f "$COMPOSE_FILE" exec -T tx-node \
                tc qdisc del dev "$iface" root 2>/dev/null || true
            docker compose -f "$COMPOSE_FILE" exec -T tx-node \
                tc qdisc add dev "$iface" root netem loss "${loss}%"
        done
        sleep 1

        rep=1
        while [ "$rep" -le "$REPS" ]; do
            PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
                ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)
            LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
                for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
            }')
            [ -z "$LOSS_PCT" ] && LOSS_PCT=100
            RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
            echo "${MODE},symmetric,${loss},${rep},${RATE}" >> "$CSV"
            rep=$((rep + 1))
        done
        echo "  [${MODE}] symmetric loss=${loss}%: done"
    done

    # Scenario B: block path1 completely, path2 alive with 20% loss
    for iface in eth0 eth1; do
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc del dev "$iface" root 2>/dev/null || true
    done
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        iptables -A INPUT -i eth0 -p udp -j DROP
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc add dev eth1 root netem loss 20%
    sleep 2

    rep=1
    while [ "$rep" -le "$REPS" ]; do
        PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)
        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
        echo "${MODE},path1_blocked,20,${rep},${RATE}" >> "$CSV"
        rep=$((rep + 1))
    done
    echo "  [${MODE}] path1 blocked + 20% on path2: done"

    # Cleanup iptables
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        iptables -D INPUT -i eth0 -p udp -j DROP 2>/dev/null || true

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
done

echo "mode,scenario,loss_pct,mean,std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2","$3
    sum[key]+=$5; sumsq[key]+=$5*$5; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if(v<0)v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d\n", k, m, s, n[k]
    }
}' "$CSV" | sort >> "$SUMMARY"

echo "[E12] Done. Summary:"
cat "$SUMMARY"
