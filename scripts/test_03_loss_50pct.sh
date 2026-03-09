#!/bin/sh
# T03: 50% shard loss exceeds redundancy — decoder cannot recover all blocks.
# Injects: tc netem loss 50% on tx-node eth0
# Expected: PASS when packet loss >= 30%
# With k=2, n=3: P(block recovered) ≈ 50% → expect ~50% packet loss.
# A threshold of 30% confirms meaningful degradation beyond what the codec absorbs.

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

docker compose -f "$COMPOSE_FILE" exec tx-node \
    tc qdisc add dev eth0 root netem loss 50%

sleep 1

# Run 20 pings; capture output even if ping exits non-zero
PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec tx-node \
    ping -c 20 -W 2 10.0.0.2 2>&1 || true)

LOSS=$(echo "$PING_OUT" | grep -o '[0-9]*% packet loss' | grep -o '^[0-9]*')

if [ -z "$LOSS" ]; then
    echo "[FAIL] T03: Loss exceeds redundancy (could not parse ping output)"
    exit 1
fi

if [ "$LOSS" -ge 30 ]; then
    echo "[PASS] T03: Loss exceeds redundancy (${LOSS}% packet loss with 50% netem)"
    exit 0
else
    echo "[FAIL] T03: Loss exceeds redundancy (${LOSS}% loss, expected >=30%)"
    exit 1
fi
