#!/bin/sh
# E0: No-FEC baseline — redundancy_ratio=1.0 (n=k, no redundant shards)
# Usage: e0_baseline_no_fec.sh [RESULTS_DIR] [REPS]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e0_baseline.csv"
SUMMARY="$RESULTS_DIR/e0_baseline_summary.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

echo "loss_pct,rep,success_rate,config" > "$CSV"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 5

# Patch redundancy_ratio to 1.0 (no FEC) inside both containers
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sed -i 's/redundancy_ratio = .*/redundancy_ratio = 1.0/' /app/config/docker-tx.conf
docker compose -f "$COMPOSE_FILE" exec -T rx-node \
    sed -i 's/redundancy_ratio = .*/redundancy_ratio = 1.0/' /app/config/docker-rx.conf

# SIGHUP both gateways to pick up new ratio
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
docker compose -f "$COMPOSE_FILE" exec -T rx-node \
    sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
sleep 3

if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E0] FAIL: baseline connectivity"
    exit 1
fi
echo "  baseline OK (ratio=1.0) — ${REPS} reps per loss level"

for loss in 0 10 20 30 40 50 60 70; do
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
        echo "$loss,$rep,$RATE,no_fec" >> "$CSV"
        rep=$((rep + 1))
    done
    echo "  loss=${loss}%: ${REPS} reps done"
done

echo "loss_pct,mean,std,n,config" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1; sum[key]+=$3; sumsq[key]+=$3*$3; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if (v<0) v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d,no_fec\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n >> "$SUMMARY"

echo "[E0] Done. Summary:"
cat "$SUMMARY"
