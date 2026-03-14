#!/bin/sh
# E1-MP-R: Multi-path decode success rate — 30 repetitions per data point
# Usage: e1_multipath_repeated.sh [RESULTS_DIR] [REPS]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e1_multipath_repeated.csv"
SUMMARY="$RESULTS_DIR/e1_multipath_repeated_summary.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.multipath.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

echo "loss_pct,rep,success_rate,config" > "$CSV"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 8

if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E1-MP-R] FAIL: baseline"
    exit 1
fi
echo "  baseline OK — ${REPS} reps per loss level"

for loss in 0 10 20 30 40 50 60 70; do
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

        echo "$loss,$rep,$RATE,multipath" >> "$CSV"
        rep=$((rep + 1))
    done
    echo "  loss=${loss}%: ${REPS} reps done"
done

echo "loss_pct,mean,std,n,config" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1; sum[key]+=$3; sumsq[key]+=$3*$3; n[key]++
}
END {
    for (k in sum) {
        m = sum[k]/n[k]
        v = (sumsq[k]/n[k]) - m*m
        if (v<0) v=0
        s = sqrt(v)
        printf "%s,%.2f,%.2f,%d,multipath\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n >> "$SUMMARY"

echo "[E1-MP-R] Done. Raw: $CSV  Summary: $SUMMARY"
cat "$SUMMARY"
