#!/bin/sh
set -e

echo "=== Building containers ==="
docker compose -f docker-compose.dev.yml build

echo "=== Starting containers ==="
docker compose -f docker-compose.dev.yml up -d

echo "=== Waiting for TUN setup (3s) ==="
sleep 3

echo "=== Ping through TUN tunnel (tx-node -> 10.0.0.2) ==="
docker compose -f docker-compose.dev.yml exec tx-node ping -c 5 -W 2 10.0.0.2
echo "PASS: ping through TUN tunnel succeeded"

echo "=== Teardown ==="
docker compose -f docker-compose.dev.yml down
