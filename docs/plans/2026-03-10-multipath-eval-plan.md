# Multi-Path Evaluation & Parameter Sweep Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add multi-path (2/3 link) evaluation infrastructure, codec parameter sweep, and link-failure resilience tests with comparison plots.

**Architecture:** New Docker topologies (tripath), eval shell scripts (E6–E8) that dynamically generate configs, and matplotlib plot scripts producing multi-curve comparison figures.

**Tech Stack:** Shell (eval scripts), Python 3 + matplotlib (plots), Docker Compose (topologies)

---

### Task 1: Fix existing multipath configs and create tripath topology

**Files:**
- Modify: `config/multipath-tx.conf`
- Modify: `config/multipath-rx.conf`
- Create: `docker-compose.tripath.yml`
- Create: `config/tripath-tx.conf`
- Create: `config/tripath-rx.conf`

**Step 1: Update multipath-tx.conf**

Change k=1 to k=2 and add [arq] section to match docker configs:

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

[arq]
arq_enabled = true
arq_cache_size = 64

[path.path1]
interface = eth0
remote_ip = 172.20.0.3
remote_port = 7000
weight = 1.0
enabled = true

[path.path2]
interface = eth1
remote_ip = 172.21.0.3
remote_port = 7000
weight = 1.0
enabled = true
```

**Step 2: Update multipath-rx.conf**

Same changes (k=2, add [arq]), remote IPs point to TX:

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

[arq]
arq_enabled = true
arq_cache_size = 64

[path.path1]
interface = eth0
remote_ip = 172.20.0.2
remote_port = 7001
weight = 1.0
enabled = true

[path.path2]
interface = eth1
remote_ip = 172.21.0.2
remote_port = 7001
weight = 1.0
enabled = true
```

**Step 3: Create docker-compose.tripath.yml**

Three bridge networks (testnet1/2/3), tx-node and rx-node each have three network interfaces:

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
      testnet3:
        ipv4_address: 172.22.0.2
    command: ["/app/coding-gateway", "--config", "/app/config/tripath-tx.conf"]

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
      testnet3:
        ipv4_address: 172.22.0.3
    command: ["/app/coding-gateway", "--config", "/app/config/tripath-rx.conf"]

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
  testnet3:
    driver: bridge
    ipam:
      config:
        - subnet: 172.22.0.0/24
```

**Step 4: Create tripath-tx.conf**

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

[arq]
arq_enabled = true
arq_cache_size = 64

[path.mmwave]
interface = eth0
remote_ip = 172.20.0.3
remote_port = 7000
weight = 1.0
enabled = true

[path.sub6ghz]
interface = eth1
remote_ip = 172.21.0.3
remote_port = 7000
weight = 1.0
enabled = true

[path.wifi]
interface = eth2
remote_ip = 172.22.0.3
remote_port = 7000
weight = 1.0
enabled = true
```

**Step 5: Create tripath-rx.conf**

Same structure, remote IPs pointing to TX (172.x.0.2:7001):

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

[arq]
arq_enabled = true
arq_cache_size = 64

[path.mmwave]
interface = eth0
remote_ip = 172.20.0.2
remote_port = 7001
weight = 1.0
enabled = true

[path.sub6ghz]
interface = eth1
remote_ip = 172.21.0.2
remote_port = 7001
weight = 1.0
enabled = true

[path.wifi]
interface = eth2
remote_ip = 172.22.0.2
remote_port = 7001
weight = 1.0
enabled = true
```

**Step 6: Smoke test tripath topology**

```bash
docker compose -f docker-compose.tripath.yml up -d --build
sleep 8
docker compose -f docker-compose.tripath.yml exec -T tx-node ping -c 3 -W 2 10.0.0.2
docker compose -f docker-compose.tripath.yml down
```

Expected: 3 pings succeed (0% loss).

**Step 7: Commit**

```bash
git add config/multipath-tx.conf config/multipath-rx.conf \
       docker-compose.tripath.yml config/tripath-tx.conf config/tripath-rx.conf
git commit -m "eval: add tripath topology, fix multipath configs (k=2, arq)"
```

---

### Task 2: E6 — Multi-path throughput comparison script

**Files:**
- Create: `scripts/eval/e6_multipath.sh`

This script measures throughput for 1-path, 2-path, and 3-path topologies. It runs two sub-experiments:
- **Symmetric**: all links get the same loss rate
- **Asymmetric**: mmWave loss swept, Sub-6GHz fixed 15%, WiFi fixed 10%

The script uses the existing compose files and `tc netem` to inject loss per-interface.

**Step 1: Write e6_multipath.sh**

```sh
#!/bin/sh
# E6: Multi-path throughput comparison
# Usage: e6_multipath.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e6_multipath_symmetric.csv
#         RESULTS_DIR/e6_multipath_asymmetric.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV_SYM="$RESULTS_DIR/e6_multipath_symmetric.csv"
CSV_ASYM="$RESULTS_DIR/e6_multipath_asymmetric.csv"

PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

cleanup_all() {
    docker compose -f "$PROJECT_DIR/docker-compose.dev.yml" down 2>/dev/null || true
    docker compose -f "$PROJECT_DIR/docker-compose.multipath.yml" down 2>/dev/null || true
    docker compose -f "$PROJECT_DIR/docker-compose.tripath.yml" down 2>/dev/null || true
}
trap cleanup_all EXIT

# measure_throughput COMPOSE_FILE
# Runs iperf3 for 5s through TUN, returns Mbps via $MBPS variable
measure_throughput() {
    _cf="$1"
    docker compose -f "$_cf" exec -T rx-node sh -c \
        'pkill iperf3 2>/dev/null; iperf3 -s -B 10.0.0.2 --json > /tmp/server.json 2>&1 &
         sleep 1; echo "server ready"'
    sleep 1

    docker compose -f "$_cf" exec -T tx-node sh -c \
        'iperf3 -c 10.0.0.2 -t 5 > /tmp/client.txt 2>&1; echo "client done exit:$?"' \
        2>/dev/null || true
    sleep 2

    BPS=$(docker compose -f "$_cf" exec -T rx-node sh -c '
        JSON=/tmp/server.json
        [ -f "$JSON" ] || { echo 0; exit; }
        grep "bits_per_second" "$JSON" | \
            awk -F"[:\t ]" "{for(i=1;i<=NF;i++) if(\$i+0>0){print \$i+0; exit}}" | \
            head -1
    ' 2>/dev/null)
    [ -z "$BPS" ] && BPS=0
    MBPS=$(awk "BEGIN { printf \"%.2f\", $BPS / 1000000 }")
}

# inject_loss COMPOSE_FILE INTERFACE LOSS_PCT [DELAY_MS]
inject_loss() {
    _cf="$1"; _iface="$2"; _loss="$3"; _delay="${4:-0}"
    docker compose -f "$_cf" exec -T tx-node \
        tc qdisc del dev "$_iface" root 2>/dev/null || true
    if [ "$_delay" -gt 0 ] 2>/dev/null; then
        docker compose -f "$_cf" exec -T tx-node \
            tc qdisc add dev "$_iface" root netem loss "${_loss}%" delay "${_delay}ms"
    else
        docker compose -f "$_cf" exec -T tx-node \
            tc qdisc add dev "$_iface" root netem loss "${_loss}%"
    fi
}

# ─── Symmetric test: all links same loss ───
echo "=== E6 Symmetric: all links same loss ==="
rm -f "$CSV_SYM"
csv_header "$CSV_SYM" "loss_pct,throughput_mbps,config"

for loss in 0 10 20 30 40 50 60; do
    # 1-path
    cleanup_all
    docker compose -f "$PROJECT_DIR/docker-compose.dev.yml" up -d --build 2>/dev/null
    sleep 8
    inject_loss "$PROJECT_DIR/docker-compose.dev.yml" eth0 "$loss"
    sleep 1
    measure_throughput "$PROJECT_DIR/docker-compose.dev.yml"
    echo "  [1-path] loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV_SYM" "$loss" "$MBPS" "1-path"

    # 2-path
    cleanup_all
    docker compose -f "$PROJECT_DIR/docker-compose.multipath.yml" up -d --build 2>/dev/null
    sleep 8
    inject_loss "$PROJECT_DIR/docker-compose.multipath.yml" eth0 "$loss" 1
    inject_loss "$PROJECT_DIR/docker-compose.multipath.yml" eth1 "$loss" 5
    sleep 1
    measure_throughput "$PROJECT_DIR/docker-compose.multipath.yml"
    echo "  [2-path] loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV_SYM" "$loss" "$MBPS" "2-path"

    # 3-path
    cleanup_all
    docker compose -f "$PROJECT_DIR/docker-compose.tripath.yml" up -d --build 2>/dev/null
    sleep 8
    inject_loss "$PROJECT_DIR/docker-compose.tripath.yml" eth0 "$loss" 1
    inject_loss "$PROJECT_DIR/docker-compose.tripath.yml" eth1 "$loss" 5
    inject_loss "$PROJECT_DIR/docker-compose.tripath.yml" eth2 "$loss" 10
    sleep 1
    measure_throughput "$PROJECT_DIR/docker-compose.tripath.yml"
    echo "  [3-path] loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV_SYM" "$loss" "$MBPS" "3-path"
done

# ─── Asymmetric test: mmWave loss swept, others fixed ───
echo "=== E6 Asymmetric: mmWave swept, Sub-6GHz=15%, WiFi=10% ==="
rm -f "$CSV_ASYM"
csv_header "$CSV_ASYM" "loss_pct,throughput_mbps,config"

for loss in 0 10 20 30 40 50 60 80 100; do
    # 1-path (mmWave only)
    cleanup_all
    docker compose -f "$PROJECT_DIR/docker-compose.dev.yml" up -d --build 2>/dev/null
    sleep 8
    inject_loss "$PROJECT_DIR/docker-compose.dev.yml" eth0 "$loss" 1
    sleep 1
    measure_throughput "$PROJECT_DIR/docker-compose.dev.yml"
    echo "  [1-path] mmWave_loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV_ASYM" "$loss" "$MBPS" "1-path"

    # 2-path (mmWave + Sub-6GHz@15%)
    cleanup_all
    docker compose -f "$PROJECT_DIR/docker-compose.multipath.yml" up -d --build 2>/dev/null
    sleep 8
    inject_loss "$PROJECT_DIR/docker-compose.multipath.yml" eth0 "$loss" 1
    inject_loss "$PROJECT_DIR/docker-compose.multipath.yml" eth1 15 5
    sleep 1
    measure_throughput "$PROJECT_DIR/docker-compose.multipath.yml"
    echo "  [2-path] mmWave_loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV_ASYM" "$loss" "$MBPS" "2-path"

    # 3-path (mmWave + Sub-6GHz@15% + WiFi@10%)
    cleanup_all
    docker compose -f "$PROJECT_DIR/docker-compose.tripath.yml" up -d --build 2>/dev/null
    sleep 8
    inject_loss "$PROJECT_DIR/docker-compose.tripath.yml" eth0 "$loss" 1
    inject_loss "$PROJECT_DIR/docker-compose.tripath.yml" eth1 15 5
    inject_loss "$PROJECT_DIR/docker-compose.tripath.yml" eth2 10 10
    sleep 1
    measure_throughput "$PROJECT_DIR/docker-compose.tripath.yml"
    echo "  [3-path] mmWave_loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV_ASYM" "$loss" "$MBPS" "3-path"
done

echo "[E6] Done."
echo "  Symmetric: $CSV_SYM"
echo "  Asymmetric: $CSV_ASYM"
```

**Step 2: Make executable and verify syntax**

```bash
chmod +x scripts/eval/e6_multipath.sh
sh -n scripts/eval/e6_multipath.sh
```

Expected: no syntax errors.

**Step 3: Commit**

```bash
git add scripts/eval/e6_multipath.sh
git commit -m "eval: add E6 multi-path throughput comparison script"
```

---

### Task 3: E7 — Redundancy ratio sweep script

**Files:**
- Create: `scripts/eval/e7_ratio_sweep.sh`

This script sweeps redundancy_ratio = 1.5, 2.0, 2.5, 3.0 on 2-path topology. It dynamically patches the config files via sed before each run.

**Step 1: Write e7_ratio_sweep.sh**

```sh
#!/bin/sh
# E7: Redundancy ratio sweep
# Usage: e7_ratio_sweep.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e7_ratio_sweep.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e7_ratio_sweep.csv"

PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
COMPOSE_FILE="$PROJECT_DIR/docker-compose.multipath.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

# Backup original configs
cp "$PROJECT_DIR/config/multipath-tx.conf" "$PROJECT_DIR/config/multipath-tx.conf.bak"
cp "$PROJECT_DIR/config/multipath-rx.conf" "$PROJECT_DIR/config/multipath-rx.conf.bak"

restore_configs() {
    mv "$PROJECT_DIR/config/multipath-tx.conf.bak" "$PROJECT_DIR/config/multipath-tx.conf" 2>/dev/null || true
    mv "$PROJECT_DIR/config/multipath-rx.conf.bak" "$PROJECT_DIR/config/multipath-rx.conf" 2>/dev/null || true
}
trap 'cleanup; restore_configs' EXIT

measure_throughput() {
    docker compose -f "$COMPOSE_FILE" exec -T rx-node sh -c \
        'pkill iperf3 2>/dev/null; iperf3 -s -B 10.0.0.2 --json > /tmp/server.json 2>&1 &
         sleep 1; echo "server ready"'
    sleep 1

    docker compose -f "$COMPOSE_FILE" exec -T tx-node sh -c \
        'iperf3 -c 10.0.0.2 -t 5 > /tmp/client.txt 2>&1; echo "client done exit:$?"' \
        2>/dev/null || true
    sleep 2

    BPS=$(docker compose -f "$COMPOSE_FILE" exec -T rx-node sh -c '
        JSON=/tmp/server.json
        [ -f "$JSON" ] || { echo 0; exit; }
        grep "bits_per_second" "$JSON" | \
            awk -F"[:\t ]" "{for(i=1;i<=NF;i++) if(\$i+0>0){print \$i+0; exit}}" | \
            head -1
    ' 2>/dev/null)
    [ -z "$BPS" ] && BPS=0
    MBPS=$(awk "BEGIN { printf \"%.2f\", $BPS / 1000000 }")
}

inject_loss() {
    _iface="$1"; _loss="$2"; _delay="${3:-0}"
    docker compose -f "$COMPOSE_FILE" exec -T tx-node \
        tc qdisc del dev "$_iface" root 2>/dev/null || true
    if [ "$_delay" -gt 0 ] 2>/dev/null; then
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc add dev "$_iface" root netem loss "${_loss}%" delay "${_delay}ms"
    else
        docker compose -f "$COMPOSE_FILE" exec -T tx-node \
            tc qdisc add dev "$_iface" root netem loss "${_loss}%"
    fi
}

rm -f "$CSV"
csv_header "$CSV" "loss_pct,throughput_mbps,config"

for ratio in 1.5 2.0 2.5 3.0; do
    echo "=== ratio=${ratio} ==="

    # Patch configs
    sed -i.tmp "s/^redundancy_ratio.*/redundancy_ratio = ${ratio}/" \
        "$PROJECT_DIR/config/multipath-tx.conf" \
        "$PROJECT_DIR/config/multipath-rx.conf"

    for loss in 0 10 20 30 40 50 60; do
        cleanup
        docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
        sleep 8

        inject_loss eth0 "$loss" 1
        inject_loss eth1 "$loss" 5
        sleep 1

        measure_throughput
        echo "  ratio=${ratio} loss=${loss}% throughput=${MBPS} Mbps"
        csv_row "$CSV" "$loss" "$MBPS" "ratio=${ratio}"
    done
done

echo "[E7] Done. Results: $CSV"
```

**Step 2: Make executable and verify syntax**

```bash
chmod +x scripts/eval/e7_ratio_sweep.sh
sh -n scripts/eval/e7_ratio_sweep.sh
```

**Step 3: Commit**

```bash
git add scripts/eval/e7_ratio_sweep.sh
git commit -m "eval: add E7 redundancy ratio sweep script"
```

---

### Task 4: E8 — Link failure resilience script

**Files:**
- Create: `scripts/eval/e8_link_failure.sh`

Time-series measurement: iperf3 runs for 30s, link failure injected at t=10s, recovered at t=20s. Measures per-second throughput from iperf3 JSON intervals.

**Step 1: Write e8_link_failure.sh**

```sh
#!/bin/sh
# E8: Link failure resilience
# Usage: e8_link_failure.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e8_link_failure.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e8_link_failure.csv"

PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

cleanup_all() {
    docker compose -f "$PROJECT_DIR/docker-compose.dev.yml" down 2>/dev/null || true
    docker compose -f "$PROJECT_DIR/docker-compose.multipath.yml" down 2>/dev/null || true
    docker compose -f "$PROJECT_DIR/docker-compose.tripath.yml" down 2>/dev/null || true
}
trap cleanup_all EXIT

rm -f "$CSV"
csv_header "$CSV" "time_s,throughput_mbps,config"

# extract_intervals COMPOSE_FILE
# Reads iperf3 server JSON and extracts per-second throughput
extract_intervals() {
    _cf="$1"
    docker compose -f "$_cf" exec -T rx-node sh -c '
        JSON=/tmp/server.json
        [ -f "$JSON" ] || exit
        awk "/\"bits_per_second\"/{
            gsub(/[^0-9.]/, \"\", \$2)
            if (\$2+0 > 0) print \$2+0
        }" "$JSON"
    ' 2>/dev/null
}

# run_scenario CONFIG_LABEL COMPOSE_FILE FAIL_IFACES
# FAIL_IFACES: space-separated list of interfaces to kill at t=10s
run_scenario() {
    _label="$1"; _cf="$2"; shift 2
    _fail_ifaces="$*"

    echo "=== Scenario: $_label ==="
    cleanup_all
    docker compose -f "$_cf" up -d --build 2>/dev/null
    sleep 8

    # Add baseline delay to all tx interfaces
    for _iface in eth0 eth1 eth2; do
        docker compose -f "$_cf" exec -T tx-node \
            tc qdisc del dev "$_iface" root 2>/dev/null || true
    done
    # mmWave 1ms, Sub-6GHz 5ms, WiFi 10ms baseline delay
    docker compose -f "$_cf" exec -T tx-node \
        tc qdisc add dev eth0 root netem delay 1ms 2>/dev/null || true
    docker compose -f "$_cf" exec -T tx-node \
        tc qdisc add dev eth1 root netem delay 5ms 2>/dev/null || true
    docker compose -f "$_cf" exec -T tx-node \
        tc qdisc add dev eth2 root netem delay 10ms 2>/dev/null || true

    # Start iperf3 server
    docker compose -f "$_cf" exec -T rx-node sh -c \
        'pkill iperf3 2>/dev/null; iperf3 -s -B 10.0.0.2 --json > /tmp/server.json 2>&1 &
         sleep 1; echo "server ready"'
    sleep 1

    # Start iperf3 client for 30 seconds (background)
    docker compose -f "$_cf" exec -T tx-node sh -c \
        'iperf3 -c 10.0.0.2 -t 30 > /tmp/client.txt 2>&1; echo "client done"' \
        2>/dev/null &
    CLIENT_PID=$!

    # t=0..10: normal operation
    sleep 10

    # t=10: inject link failure (100% loss on specified interfaces)
    for _iface in $_fail_ifaces; do
        docker compose -f "$_cf" exec -T tx-node \
            tc qdisc change dev "$_iface" root netem loss 100% 2>/dev/null || \
        docker compose -f "$_cf" exec -T tx-node \
            tc qdisc add dev "$_iface" root netem loss 100% 2>/dev/null || true
        echo "  t=10s: $_iface -> 100% loss"
    done

    # t=10..20: failure period
    sleep 10

    # t=20: recover links
    for _iface in $_fail_ifaces; do
        docker compose -f "$_cf" exec -T tx-node \
            tc qdisc change dev "$_iface" root netem loss 0% 2>/dev/null || true
        echo "  t=20s: $_iface -> recovered"
    done

    # t=20..30: recovery period
    wait "$CLIENT_PID" 2>/dev/null || true
    sleep 2

    # Extract per-second throughput intervals
    _sec=0
    extract_intervals "$_cf" | while read -r _bps; do
        _mbps=$(awk "BEGIN { printf \"%.2f\", $_bps / 1000000 }")
        csv_row "$CSV" "$_sec" "$_mbps" "$_label"
        _sec=$((_sec + 1))
    done

    echo "  $_label done"
}

# Scenario 8a: 2-path, mmWave fails
run_scenario "2-path:mmwave-fail" \
    "$PROJECT_DIR/docker-compose.multipath.yml" \
    eth0

# Scenario 8b: 3-path, mmWave fails
run_scenario "3-path:mmwave-fail" \
    "$PROJECT_DIR/docker-compose.tripath.yml" \
    eth0

# Scenario 8c: 3-path, mmWave + Sub-6GHz fail
run_scenario "3-path:dual-fail" \
    "$PROJECT_DIR/docker-compose.tripath.yml" \
    eth0 eth1

echo "[E8] Done. Results: $CSV"
```

**Step 2: Make executable and verify syntax**

```bash
chmod +x scripts/eval/e8_link_failure.sh
sh -n scripts/eval/e8_link_failure.sh
```

**Step 3: Commit**

```bash
git add scripts/eval/e8_link_failure.sh
git commit -m "eval: add E8 link failure resilience script"
```

---

### Task 5: Plot scripts — E6, E7, E8

**Files:**
- Create: `scripts/eval/plot/plot_e6.py`
- Create: `scripts/eval/plot/plot_e7.py`
- Create: `scripts/eval/plot/plot_e8.py`
- Modify: `scripts/eval/plot/plot_all.sh`

**Step 1: Write plot_e6.py**

Reuses the same multi-config pattern as plot_e2.py. Called twice (symmetric + asymmetric CSV).

```python
#!/usr/bin/env python3
"""E6: Multi-path throughput comparison."""
import csv, sys
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(path):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        for row in csv.DictReader(f):
            c = row['config']
            data[c][0].append(float(row['loss_pct']))
            data[c][1].append(float(row['throughput_mbps']))
    return data

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e6_multipath_symmetric.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e6_multipath_symmetric.pdf'
    title = sys.argv[3] if len(sys.argv) > 3 else 'E6: Multi-path throughput'
    data = load(csv_path)
    fig, ax = plt.subplots(figsize=(3.5, 2.8))
    colors = {'1-path': 'C0', '2-path': 'C1', '3-path': 'C2'}
    for config, (xs, ys) in sorted(data.items()):
        pairs = sorted(zip(xs, ys))
        ax.plot([p[0] for p in pairs], [p[1] for p in pairs],
                marker='o', label=config, linewidth=1.2, markersize=4,
                color=colors.get(config))
    ax.set_xlabel('Injected loss rate (%)')
    ax.set_ylabel('Effective throughput (Mbps)')
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    ax.set_title(title, fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
```

**Step 2: Write plot_e7.py**

Same structure, config labels are "ratio=X.X".

```python
#!/usr/bin/env python3
"""E7: Redundancy ratio sweep."""
import csv, sys
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(path):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        for row in csv.DictReader(f):
            c = row['config']
            data[c][0].append(float(row['loss_pct']))
            data[c][1].append(float(row['throughput_mbps']))
    return data

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e7_ratio_sweep.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e7_ratio_sweep.pdf'
    data = load(csv_path)
    fig, ax = plt.subplots(figsize=(3.5, 2.8))
    for config, (xs, ys) in sorted(data.items()):
        pairs = sorted(zip(xs, ys))
        ax.plot([p[0] for p in pairs], [p[1] for p in pairs],
                marker='o', label=config, linewidth=1.2, markersize=4)
    ax.set_xlabel('Injected loss rate (%)')
    ax.set_ylabel('Effective throughput (Mbps)')
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    ax.set_title('E7: Redundancy ratio sweep (2-path)', fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
```

**Step 3: Write plot_e8.py**

Time-series with vertical event markers at t=10s and t=20s.

```python
#!/usr/bin/env python3
"""E8: Link failure resilience — time-series throughput."""
import csv, sys
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(path):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        for row in csv.DictReader(f):
            c = row['config']
            data[c][0].append(float(row['time_s']))
            data[c][1].append(float(row['throughput_mbps']))
    return data

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e8_link_failure.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e8_link_failure.pdf'
    data = load(csv_path)
    fig, ax = plt.subplots(figsize=(4.5, 2.8))
    for config, (xs, ys) in sorted(data.items()):
        ax.plot(xs, ys, label=config, linewidth=1.2)
    ax.axvline(x=10, color='red', linestyle='--', linewidth=0.8, alpha=0.7)
    ax.axvline(x=20, color='green', linestyle='--', linewidth=0.8, alpha=0.7)
    ax.text(10.3, ax.get_ylim()[1]*0.95, 'fail', fontsize=6, color='red')
    ax.text(20.3, ax.get_ylim()[1]*0.95, 'recover', fontsize=6, color='green')
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Throughput (Mbps)')
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=6, loc='upper right')
    ax.grid(True, alpha=0.3)
    ax.set_title('E8: Link failure resilience', fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
```

**Step 4: Update plot_all.sh to include E6–E8**

Add after the existing loop:

```sh
# E6 symmetric + asymmetric
for sub in symmetric asymmetric; do
    csv="$RESULTS_DIR/e6_multipath_${sub}.csv"
    pdf="$RESULTS_DIR/e6_multipath_${sub}.pdf"
    [ -f "$csv" ] || { echo "[SKIP] E6-${sub}: $csv not found"; continue; }
    python3 "$SCRIPT_DIR/plot_e6.py" "$csv" "$pdf" "E6: Multi-path (${sub})" && echo "[OK] E6-${sub}: $pdf"
done

# E7
csv="$RESULTS_DIR/e7_ratio_sweep.csv"
pdf="$RESULTS_DIR/e7_ratio_sweep.pdf"
[ -f "$csv" ] || echo "[SKIP] E7: $csv not found"
[ -f "$csv" ] && python3 "$SCRIPT_DIR/plot_e7.py" "$csv" "$pdf" && echo "[OK] E7: $pdf"

# E8
csv="$RESULTS_DIR/e8_link_failure.csv"
pdf="$RESULTS_DIR/e8_link_failure.pdf"
[ -f "$csv" ] || echo "[SKIP] E8: $csv not found"
[ -f "$csv" ] && python3 "$SCRIPT_DIR/plot_e8.py" "$csv" "$pdf" && echo "[OK] E8: $pdf"
```

**Step 5: Commit**

```bash
git add scripts/eval/plot/plot_e6.py scripts/eval/plot/plot_e7.py scripts/eval/plot/plot_e8.py scripts/eval/plot/plot_all.sh
git commit -m "eval: add E6/E7/E8 plot scripts, update plot_all.sh"
```

---

### Task 6: Run E6 and generate comparison plots

**Step 1: Run E6 symmetric + asymmetric**

```bash
bash scripts/eval/e6_multipath.sh
```

Expected runtime: ~30 minutes (3 topologies × 7 loss points × ~2min each for symmetric, plus 3 × 9 for asymmetric).

**Step 2: Generate plots**

```bash
python3 scripts/eval/plot/plot_e6.py results/e6_multipath_symmetric.csv results/e6_multipath_symmetric.pdf "E6: Symmetric loss"
python3 scripts/eval/plot/plot_e6.py results/e6_multipath_asymmetric.csv results/e6_multipath_asymmetric.pdf "E6: Asymmetric loss (mmWave swept)"
```

**Step 3: Verify plots show expected pattern**

- 3-path should always be >= 2-path >= 1-path at every loss point
- At 100% mmWave loss (asymmetric), 2-path and 3-path should still show non-zero throughput
- 1-path at 100% loss should be 0

**Step 4: Commit results**

```bash
git add scripts/eval/results/e6_*.csv scripts/eval/results/e6_*.pdf
git commit -m "eval: E6 multi-path throughput results and plots"
```

---

### Task 7: Run E7 and E8, generate all plots

**Step 1: Run E7 ratio sweep**

```bash
bash scripts/eval/e7_ratio_sweep.sh
```

**Step 2: Run E8 link failure**

```bash
bash scripts/eval/e8_link_failure.sh
```

**Step 3: Generate plots**

```bash
python3 scripts/eval/plot/plot_e7.py results/e7_ratio_sweep.csv results/e7_ratio_sweep.pdf
python3 scripts/eval/plot/plot_e8.py results/e8_link_failure.csv results/e8_link_failure.pdf
```

**Step 4: Verify E7 pattern**

- Higher ratio = better throughput at high loss (more redundancy)
- ratio=3.0 should survive up to ~66% loss (n/k = 3.0, can lose 2 of 3 shards per original packet)
- ratio=1.5 drops off earliest

**Step 5: Verify E8 pattern**

- During failure (t=10–20s): throughput drops but 2-path/3-path maintain some throughput
- After recovery (t>20s): throughput returns to pre-failure level
- 3-path:dual-fail is hardest (only WiFi survives)

**Step 6: Commit results**

```bash
git add scripts/eval/results/e7_*.csv scripts/eval/results/e7_*.pdf \
       scripts/eval/results/e8_*.csv scripts/eval/results/e8_*.pdf
git commit -m "eval: E7 ratio sweep and E8 link failure results"
```
