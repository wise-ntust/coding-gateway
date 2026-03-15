#!/bin/sh
# E13: Path-count sweep — compare 2, 3, 4 paths with and without FEC
# Usage: e13_path_count_sweep.sh [RESULTS_DIR] [REPS]
#
# For each N in {2, 3, 4}:
#   mptcp_equiv = N-path topology, redundancy_ratio=1.0 (no FEC)
#   fec_2x      = N-path topology, redundancy_ratio=2.0 (FEC)
# At symmetric loss 0%, 10%, 20%, 30%, 40% — 30 reps each.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e13_path_count_sweep.csv"
SUMMARY="$RESULTS_DIR/e13_path_count_sweep_summary.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"

cleanup() {
    docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT

echo "paths,mode,loss_pct,rep,success_rate" > "$CSV"

for n_paths in 2 3 4; do
    case "$n_paths" in
        2) COMPOSE="$REPO/docker-compose.multipath.yml"
           IFACES="eth0 eth1"
           CFG_PFX="multipath" ;;
        3) COMPOSE="$REPO/docker-compose.tripath.yml"
           IFACES="eth0 eth1 eth2"
           CFG_PFX="tripath" ;;
        4) COMPOSE="$REPO/docker-compose.quadpath.yml"
           IFACES="eth0 eth1 eth2 eth3"
           CFG_PFX="quadpath" ;;
    esac
    CURRENT_COMPOSE="$COMPOSE"

    for ratio in 1.0 2.0; do
        if [ "$ratio" = "1.0" ]; then MODE="mptcp_equiv"; else MODE="fec_2x"; fi

        echo "=== ${n_paths}-path ${MODE} (ratio=${ratio}) ==="

        docker compose -f "$COMPOSE" down 2>/dev/null || true
        docker compose -f "$COMPOSE" up -d --build 2>/dev/null
        sleep 5

        docker compose -f "$COMPOSE" exec -T tx-node \
            sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" \
            "/app/config/${CFG_PFX}-tx.conf"
        docker compose -f "$COMPOSE" exec -T rx-node \
            sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" \
            "/app/config/${CFG_PFX}-rx.conf"
        docker compose -f "$COMPOSE" exec -T tx-node \
            sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
        docker compose -f "$COMPOSE" exec -T rx-node \
            sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
        sleep 3

        if ! docker compose -f "$COMPOSE" exec -T tx-node \
                ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
            echo "  [FAIL] ${n_paths}p ${MODE}: baseline failed"
            docker compose -f "$COMPOSE" down 2>/dev/null || true
            continue
        fi

        for loss in 0 10 20 30 40; do
            for iface in $IFACES; do
                docker compose -f "$COMPOSE" exec -T tx-node \
                    tc qdisc del dev "$iface" root 2>/dev/null || true
                docker compose -f "$COMPOSE" exec -T tx-node \
                    tc qdisc add dev "$iface" root netem loss "${loss}%"
            done
            sleep 1

            rep=1
            while [ "$rep" -le "$REPS" ]; do
                PING_OUT=$(docker compose -f "$COMPOSE" exec -T tx-node \
                    ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)
                LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
                    for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
                }')
                [ -z "$LOSS_PCT" ] && LOSS_PCT=100
                RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
                echo "${n_paths},${MODE},${loss},${rep},${RATE}" >> "$CSV"
                rep=$((rep + 1))
            done
            echo "  [${n_paths}p ${MODE}] loss=${loss}%: ${REPS} reps done"
        done

        docker compose -f "$COMPOSE" down 2>/dev/null || true
    done
done

echo "paths,mode,loss_pct,mean,std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2","$3
    sum[key]+=$5; sumsq[key]+=$5*$5; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if(v<0)v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n -k2 -k3 -n >> "$SUMMARY"

echo "[E13] Done. Summary:"
cat "$SUMMARY"
