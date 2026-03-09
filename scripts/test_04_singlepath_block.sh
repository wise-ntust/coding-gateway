#!/bin/sh
# T04: Single path completely blocked — tunnel must fail.
# Injects: iptables DROP all UDP on rx-node
# Expected: PASS (the test passes when ping FAILS — negative test)

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

# Block all incoming UDP on rx-node
docker compose -f "$COMPOSE_FILE" exec rx-node \
    iptables -A INPUT -p udp -j DROP

sleep 1

# Ping is expected to fail
if docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 5 -W 1 10.0.0.2 2>/dev/null; then
    echo "[FAIL] T04: Single-path block"
    exit 1
else
    echo "[PASS] T04: Single-path block"
    exit 0
fi
