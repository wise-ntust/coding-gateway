#!/bin/sh
# T06: SIGHUP config reload — verify strategy parameters update without restart.
# Start TX node, send SIGHUP with modified config, verify log shows reload.

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

# Verify baseline connectivity
if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[FAIL] T06: SIGHUP reload (baseline ping failed)"
    exit 1
fi

# Modify redundancy_ratio in the running container's config
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sed -i 's/redundancy_ratio = 1.5/redundancy_ratio = 2.5/' /app/config/docker-tx.conf

# Send SIGHUP to the gateway process
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sh -c 'kill -HUP $(pgrep coding-gateway)'

sleep 2

# Check gateway logs for reload confirmation
RELOAD_LOG=$(docker compose -f "$COMPOSE_FILE" logs tx-node 2>&1 | grep -c "config reloaded")

if [ "$RELOAD_LOG" -ge 1 ]; then
    # Verify tunnel still works after reload
    if docker compose -f "$COMPOSE_FILE" exec -T tx-node ping -c 5 -W 2 10.0.0.2 >/dev/null 2>&1; then
        echo "[PASS] T06: SIGHUP reload (config reloaded, tunnel still functional)"
        exit 0
    else
        echo "[FAIL] T06: SIGHUP reload (reloaded but tunnel broken)"
        exit 1
    fi
else
    echo "[FAIL] T06: SIGHUP reload (no reload message in logs)"
    exit 1
fi
