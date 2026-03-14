#!/bin/sh
# E6: ARQ controlled comparison — FEC-only vs FEC+ARQ in same run
# Usage: e6_arq_controlled.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e6_arq_fec_only.csv, e6_arq_fec_arq.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV_FEC="$RESULTS_DIR/e6_arq_fec_only.csv"
CSV_ARQ="$RESULTS_DIR/e6_arq_fec_arq.csv"

COMPOSE_FILE="$(dirname "$(dirname "$SCRIPT_DIR")")/docker-compose.dev.yml"
TX_CONF="/app/config/docker-tx.conf"
RX_CONF="/app/config/docker-rx.conf"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV_FEC" "loss_pct,success_rate,config"
csv_header "$CSV_ARQ" "loss_pct,success_rate,config"

for arq_mode in "false" "true"; do
    if [ "$arq_mode" = "false" ]; then
        LABEL="fec_only"
        CSV="$CSV_FEC"
    else
        LABEL="fec+arq"
        CSV="$CSV_ARQ"
    fi

    echo "=== $LABEL ==="

    for loss in 0 10 20 30 40 50; do
        docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
        docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
        sleep 5

        # Patch ARQ setting inside containers
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            sed -i "s/arq_enabled = .*/arq_enabled = ${arq_mode}/" "$TX_CONF"
        docker compose -f "$COMPOSE_FILE" exec -T rx-node \
            sed -i "s/arq_enabled = .*/arq_enabled = ${arq_mode}/" "$RX_CONF"

        # Restart gateways with patched config via SIGHUP
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
        docker compose -f "$COMPOSE_FILE" exec -T rx-node \
            sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
        sleep 2

        # Inject loss
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc del dev eth0 root 2>/dev/null || true
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc add dev eth0 root netem loss "${loss}%"
        sleep 1

        PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            ping -c 40 -i 0.5 -W 2 10.0.0.2 2>&1 || true)

        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")

        echo "  [$LABEL] loss=${loss}% success=${RATE}%"
        csv_row "$CSV" "$loss" "$RATE" "$LABEL"

        docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
        sleep 1
    done
done

echo "[E6] Done. Results: $CSV_FEC, $CSV_ARQ"
