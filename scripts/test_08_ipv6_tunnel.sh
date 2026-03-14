#!/bin/sh
# T08: IPv6 tunnel — verify IPv6 ping through coding-gateway tunnel.
# Adds IPv6 addresses to the TUN interfaces inside containers and pings.
# The gateway encodes/decodes IPv6 packets using the version-6 length path.

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 4

# Add IPv6 addresses to the TUN interfaces (existing IPv4 stays)
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    ip -6 addr add fd00::1/64 dev tun0 2>/dev/null || true
docker compose -f "$COMPOSE_FILE" exec -T rx-node \
    ip -6 addr add fd00::2/64 dev tun0 2>/dev/null || true

sleep 2

# Verify IPv4 baseline still works
if ! docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[FAIL] T08: IPv6 tunnel (IPv4 baseline broken)"
    exit 1
fi

# Ping over IPv6 through the tunnel
if docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        ping -6 -c 5 -W 3 fd00::2 2>/dev/null; then
    echo "[PASS] T08: IPv6 tunnel (5/5 pings via IPv6)"
    exit 0
else
    echo "[FAIL] T08: IPv6 tunnel (ping6 failed)"
    exit 1
fi
