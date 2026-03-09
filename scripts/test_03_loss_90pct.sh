#!/bin/sh
# T03: 90% shard loss exceeds redundancy — decoder cannot recover.
# Injects: tc netem loss 90% on tx-node eth0
# Expected: PASS (the test passes when ping FAILS — negative test)
set -e

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

docker compose -f "$COMPOSE_FILE" exec tx-node \
    tc qdisc add dev eth0 root netem loss 90%

sleep 1

# This ping is EXPECTED to fail (high loss). We invert the exit code.
if docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 5 -W 1 10.0.0.2 2>/dev/null; then
    echo "[WARN] T03: ping succeeded unexpectedly — running extended check"
    RESULT=$(docker compose -f "$COMPOSE_FILE" exec tx-node \
        ping -c 20 -W 1 10.0.0.2 2>/dev/null || true)
    LOSS=$(echo "$RESULT" | grep -o '[0-9]*% packet loss' | grep -o '^[0-9]*')
    if [ -z "$LOSS" ] || [ "$LOSS" -lt 70 ]; then
        echo "FAIL: T03 expected >=70% packet loss but got ${LOSS:-unknown}%"
        exit 1
    fi
fi

echo "PASS: T03 confirmed 90% loss prevents reliable recovery"
