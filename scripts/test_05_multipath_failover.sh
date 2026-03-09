#!/bin/sh
# T05: Multi-path failover — block path1, tunnel continues via path2.
# Topology: tx-node has two paths (eth0→testnet1, eth1→testnet2) to rx-node.
# Injects: iptables DROP all UDP on rx-node eth0 (blocks path1/testnet1)
# Expected: PASS (shards still arrive via path2/eth1; k=1 so any 1 shard suffices)

COMPOSE_FILE="docker-compose.multipath.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 4

# Verify baseline: both paths work before blocking
if ! docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 3 -W 2 10.0.0.2 2>/dev/null; then
    echo "[FAIL] T05: Multi-path failover (baseline ping failed)"
    exit 1
fi

# Block path1: drop UDP on rx-node's eth0 (testnet1 interface)
docker compose -f "$COMPOSE_FILE" exec rx-node \
    iptables -A INPUT -i eth0 -p udp -j DROP

sleep 1

# Tunnel must continue via path2 (eth1/testnet2)
if docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 10 -W 2 10.0.0.2 2>/dev/null; then
    echo "[PASS] T05: Multi-path failover"
    exit 0
else
    echo "[FAIL] T05: Multi-path failover"
    exit 1
fi
