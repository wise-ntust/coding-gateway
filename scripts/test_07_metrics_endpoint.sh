#!/bin/sh
# T07: Prometheus metrics endpoint — verify /metrics returns valid data.
# Start TX node with metrics_port enabled, curl the endpoint, check format.

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

# Add metrics_port to docker configs before starting
docker compose -f "$COMPOSE_FILE" build --quiet 2>/dev/null

# We need to inject metrics_port into the config. Use a modified compose
# command that patches the config inside the container after start.
docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

# Enable metrics on tx-node
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sh -c 'grep -q metrics_port /app/config/docker-tx.conf || \
           sed -i "/listen_port/a metrics_port = 9090" /app/config/docker-tx.conf'

# Restart gateway to pick up metrics_port (since it's read at startup)
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sh -c 'kill $(pgrep coding-gateway); sleep 1; /app/coding-gateway --config /app/config/docker-tx.conf &'
sleep 3

# Generate some traffic so counters are non-zero
docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    ping -c 10 -i 0.2 -W 2 10.0.0.2 >/dev/null 2>&1 || true

sleep 2

# Fetch metrics
METRICS=$(docker compose -f "$COMPOSE_FILE" exec -T tx-node \
    sh -c 'wget -qO- http://127.0.0.1:9090/metrics 2>/dev/null || echo FETCH_FAILED')

if echo "$METRICS" | grep -q "FETCH_FAILED"; then
    echo "[FAIL] T07: Metrics endpoint (could not fetch /metrics)"
    exit 1
fi

# Verify key metrics are present
CHECKS=0
PASSED=0

for metric in \
    "coding_gateway_shards_sent_total" \
    "coding_gateway_decode_success_total" \
    "coding_gateway_decode_failure_total" \
    "coding_gateway_redundancy_ratio_current" \
    "coding_gateway_block_latency_ms_bucket"; do
    CHECKS=$((CHECKS + 1))
    if echo "$METRICS" | grep -q "$metric"; then
        PASSED=$((PASSED + 1))
    else
        echo "  MISSING: $metric"
    fi
done

if [ "$PASSED" -eq "$CHECKS" ]; then
    echo "[PASS] T07: Metrics endpoint ($PASSED/$CHECKS metrics found)"
    exit 0
else
    echo "[FAIL] T07: Metrics endpoint ($PASSED/$CHECKS metrics found)"
    exit 1
fi
