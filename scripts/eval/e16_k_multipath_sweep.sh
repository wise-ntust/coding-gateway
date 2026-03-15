#!/bin/sh
# E16: k-sweep on multi-path — k ∈ {1,2,4} × N ∈ {2,3,4} × ratio=2.0
# Usage: e16_k_multipath_sweep.sh [RESULTS_DIR] [REPS]
#
# Two scenarios per (k, N):
#   A) Symmetric loss: all paths at {0,20,30,40}% loss — 30 reps
#   B) Asymmetric: path0 at 0% loss, all other paths at 30% loss — 30 reps
#
# Hypothesis: with ratio=2.0, each block generates k*2 shards.
# When k*2 >= N, every block has at least one shard on every path,
# so the 0%-loss path in scenario B guarantees decode.
#
#   k=1 (2 shards): N=2 covered, N=3/4 not fully covered → not 100%
#   k=2 (4 shards): N=2/3/4 all covered → 100% with 0%-loss path
#   k=4 (8 shards): N=2/3/4 all covered with 2 shards/path → 100%

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e16_k_multipath_sweep.csv"
SUMMARY="$RESULTS_DIR/e16_k_multipath_sweep_summary.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"

CURRENT_COMPOSE=""
cleanup() {
    [ -n "$CURRENT_COMPOSE" ] && docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT

echo "k,paths,scenario,loss_pct,rep,success_rate" > "$CSV"

for k_val in 1 2 4; do
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

        echo "=== k=${k_val}, ${n_paths}-path ==="

        docker compose -f "$COMPOSE" down 2>/dev/null || true
        docker compose -f "$COMPOSE" up -d --build 2>/dev/null
        sleep 5

        # k is not hot-reloadable — patch config and restart
        docker compose -f "$COMPOSE" exec -T tx-node \
            sed -i "s/^k = .*/k = ${k_val}/" "/app/config/${CFG_PFX}-tx.conf"
        docker compose -f "$COMPOSE" exec -T rx-node \
            sed -i "s/^k = .*/k = ${k_val}/" "/app/config/${CFG_PFX}-rx.conf"
        docker compose -f "$COMPOSE" restart 2>/dev/null
        sleep 6

        if ! docker compose -f "$COMPOSE" exec -T tx-node \
                ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
            echo "  [FAIL] k=${k_val}, ${n_paths}p: baseline failed"
            docker compose -f "$COMPOSE" down 2>/dev/null || true
            continue
        fi

        # ---- Scenario A: symmetric loss on all interfaces ----
        for loss in 0 20 30 40; do
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
                echo "${k_val},${n_paths},symmetric,${loss},${rep},${RATE}" >> "$CSV"
                rep=$((rep + 1))
            done
            echo "  [k=${k_val},${n_paths}p] symmetric loss=${loss}%: done"
        done

        # ---- Scenario B: eth0 clean (0% loss), all other paths at 30% ----
        # Remove any stale netem rules first
        for iface in $IFACES; do
            docker compose -f "$COMPOSE" exec -T tx-node \
                tc qdisc del dev "$iface" root 2>/dev/null || true
        done
        # Apply 30% loss to all interfaces except eth0
        SKIP_FIRST=1
        for iface in $IFACES; do
            if [ "$SKIP_FIRST" = "1" ]; then
                SKIP_FIRST=0
                continue
            fi
            docker compose -f "$COMPOSE" exec -T tx-node \
                tc qdisc add dev "$iface" root netem loss 30%
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
            echo "${k_val},${n_paths},path0_good,30,${rep},${RATE}" >> "$CSV"
            rep=$((rep + 1))
        done
        echo "  [k=${k_val},${n_paths}p] path0=0% + others=30%: done"

        docker compose -f "$COMPOSE" down 2>/dev/null || true
    done
done

echo "k,paths,scenario,loss_pct,mean,std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2","$3","$4
    sum[key]+=$6; sumsq[key]+=$6*$6; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if(v<0)v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n -k2 -n -k3 -k4 -n >> "$SUMMARY"

echo "[E16] Done. Summary:"
cat "$SUMMARY"
