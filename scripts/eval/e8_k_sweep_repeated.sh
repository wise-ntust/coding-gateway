#!/bin/sh
# E8-R: k-value sweep with 30 reps — latency + decode success at 20% loss
# Usage: e8_k_sweep_repeated.sh [RESULTS_DIR] [REPS]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e8_k_sweep_repeated.csv"
SUMMARY="$RESULTS_DIR/e8_k_sweep_repeated_summary.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

echo "k,rep,avg_rtt_ms,success_rate_20pct" > "$CSV"

for k_val in 1 2 4 8; do
    echo "=== k=${k_val} ==="
    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 5

    # Patch k in both configs
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        sed -i "s/^k = .*/k = ${k_val}/" /app/config/docker-tx.conf
    docker compose -f "$COMPOSE_FILE" exec -T rx-node \
        sed -i "s/^k = .*/k = ${k_val}/" /app/config/docker-rx.conf

    # k is not hot-reloadable, restart containers
    docker compose -f "$COMPOSE_FILE" restart 2>/dev/null
    sleep 6

    # Verify connectivity
    if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
        echo "  [SKIP] k=${k_val}: baseline failed"
        rep=1
        while [ "$rep" -le "$REPS" ]; do
            echo "${k_val},${rep},0,0" >> "$CSV"
            rep=$((rep + 1))
        done
        continue
    fi

    rep=1
    while [ "$rep" -le "$REPS" ]; do
        # RTT measurement (no loss)
        RTT_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 10 -i 0.1 -W 2 10.0.0.2 2>&1 || true)
        AVG_RTT=$(echo "$RTT_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $8}')
        [ -z "$AVG_RTT" ] && AVG_RTT="0"

        # Success at 20% loss
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc del dev eth0 root 2>/dev/null || true
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc add dev eth0 root netem loss 20%
        sleep 1

        PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)
        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")

        # Remove loss for next RTT measurement
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc del dev eth0 root 2>/dev/null || true

        echo "${k_val},${rep},${AVG_RTT},${RATE}" >> "$CSV"
        rep=$((rep + 1))
    done
    echo "  k=${k_val}: ${REPS} reps done"
done

echo "k,rtt_mean,rtt_std,success_mean,success_std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1
    rsum[key]+=$3; rsumsq[key]+=$3*$3
    ssum[key]+=$4; ssumsq[key]+=$4*$4
    n[key]++
} END {
    for (k in n) {
        rm = rsum[k]/n[k]; rv = (rsumsq[k]/n[k]) - rm*rm; if(rv<0)rv=0
        sm = ssum[k]/n[k]; sv = (ssumsq[k]/n[k]) - sm*sm; if(sv<0)sv=0
        printf "%s,%.3f,%.3f,%.2f,%.2f,%d\n", k, rm, sqrt(rv), sm, sqrt(sv), n[k]
    }
}' "$CSV" | sort -t, -k1 -n >> "$SUMMARY"

echo "[E8-R] Done. Summary:"
cat "$SUMMARY"
