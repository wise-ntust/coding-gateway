#!/bin/sh
# E9-R: Tunnel latency overhead — 30 repetitions
# Usage: e9_latency_repeated.sh [RESULTS_DIR] [REPS]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e9_latency_repeated.csv"
SUMMARY="$RESULTS_DIR/e9_latency_repeated_summary.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

echo "mode,rep,avg_rtt_ms" > "$CSV"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 5

rep=1
while [ "$rep" -le "$REPS" ]; do
    # Direct
    DIRECT_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 20 -i 0.1 -W 2 172.20.0.3 2>&1 || true)
    DIRECT_AVG=$(echo "$DIRECT_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $8}')
    [ -z "$DIRECT_AVG" ] && DIRECT_AVG="0"
    echo "direct,$rep,$DIRECT_AVG" >> "$CSV"

    # Tunnel
    TUNNEL_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 20 -i 0.1 -W 2 10.0.0.2 2>&1 || true)
    TUNNEL_AVG=$(echo "$TUNNEL_OUT" | awk -F'[/ ]' '/rtt min\/avg/{print $8}')
    [ -z "$TUNNEL_AVG" ] && TUNNEL_AVG="0"
    echo "tunnel,$rep,$TUNNEL_AVG" >> "$CSV"

    echo "  rep $rep/$REPS: direct=${DIRECT_AVG}ms tunnel=${TUNNEL_AVG}ms"
    rep=$((rep + 1))
done

echo "mode,mean,std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1; sum[key]+=$3; sumsq[key]+=$3*$3; n[key]++
}
END {
    for (k in sum) {
        m = sum[k]/n[k]
        v = (sumsq[k]/n[k]) - m*m
        if (v<0) v=0
        s = sqrt(v)
        printf "%s,%.3f,%.3f,%d\n", k, m, s, n[k]
    }
}' "$CSV" | sort >> "$SUMMARY"

echo "[E9-R] Done. Raw: $CSV  Summary: $SUMMARY"
cat "$SUMMARY"
