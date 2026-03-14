#!/bin/sh
# E7: Burst loss (Gilbert-Elliott model) vs uniform random loss
# Usage: e7_burst_loss.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e7_burst_loss.csv
#
# tc netem loss with correlation: "loss N% C%" where C% is state correlation.
# C=0% → pure random (Bernoulli), C=75% → bursty (Gilbert-Elliott approx).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e7_burst_loss.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "loss_pct,correlation,success_rate,config"

for corr in 0 25 50 75; do
    if [ "$corr" -eq 0 ]; then
        LABEL="random"
    else
        LABEL="burst_${corr}pct"
    fi

    for loss in 10 20 30 40 50; do
        # Fresh containers for each data point to avoid state contamination
        docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
        docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
        sleep 5

        # Verify baseline before injecting loss
        if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node \
                ping -c 2 -W 2 10.0.0.2 >/dev/null 2>&1; then
            echo "  [SKIP] corr=${corr}% loss=${loss}%: baseline failed"
            csv_row "$CSV" "$loss" "$corr" "0.0" "$LABEL"
            continue
        fi

        if [ "$corr" -eq 0 ]; then
            docker compose -f "$COMPOSE_FILE" exec -T tx-node \
                tc qdisc add dev eth0 root netem loss "${loss}%"
        else
            docker compose -f "$COMPOSE_FILE" exec -T tx-node \
                tc qdisc add dev eth0 root netem loss "${loss}%" "${corr}%"
        fi

        sleep 2

        PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 40 -i 0.5 -W 2 10.0.0.2 2>&1 || true)

        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")

        echo "  [corr=${corr}%] loss=${loss}% success=${RATE}%"
        csv_row "$CSV" "$loss" "$corr" "$RATE" "$LABEL"
    done
done

echo "[E7] Done. Results: $CSV"
