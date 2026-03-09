#!/bin/sh
# T01: Basic connectivity — ping through TUN tunnel with no faults.
# Expected: PASS (5/5 packets)

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

if docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 5 -W 2 10.0.0.2; then
    echo "[PASS] T01: Basic connectivity"
    exit 0
else
    echo "[FAIL] T01: Basic connectivity"
    exit 1
fi
