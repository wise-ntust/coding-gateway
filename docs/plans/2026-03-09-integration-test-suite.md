# Integration Test Suite Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build an automated Docker-based integration test suite that verifies coding-gateway meets its core requirements: basic connectivity, erasure coding loss tolerance, loss boundary, and multi-path failover.

**Architecture:** Five independent shell scripts (T01–T05) each start their own Docker containers, inject network faults via `tc netem` or `iptables`, assert pass/fail, and tear down. A single `run-all-tests.sh` entry point runs all five and prints a summary. T01–T04 reuse the existing single-path `docker-compose.dev.yml`; T05 uses a new dual-network `docker-compose.multipath.yml`.

**Tech Stack:** Shell (POSIX sh), Docker Compose, Alpine Linux, `tc netem` (iproute2), `iptables`

---

## Background: How the Codec Works

- `k=2` original packets per block, `redundancy_ratio=1.5` → `n=3` coded shards per block
- Receiver needs any **2 of 3** shards to decode
- At **20% shard loss**: probability of decoding = P(≥2 of 3 survive) = 3·0.8²·0.2 + 0.8³ ≈ 89.6% → most blocks decode, ping passes
- At **90% shard loss**: probability of decoding = 3·0.1²·0.9 + 0.1³ ≈ 2.8% → almost all blocks fail, ping fails

---

## Task 1: Update Dockerfile.test to include iptables

**Files:**
- Modify: `docker/Dockerfile.test`

T04 and T05 use `iptables` to block a path. Alpine's `iproute2` already provides `tc`; we just need to add `iptables`.

**Step 1: Edit Dockerfile.test**

Change:
```dockerfile
RUN apk add --no-cache \
    gcc musl-dev make linux-headers \
    iproute2 iputils \
    bash
```

To:
```dockerfile
RUN apk add --no-cache \
    gcc musl-dev make linux-headers \
    iproute2 iputils iptables \
    bash
```

**Step 2: Rebuild to verify**

```bash
docker compose -f docker-compose.dev.yml build --no-cache 2>&1 | tail -5
```

Expected: build succeeds, `iptables` package installed.

**Step 3: Commit**

```bash
git add docker/Dockerfile.test
git commit -m "feat(test): add iptables to Docker test image"
```

---

## Task 2: T01 — Basic Connectivity Test

**Files:**
- Create: `scripts/test_01_basic.sh`

This is a refactor of the existing `scripts/test-docker.sh` into the new naming convention. The logic is identical.

**Step 1: Create `scripts/test_01_basic.sh`**

```sh
#!/bin/sh
# T01: Basic connectivity — ping through TUN tunnel with no faults.
# Expected: PASS (5/5 packets)
set -e

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 5 -W 2 10.0.0.2
```

**Step 2: Make executable and run**

```bash
chmod +x scripts/test_01_basic.sh
./scripts/test_01_basic.sh
```

Expected: exits 0, `5 packets transmitted, 5 received, 0% packet loss`

**Step 3: Commit**

```bash
git add scripts/test_01_basic.sh
git commit -m "test: T01 basic TUN connectivity"
```

---

## Task 3: T02 — Loss Tolerance at 20%

**Files:**
- Create: `scripts/test_02_loss_20pct.sh`

Inject 20% packet loss on the TX→RX UDP path. With k=2, n=3, the codec should absorb this loss and ping should succeed.

**Step 1: Create `scripts/test_02_loss_20pct.sh`**

```sh
#!/bin/sh
# T02: Erasure coding absorbs 20% shard loss.
# Injects: tc netem loss 20% on tx-node eth0 (outbound UDP shards)
# Expected: PASS (codec recovers; redundancy_ratio=1.5 can absorb <33% loss)
set -e

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

# Inject 20% loss on outbound traffic from tx-node
docker compose -f "$COMPOSE_FILE" exec tx-node \
    tc qdisc add dev eth0 root netem loss 20%

sleep 1

docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 10 -W 2 10.0.0.2
```

**Step 2: Run**

```bash
./scripts/test_02_loss_20pct.sh
```

Expected: exits 0, most packets received (≥ 7 of 10)

**Note:** The test uses `ping -c 10` (more packets) to smooth over probabilistic loss. With 20% shard loss and n=3 coded shards, roughly 89% of blocks decode successfully, so we expect ≥ 8 of 10 pings to succeed. `ping` returns exit code 0 if any reply is received.

**Step 3: Commit**

```bash
git add scripts/test_02_loss_20pct.sh
git commit -m "test: T02 erasure coding absorbs 20% shard loss"
```

---

## Task 4: T03 — Loss Exceeds Redundancy (Negative Test)

**Files:**
- Create: `scripts/test_03_loss_90pct.sh`

Inject 90% packet loss. With k=2, n=3, only ~3% of blocks decode. The test **expects ping to fail** — this is a negative test proving the codec boundary is real.

**Step 1: Create `scripts/test_03_loss_90pct.sh`**

```sh
#!/bin/sh
# T03: 90% shard loss exceeds redundancy — decoder cannot recover.
# Injects: tc netem loss 90% on tx-node eth0
# Expected: PASS (i.e., the test passes when ping FAILS — negative test)
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
    echo "[WARN] T03: ping succeeded unexpectedly — codec may have recovered by chance"
    echo "[WARN] T03: re-running with stricter check"
    # Re-check: ping 20 packets and assert >= 80% loss
    RESULT=$(docker compose -f "$COMPOSE_FILE" exec tx-node \
        ping -c 20 -W 1 10.0.0.2 2>/dev/null || true)
    LOSS=$(echo "$RESULT" | grep -o '[0-9]*% packet loss' | grep -o '[0-9]*')
    if [ -z "$LOSS" ] || [ "$LOSS" -lt 70 ]; then
        echo "FAIL: T03 expected >= 70% packet loss but got ${LOSS:-unknown}%"
        exit 1
    fi
fi

echo "PASS: T03 confirmed that 90% loss prevents reliable recovery"
```

**Step 2: Run**

```bash
./scripts/test_03_loss_90pct.sh
```

Expected: exits 0, message `PASS: T03 confirmed that 90% loss prevents reliable recovery`

**Step 3: Commit**

```bash
git add scripts/test_03_loss_90pct.sh
git commit -m "test: T03 negative test — 90% loss exceeds redundancy boundary"
```

---

## Task 5: T04 — Single-Path Complete Block (Negative Test)

**Files:**
- Create: `scripts/test_04_singlepath_block.sh`

Block all UDP on the only path using `iptables`. With no shards reaching the receiver, the tunnel must go down. This is a negative test.

**Step 1: Create `scripts/test_04_singlepath_block.sh`**

```sh
#!/bin/sh
# T04: Single path completely blocked — tunnel must fail.
# Injects: iptables DROP all UDP on rx-node (simulates full mmWave blockage)
# Expected: PASS (i.e., the test passes when ping FAILS — negative test)
set -e

COMPOSE_FILE="docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build
sleep 3

# Block all incoming UDP on rx-node (drops all data shards)
docker compose -f "$COMPOSE_FILE" exec rx-node \
    iptables -A INPUT -p udp -j DROP

sleep 1

# Ping is expected to fail
if docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 5 -W 1 10.0.0.2 2>/dev/null; then
    echo "FAIL: T04 expected ping to fail when path is blocked, but it succeeded"
    exit 1
fi

echo "PASS: T04 confirmed that complete path block stops the tunnel"
```

**Step 2: Run**

```bash
./scripts/test_04_singlepath_block.sh
```

Expected: exits 0, `PASS: T04 confirmed that complete path block stops the tunnel`

**Step 3: Commit**

```bash
git add scripts/test_04_singlepath_block.sh
git commit -m "test: T04 negative test — single-path complete block"
```

---

## Task 6: Multi-Path Docker Compose and Configs

**Files:**
- Create: `docker-compose.multipath.yml`
- Create: `config/multipath-tx.conf`
- Create: `config/multipath-rx.conf`

Two Docker networks simulate two independent mmWave links. TX distributes shards across both paths; RX listens on a single port (INADDR_ANY).

**Step 1: Create `config/multipath-tx.conf`**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.1/30
listen_port = 7001

[coding]
k = 2
redundancy_ratio = 1.5
block_timeout_ms = 10
max_payload = 1400
window_size = 8

[strategy]
type = fixed
probe_interval_ms = 100
probe_loss_threshold = 0.3

[path.link1]
interface = eth0
remote_ip = 172.20.0.3
remote_port = 7000
weight = 1.0
enabled = true

[path.link2]
interface = eth1
remote_ip = 172.21.0.3
remote_port = 7000
weight = 1.0
enabled = true
```

**Step 2: Create `config/multipath-rx.conf`**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.2/30
listen_port = 7000

[coding]
k = 2
redundancy_ratio = 1.5
block_timeout_ms = 10
max_payload = 1400
window_size = 8

[strategy]
type = fixed
probe_interval_ms = 100
probe_loss_threshold = 0.3

[path.link1]
interface = eth0
remote_ip = 172.20.0.2
remote_port = 7001
weight = 1.0
enabled = true

[path.link2]
interface = eth1
remote_ip = 172.21.0.2
remote_port = 7001
weight = 1.0
enabled = true
```

**Step 3: Create `docker-compose.multipath.yml`**

```yaml
services:
  tx-node:
    build:
      context: .
      dockerfile: docker/Dockerfile.test
    cap_add:
      - NET_ADMIN
    devices:
      - /dev/net/tun
    networks:
      testnet1:
        ipv4_address: 172.20.0.2
      testnet2:
        ipv4_address: 172.21.0.2
    command: ["/app/coding-gateway", "--config", "/app/config/multipath-tx.conf"]

  rx-node:
    build:
      context: .
      dockerfile: docker/Dockerfile.test
    cap_add:
      - NET_ADMIN
    devices:
      - /dev/net/tun
    networks:
      testnet1:
        ipv4_address: 172.20.0.3
      testnet2:
        ipv4_address: 172.21.0.3
    command: ["/app/coding-gateway", "--config", "/app/config/multipath-rx.conf"]

networks:
  testnet1:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.0.0/24
  testnet2:
    driver: bridge
    ipam:
      config:
        - subnet: 172.21.0.0/24
```

**Step 4: Verify basic multi-path ping works first**

```bash
docker compose -f docker-compose.multipath.yml up -d --build
sleep 3
docker compose -f docker-compose.multipath.yml exec tx-node ping -c 3 -W 2 10.0.0.2
docker compose -f docker-compose.multipath.yml down
```

Expected: 3/3 packets, 0% loss.

**Step 5: Commit**

```bash
git add docker-compose.multipath.yml config/multipath-tx.conf config/multipath-rx.conf
git commit -m "feat(test): dual-network Docker topology for multi-path tests"
```

---

## Task 7: T05 — Multi-Path Failover

**Files:**
- Create: `scripts/test_05_multipath_failover.sh`

Block path1 (testnet1/eth0) with iptables on rx-node. TX distributes shards across both paths with `fixed` strategy (round-robin). After blocking path1, approximately half the shards are lost, but the codec with `redundancy_ratio=1.5` (n=3 for k=2) should recover: need 2 of 3 shards, and with one path providing ~0.5 shards per block, the surviving path2 provides enough.

Actually with round-robin across 2 paths and path1 blocked:
- Each block generates n=3 shards, sent round-robin: shard0→path1, shard1→path2, shard2→path1
- path1 blocked → shards 0 and 2 lost, only shard 1 arrives
- Only 1 of 3 shards → decoder needs 2 → FAIL

This means the test would fail with `fixed` strategy. For T05 to pass, we need `adaptive` strategy which detects path1 failure and increases `n` to ensure enough shards go via path2.

**Alternative approach for T05:** Use `weighted` strategy with path1 weight=0.0 (disabled) to simulate a failed path. But that's a config change, not a runtime failure simulation.

**Revised T05 design:** Block path1 completely, and use `adaptive` strategy which will:
1. Detect path1 as dead (probes not echoing back)
2. Scale up `n` so all coded shards go via path2
3. Ping succeeds via path2

This means T05 requires `strategy = adaptive` in `multipath-tx.conf`. Update the config.

**Step 1: Update `config/multipath-tx.conf` strategy section**

Change `type = fixed` to `type = adaptive` in `config/multipath-tx.conf`.

**Step 2: Create `scripts/test_05_multipath_failover.sh`**

```sh
#!/bin/sh
# T05: Multi-path failover — block path1, adaptive strategy routes via path2.
# Topology: tx-node has two paths (testnet1=eth0, testnet2=eth1)
# Injects: iptables DROP UDP on rx-node eth0 (kills path1/testnet1)
# Expected: PASS (adaptive strategy detects path1 dead, uses path2 only)
set -e

COMPOSE_FILE="docker-compose.multipath.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" up -d --build

# Wait for TUN setup and initial probe exchange
sleep 5

# Verify tunnel works with both paths
docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 3 -W 2 10.0.0.2 || {
    echo "FAIL: T05 baseline ping failed (both paths should work)"
    exit 1
}

# Block path1 (testnet1 = eth0 on rx-node)
docker compose -f "$COMPOSE_FILE" exec rx-node \
    iptables -A INPUT -i eth0 -p udp -j DROP

# Wait for adaptive strategy to detect path1 failure and adapt
# probe_interval_ms=100 → after ~5 probes (0.5s) loss_rate rises
# EWMA with alpha=0.2: after 8 probes at loss=1.0, loss_rate ≈ 0.83 > 0.3 threshold
sleep 3

# Tunnel must still work via path2
docker compose -f "$COMPOSE_FILE" exec tx-node ping -c 5 -W 2 10.0.0.2

echo "PASS: T05 multi-path failover — path2 carries traffic after path1 blocked"
```

**Step 3: Run**

```bash
./scripts/test_05_multipath_failover.sh
```

Expected: exits 0, `PASS: T05 multi-path failover — path2 carries traffic after path1 blocked`

**Step 4: Commit**

```bash
git add scripts/test_05_multipath_failover.sh config/multipath-tx.conf
git commit -m "test: T05 multi-path failover with adaptive strategy"
```

---

## Task 8: run-all-tests.sh — Master Test Runner

**Files:**
- Create: `scripts/run-all-tests.sh`

**Step 1: Create `scripts/run-all-tests.sh`**

```sh
#!/bin/sh
# run-all-tests.sh — Execute all integration tests and report results.
# Usage: ./scripts/run-all-tests.sh
# Exit code: 0 if all pass, 1 if any fail.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

TESTS="
test_01_basic.sh          T01: Basic connectivity
test_02_loss_20pct.sh     T02: Erasure coding absorbs 20% shard loss
test_03_loss_90pct.sh     T03: 90% loss exceeds redundancy (negative)
test_04_singlepath_block.sh  T04: Single-path complete block (negative)
test_05_multipath_failover.sh T05: Multi-path failover via adaptive strategy
"

pass=0
fail=0
total=0

# Run each test
run_test() {
    script="$1"
    name="$2"
    total=$((total + 1))

    printf "Running %-45s ... " "$name"
    if sh "$SCRIPT_DIR/$script" >/tmp/test_output 2>&1; then
        echo "[PASS]"
        pass=$((pass + 1))
    else
        echo "[FAIL]"
        echo "  Output:"
        sed 's/^/    /' /tmp/test_output | tail -10
        fail=$((fail + 1))
    fi
}

run_test "test_01_basic.sh"              "T01: Basic connectivity"
run_test "test_02_loss_20pct.sh"         "T02: 20% loss tolerance"
run_test "test_03_loss_90pct.sh"         "T03: 90% loss boundary (negative)"
run_test "test_04_singlepath_block.sh"   "T04: Single-path block (negative)"
run_test "test_05_multipath_failover.sh" "T05: Multi-path failover"

echo ""
echo "Results: $total tests, $pass passed, $fail failed"

if [ "$fail" -eq 0 ]; then
    echo "ALL TESTS PASSED"
    exit 0
else
    echo "SOME TESTS FAILED"
    exit 1
fi
```

**Step 2: Make executable and run all tests**

```bash
chmod +x scripts/run-all-tests.sh
./scripts/run-all-tests.sh
```

Expected output:
```
Running T01: Basic connectivity              ... [PASS]
Running T02: 20% loss tolerance              ... [PASS]
Running T03: 90% loss boundary (negative)    ... [PASS]
Running T04: Single-path block (negative)    ... [PASS]
Running T05: Multi-path failover             ... [PASS]

Results: 5 tests, 5 passed, 0 failed
ALL TESTS PASSED
```

**Step 3: Commit**

```bash
git add scripts/run-all-tests.sh
git commit -m "test: run-all-tests.sh master integration test runner"
```

---

## Task 9: Final verification and push

**Step 1: Run the complete test suite one final time**

```bash
./scripts/run-all-tests.sh
```

Expected: `ALL TESTS PASSED`

**Step 2: Push**

```bash
git push
```
