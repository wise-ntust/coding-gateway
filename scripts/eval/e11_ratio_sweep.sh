#!/bin/sh
# E11: Redundancy ratio sweep — find optimal operating point
# Usage: e11_ratio_sweep.sh [RESULTS_DIR] [REPS]
# Tests ratios 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0 at 30% loss

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e11_ratio_sweep.csv"
SUMMARY="$RESULTS_DIR/e11_ratio_sweep_summary.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

echo "ratio,loss_pct,rep,success_rate" > "$CSV"

for ratio in 1.0 1.25 1.5 1.75 2.0 2.5 3.0; do
    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 5

    # Patch ratio
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" /app/config/docker-tx.conf
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" /app/config/docker-rx.conf
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
    sleep 2

    for loss in 0 10 20 30 40 50; do
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc del dev eth0 root 2>/dev/null || true
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc add dev eth0 root netem loss "${loss}%"
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
            echo "$ratio,$loss,$rep,$RATE" >> "$CSV"
            rep=$((rep + 1))
        done
        echo "  ratio=${ratio} loss=${loss}%: ${REPS} reps done"
    done
done

echo "ratio,loss_pct,mean,std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2; sum[key]+=$4; sumsq[key]+=$4*$4; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if (v<0) v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n -k2 -n >> "$SUMMARY"

echo "[E11] Done. Summary:"
cat "$SUMMARY"
