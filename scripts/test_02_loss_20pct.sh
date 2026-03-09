#!/bin/sh
# T02: Erasure coding absorbs 20% shard loss.
# Injects: tc netem loss 20% on tx-node eth0 (outbound UDP shards)
# Expected: PASS (codec recovers with redundancy_ratio=1.5, n=3 for k=2)
set -e

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

docker compose -f "$COMPOSE_FILE" exec tx-node \
    tc qdisc add dev eth0 root netem loss 20%

sleep 1

docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 10 -W 2 10.0.0.2
