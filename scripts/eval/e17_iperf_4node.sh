#!/bin/sh
# E17: 4-node tunnel performance — FEC vs no-FEC
# Usage: e17_iperf_4node.sh [RESULTS_DIR] [REPS]
#
# Topology: client-tx → ap-tx →[mmWave]→ ap-rx → client-rx
# Metrics:
#   throughput_mbps  — iperf3 UDP at 0% loss only (server started once per mode)
#   tunnel_loss_pct  — ping -c 100 end-to-end IP loss (primary FEC metric)
# Two modes: ratio=1.0 (no FEC) and ratio=2.0 (FEC 2x).
# Symmetric loss applied on ap-tx eth1+eth2 (the mmWave paths).
#
# Note: iperf3 on Alpine ARM64 hangs when TCP control channel
# experiences packet loss; throughput is only measured at 0% path loss.
# tunnel_loss_pct via ping is the primary metric at all loss levels.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-10}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e17_iperf_4node.csv"
SUMMARY="$RESULTS_DIR/e17_iperf_4node_summary.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"
COMPOSE="$REPO/docker-compose.4node.multipath.yml"

CURRENT_COMPOSE=""
cleanup() {
    [ -n "$CURRENT_COMPOSE" ] && docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT
CURRENT_COMPOSE="$COMPOSE"

echo "mode,loss_pct,rep,throughput_mbps,tunnel_loss_pct" > "$CSV"

for ratio in 1.0 2.0; do
    if [ "$ratio" = "1.0" ]; then MODE="no_fec"; else MODE="fec_2x"; fi

    echo "=== E17: ${MODE} (ratio=${ratio}) ==="

    docker compose -f "$COMPOSE" down 2>/dev/null || true
    docker compose -f "$COMPOSE" up -d --build 2>/dev/null
    sleep 10

    # Hot-reload redundancy_ratio on both AP nodes
    docker compose -f "$COMPOSE" exec -T ap-tx \
        sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" \
        /app/config/4node-ap-tx.conf
    docker compose -f "$COMPOSE" exec -T ap-rx \
        sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" \
        /app/config/4node-ap-rx.conf
    docker compose -f "$COMPOSE" exec -T ap-tx \
        sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
    docker compose -f "$COMPOSE" exec -T ap-rx \
        sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
    sleep 2

    # Verify baseline connectivity
    if ! docker compose -f "$COMPOSE" exec -T client-tx \
            ping -c 3 -W 2 10.20.0.2 >/dev/null 2>&1; then
        echo "  [FAIL] ${MODE}: baseline ping failed"
        docker compose -f "$COMPOSE" down 2>/dev/null || true
        continue
    fi

    for loss in 0 10 20 30 40; do
        # Apply symmetric loss on ap-tx mmWave interfaces
        for iface in eth1 eth2; do
            docker compose -f "$COMPOSE" exec -T ap-tx \
                tc qdisc del dev "$iface" root 2>/dev/null || true
            docker compose -f "$COMPOSE" exec -T ap-tx \
                tc qdisc add dev "$iface" root netem loss "${loss}%"
        done
        sleep 1

        rep=1
        while [ "$rep" -le "$REPS" ]; do
            # --- Throughput: iperf3 UDP only at 0% loss ---
            THROUGHPUT=0.00
            if [ "$loss" = "0" ]; then
                IPERF_OUT=$(docker compose -f "$COMPOSE" exec -T client-tx \
                    timeout 10 iperf3 -c 10.20.0.2 -u -b 10M -t 5 -l 1300 -J \
                    2>/dev/null || echo "")
                THROUGHPUT=$(echo "$IPERF_OUT" | awk '
                    /"bits_per_second"/ { gsub(/[^0-9.]/,"",$NF); bps=$NF+0 }
                    END { printf "%.2f", bps/1e6 }')
                [ -z "$THROUGHPUT" ] && THROUGHPUT=0.00
                sleep 2
            fi

            # --- Loss: ping 100 packets at 5 pkt/s (20 seconds) ---
            PING_OUT=$(docker compose -f "$COMPOSE" exec -T client-tx \
                ping -c 100 -i 0.2 -W 2 10.20.0.2 2>/dev/null || echo "")
            TUNNEL_LOSS=$(echo "$PING_OUT" | awk '
                /packet loss/ {
                    for(i=1;i<=NF;i++) if($i~/%/) { gsub(/%/,"",$i); print $i+0; exit }
                }')
            [ -z "$TUNNEL_LOSS" ] && TUNNEL_LOSS=100

            echo "${MODE},${loss},${rep},${THROUGHPUT},${TUNNEL_LOSS}" >> "$CSV"
            rep=$((rep + 1))
        done
        echo "  [${MODE}] loss=${loss}%: ${REPS} reps done"
    done

    docker compose -f "$COMPOSE" down 2>/dev/null || true
done

echo "mode,loss_pct,mean_mbps,std_mbps,mean_tunnel_loss,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2
    sum[key]+=$4; sumsq[key]+=$4*$4
    lsum[key]+=$5; n[key]++
} END {
    for (k in sum) {
        m=sum[k]/n[k]; v=(sumsq[k]/n[k])-m*m
        if(v<0)v=0; s=sqrt(v)
        ml=lsum[k]/n[k]
        printf "%s,%.2f,%.2f,%.1f,%d\n", k, m, s, ml, n[k]
    }
}' "$CSV" | sort -t, -k1,1 -k2,2n >> "$SUMMARY"

echo "[E17] Done. Summary:"
cat "$SUMMARY"
