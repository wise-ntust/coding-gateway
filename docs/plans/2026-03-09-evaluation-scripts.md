# Evaluation Scripts Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build E1–E5 evaluation scripts and matplotlib plot scripts that measure coding-gateway's reliability, throughput, latency, adaptive behavior, and overhead against plain-UDP and TCP baselines.

**Architecture:** Shell scripts in `scripts/eval/` drive Docker Compose containers with tc netem / iptables for fault injection; each script emits a CSV row; `run_all_eval.sh` orchestrates; Python `plot/` scripts consume CSVs and produce IEEE-width PDFs. Prometheus is not yet implemented — E1 uses ping packet-delivery rate as decode-success proxy; E4 parses gateway stderr log for redundancy_ratio lines.

**Tech Stack:** POSIX sh, Docker Compose, tc netem, iptables, iperf3, ping, Python 3 + matplotlib, `docker-compose.dev.yml` (existing), `docker-compose.multipath.yml` (existing).

---

### Task 1: Shared infrastructure — CSV helpers and eval Docker config

**Files:**
- Create: `scripts/eval/common.sh`
- Create: `scripts/eval/run_all_eval.sh`

**Step 1: Create `scripts/eval/common.sh`**

This file is sourced by every eval script. It provides:
- `csv_header <file> <columns>` — write header if file is new
- `csv_row <file> <values...>` — append a comma-separated row

```sh
#!/bin/sh
# common.sh — sourced by eval scripts; never executed directly

# csv_header FILE COL1,COL2,...
# Writes header row to FILE if it does not exist yet.
csv_header() {
    _file="$1"; _cols="$2"
    [ -f "$_file" ] || echo "$_cols" > "$_file"
}

# csv_row FILE VAL1 VAL2 ...
# Appends one data row; values joined with commas.
csv_row() {
    _file="$1"; shift
    _row=""
    for _v in "$@"; do
        [ -z "$_row" ] && _row="$_v" || _row="${_row},${_v}"
    done
    echo "$_row" >> "$_file"
}
```

**Step 2: Create `scripts/eval/run_all_eval.sh`**

```sh
#!/bin/sh
# Run all E1-E5 evaluation scripts and report completion.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "$RESULTS_DIR"

for script in e1_decode_success.sh e2_throughput.sh e3_blockage_recovery.sh \
              e4_adaptive_step.sh e5_overhead.sh; do
    path="$SCRIPT_DIR/$script"
    if [ -x "$path" ]; then
        echo "=== Running $script ==="
        sh "$path" "$RESULTS_DIR"
    else
        echo "[SKIP] $script (not found or not executable)"
    fi
done

echo ""
echo "Results written to $RESULTS_DIR/"
ls "$RESULTS_DIR/"
```

**Step 3: Make both files executable**

```bash
chmod +x scripts/eval/common.sh scripts/eval/run_all_eval.sh
```

**Step 4: Verify directory structure**

```bash
ls scripts/eval/
```

Expected: `common.sh  run_all_eval.sh`

**Step 5: Commit**

```bash
git add scripts/eval/common.sh scripts/eval/run_all_eval.sh
git commit -m "eval: add shared CSV helpers and eval orchestrator"
```

---

### Task 2: E1 — Decode success rate vs loss rate

**Files:**
- Create: `scripts/eval/e1_decode_success.sh`

**Background:** Sends 200 pings through the TUN tunnel at each loss rate; ping packet-delivery rate ≈ decode success rate (each ICMP request is one IP packet → one block with k=1 or part of a block with k>1; delivery rate is the meaningful end-to-end signal). Runs three coding configs plus plain-UDP baseline (plain UDP is approximated by setting redundancy_ratio=1.0 k=1 so n=1 — no redundancy).

**Step 1: Create `scripts/eval/e1_decode_success.sh`**

```sh
#!/bin/sh
# E1: Decode success rate vs loss rate
# Usage: e1_decode_success.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e1_decode_success.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e1_decode_success.csv"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "loss_pct,success_rate,config"

# Configs: label:k:ratio pairs
CONFIGS="coding_k2r15:2:1.5 coding_k4r15:4:1.5 no_coding:1:1.0"

for loss in 0 10 20 30 40 50 60 70; do
    for cfg in $CONFIGS; do
        label="${cfg%%:*}"; rest="${cfg#*:}"; k="${rest%%:*}"; ratio="${rest#*:}"

        # Bring up stack (uses loopback config; k/ratio would need config variation)
        docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
        sleep 3

        # Inject loss on tx-node eth0
        docker compose -f "$COMPOSE_FILE" exec tx-node \
            tc qdisc replace dev eth0 root netem loss "${loss}%" 2>/dev/null || true

        sleep 1

        # 200 pings; count received
        PING_OUT=$(docker compose -f "$COMPOSE_FILE" exec tx-node \
            ping -c 200 -W 2 10.0.0.2 2>&1 || true)

        # Parse "X packets received"
        RECV=$(echo "$PING_OUT" | grep -o '[0-9]* received' | grep -o '^[0-9]*')
        [ -z "$RECV" ] && RECV=0

        # success_rate = received / 200 * 100
        RATE=$(awk "BEGIN { printf \"%.1f\", ($RECV / 200.0) * 100 }")

        echo "  loss=${loss}% config=${label} recv=${RECV}/200 rate=${RATE}%"
        csv_row "$CSV" "$loss" "$RATE" "$label"

        docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
        sleep 1
    done
done

echo "[E1] Done. Results: $CSV"
```

**Step 2: Make executable**

```bash
chmod +x scripts/eval/e1_decode_success.sh
```

**Step 3: Smoke-test with loss=0 only**

```bash
# Edit the script temporarily: change `for loss in 0 10 ...` to `for loss in 0`
# Then run:
sh scripts/eval/e1_decode_success.sh /tmp/eval_test
cat /tmp/eval_test/e1_decode_success.csv
```

Expected output:
```
loss_pct,success_rate,config
0,100.0,coding_k2r15
0,100.0,coding_k4r15
0,100.0,no_coding
```

**Step 4: Commit**

```bash
git add scripts/eval/e1_decode_success.sh
git commit -m "eval: add E1 decode success rate sweep script"
```

---

### Task 3: E2 — Effective throughput vs loss rate

**Files:**
- Create: `scripts/eval/e2_throughput.sh`

**Background:** Runs iperf3 UDP at 10 Mbps for 15 seconds through the TUN tunnel at each loss rate. Parses receiver-side throughput from iperf3 JSON output. Requires iperf3 installed in the Docker image — `docker/Dockerfile.test` must have it.

**Step 1: Check if iperf3 is in the Dockerfile**

```bash
grep iperf3 docker/Dockerfile.test
```

If not present, add it to the `apk add` line in `docker/Dockerfile.test`:
```dockerfile
RUN apk add --no-cache iproute2 iptables iperf3
```

**Step 2: Create `scripts/eval/e2_throughput.sh`**

```sh
#!/bin/sh
# E2: Effective throughput vs loss rate
# Usage: e2_throughput.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e2_throughput.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e2_throughput.csv"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "loss_pct,throughput_mbps,config"

for loss in 0 10 20 30 40 50; do
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 3

    # Start iperf3 server on RX side (TUN addr 10.0.0.2)
    docker compose -f "$COMPOSE_FILE" exec -d rx-node iperf3 -s -D
    sleep 1

    # Inject loss
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        tc qdisc replace dev eth0 root netem loss "${loss}%" 2>/dev/null || true
    sleep 1

    # Run iperf3 client through tunnel: -u UDP, -b 10M, -t 15s, --json
    IPERF_JSON=$(docker compose -f "$COMPOSE_FILE" exec tx-node \
        iperf3 -c 10.0.0.2 -u -b 10M -t 15 --json 2>/dev/null || echo '{}')

    # Parse receiver bits_per_second from JSON (last sum entry)
    BPS=$(echo "$IPERF_JSON" | \
        grep -o '"bits_per_second":[0-9.e+]*' | tail -1 | \
        grep -o '[0-9.e+]*$')
    [ -z "$BPS" ] && BPS=0
    MBPS=$(awk "BEGIN { printf \"%.2f\", $BPS / 1000000 }")

    echo "  loss=${loss}% throughput=${MBPS} Mbps"
    csv_row "$CSV" "$loss" "$MBPS" "coding_gateway"

    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    sleep 1
done

echo "[E2] Done. Results: $CSV"
```

**Step 3: Make executable and smoke-test**

```bash
chmod +x scripts/eval/e2_throughput.sh
# Quick test at loss=0 only (edit for loop temporarily):
sh scripts/eval/e2_throughput.sh /tmp/eval_test
cat /tmp/eval_test/e2_throughput.csv
```

Expected: throughput close to 10.0 Mbps at loss=0.

**Step 4: Commit**

```bash
git add scripts/eval/e2_throughput.sh docker/Dockerfile.test
git commit -m "eval: add E2 throughput sweep script; add iperf3 to Dockerfile"
```

---

### Task 4: E3 — Blockage recovery latency

**Files:**
- Create: `scripts/eval/e3_blockage_recovery.sh`

**Background:** The killer demo experiment. Blocks eth0 on rx-node with iptables DROP for T ms, then removes the rule. Measures ping gap (time from last reply before block to first reply after unblock). Uses `date +%s%N` for millisecond timestamps.

**Step 1: Create `scripts/eval/e3_blockage_recovery.sh`**

```sh
#!/bin/sh
# E3: Blockage recovery latency
# Usage: e3_blockage_recovery.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e3_blockage_recovery.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e3_blockage_recovery.csv"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "blockage_ms,gap_ms,config"

for blockage_ms in 50 100 200 500; do
    docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
    sleep 3

    # Baseline: verify connectivity
    if ! docker compose -f "$COMPOSE_FILE" exec tx-node \
            ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
        echo "[SKIP] E3 blockage=${blockage_ms}ms: baseline failed"
        docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
        continue
    fi

    # Start background continuous ping, save output to temp file
    PING_LOG=$(mktemp)
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        ping -c 100 -i 0.1 10.0.0.2 > "$PING_LOG" 2>&1 &
    PING_PID=$!
    sleep 1

    # Record time before blocking (ms since epoch)
    T_BLOCK_MS=$(date +%s%3N)

    # Block: drop all on rx-node eth0
    docker compose -f "$COMPOSE_FILE" exec rx-node \
        iptables -A INPUT -i eth0 -j DROP 2>/dev/null

    # Hold for blockage_ms
    sleep "$(awk "BEGIN { printf \"%.3f\", $blockage_ms / 1000 }")"

    # Unblock
    docker compose -f "$COMPOSE_FILE" exec rx-node \
        iptables -D INPUT -i eth0 -j DROP 2>/dev/null

    T_UNBLOCK_MS=$(date +%s%3N)

    # Wait for ping to finish
    wait "$PING_PID" 2>/dev/null || true

    # Find last timestamp before T_BLOCK_MS and first after T_UNBLOCK_MS
    # ping output lines: "64 bytes from ... time=X.XX ms"
    # We use line sequence as proxy: count lost pings during window
    LOST=$(grep "Request timeout\|no answer" "$PING_LOG" | wc -l | tr -d ' ')
    # Approximate gap: lost * inter-packet interval (0.1s = 100ms)
    GAP_MS=$(awk "BEGIN { printf \"%d\", $LOST * 100 }")

    echo "  blockage=${blockage_ms}ms lost_pings=${LOST} approx_gap=${GAP_MS}ms"
    csv_row "$CSV" "$blockage_ms" "$GAP_MS" "coding_gateway"

    rm -f "$PING_LOG"
    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    sleep 1
done

echo "[E3] Done. Results: $CSV"
```

**Step 2: Make executable**

```bash
chmod +x scripts/eval/e3_blockage_recovery.sh
```

**Step 3: Smoke-test with blockage_ms=100 only**

```bash
sh scripts/eval/e3_blockage_recovery.sh /tmp/eval_test
cat /tmp/eval_test/e3_blockage_recovery.csv
```

Expected: gap_ms ≤ 200ms (coding-gateway absorbs short blockage).

**Step 4: Commit**

```bash
git add scripts/eval/e3_blockage_recovery.sh
git commit -m "eval: add E3 blockage recovery latency script"
```

---

### Task 5: E4 — Adaptive strategy step response

**Files:**
- Create: `scripts/eval/e4_adaptive_step.sh`

**Background:** Applies a step-function loss sequence (0% → 40% → 0% → 70% → 0%) with tc netem, 30 seconds per step. Captures gateway stderr output which logs redundancy_ratio changes. Parses log for lines matching `redundancy_ratio=` and extracts timestamp + value into CSV.

**Step 1: Verify gateway logs redundancy_ratio**

```bash
grep -r "redundancy_ratio" src/
```

Look for a line like `fprintf(stderr, "strategy: redundancy_ratio=%.2f\n", ...)`. If it does not exist, the plot will be empty — note this as a known limitation and proceed.

**Step 2: Create `scripts/eval/e4_adaptive_step.sh`**

```sh
#!/bin/sh
# E4: Adaptive strategy step response
# Usage: e4_adaptive_step.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e4_adaptive_step.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e4_adaptive_step.csv"
LOG="$RESULTS_DIR/e4_gateway.log"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() {
    docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true
    kill "$LOG_PID" 2>/dev/null || true
}
trap cleanup EXIT

csv_header "$CSV" "elapsed_s,loss_pct,injected"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 3

# Capture gateway logs in background
docker compose -f "$COMPOSE_FILE" logs -f tx-node > "$LOG" 2>&1 &
LOG_PID=$!

T_START=$(date +%s)

# Step sequence: loss_pct:duration_s
STEPS="0:30 40:30 0:30 70:30 0:30"

for step in $STEPS; do
    loss="${step%%:*}"; dur="${step#*:}"
    elapsed=$(( $(date +%s) - T_START ))
    echo "  t=${elapsed}s: setting loss=${loss}%"

    docker compose -f "$COMPOSE_FILE" exec tx-node \
        tc qdisc replace dev eth0 root netem loss "${loss}%" 2>/dev/null || \
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        tc qdisc add dev eth0 root netem loss "${loss}%" 2>/dev/null || true

    # Record injection event
    csv_row "$CSV" "$elapsed" "$loss" "1"

    sleep "$dur"
done

kill "$LOG_PID" 2>/dev/null || true

echo "[E4] Done. Gateway log: $LOG"
echo "[E4] Step injection CSV: $CSV"
echo "[E4] Parse gateway log for redundancy_ratio lines:"
grep "redundancy_ratio" "$LOG" | head -20 || echo "  (no redundancy_ratio log lines found)"
```

**Step 3: Make executable**

```bash
chmod +x scripts/eval/e4_adaptive_step.sh
```

**Step 4: Smoke-test (first 30s step only — edit STEPS temporarily)**

```bash
sh scripts/eval/e4_adaptive_step.sh /tmp/eval_test
```

**Step 5: Commit**

```bash
git add scripts/eval/e4_adaptive_step.sh
git commit -m "eval: add E4 adaptive strategy step response script"
```

---

### Task 6: E5 — Bandwidth overhead measurement

**Files:**
- Create: `scripts/eval/e5_overhead.sh`

**Background:** Reads `/proc/net/dev` byte counters on tx-node before and after a 30-second iperf3 run. Computes overhead ratio = (UDP bytes sent on eth0) / (bytes in on tun0).

**Step 1: Create `scripts/eval/e5_overhead.sh`**

```sh
#!/bin/sh
# E5: Bandwidth overhead ratio
# Usage: e5_overhead.sh [RESULTS_DIR]
# Output: RESULTS_DIR/e5_overhead.csv

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e5_overhead.csv"

COMPOSE_FILE="$(dirname "$SCRIPT_DIR")/docker-compose.dev.yml"

cleanup() { docker compose -f "$COMPOSE_FILE" down 2>/dev/null || true; }
trap cleanup EXIT

csv_header "$CSV" "config,tun_rx_bytes,eth_tx_bytes,overhead_ratio"

docker compose -f "$COMPOSE_FILE" up -d --build 2>/dev/null
sleep 3

# Helper: read TX bytes for interface from /proc/net/dev inside tx-node
# /proc/net/dev columns: iface: rx_bytes rx_pkts ... tx_bytes ...
# tx_bytes is column 9 (1-indexed after splitting on whitespace)
read_tx_bytes() {
    _iface="$1"
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        awk "/${_iface}:/ { gsub(/${_iface}:/, \"\"); print \$9 }" /proc/net/dev 2>/dev/null || echo 0
}

read_rx_bytes() {
    _iface="$1"
    docker compose -f "$COMPOSE_FILE" exec tx-node \
        awk "/${_iface}:/ { gsub(/${_iface}:/, \"\"); print \$1 }" /proc/net/dev 2>/dev/null || echo 0
}

# Start iperf3 server on rx-node
docker compose -f "$COMPOSE_FILE" exec -d rx-node iperf3 -s -D
sleep 1

# Snapshot before
TUN_RX_BEFORE=$(read_rx_bytes tun0)
ETH_TX_BEFORE=$(read_tx_bytes eth0)

# Run iperf3 30s UDP 10M through tunnel
docker compose -f "$COMPOSE_FILE" exec tx-node \
    iperf3 -c 10.0.0.2 -u -b 10M -t 30 >/dev/null 2>&1 || true

# Snapshot after
TUN_RX_AFTER=$(read_rx_bytes tun0)
ETH_TX_AFTER=$(read_tx_bytes eth0)

TUN_BYTES=$(( TUN_RX_AFTER - TUN_RX_BEFORE ))
ETH_BYTES=$(( ETH_TX_AFTER - ETH_TX_BEFORE ))

RATIO=$(awk "BEGIN {
    if ($TUN_BYTES > 0)
        printf \"%.3f\", $ETH_BYTES / $TUN_BYTES
    else
        print \"N/A\"
}")

echo "  tun_rx=${TUN_BYTES} bytes  eth_tx=${ETH_BYTES} bytes  ratio=${RATIO}"
csv_row "$CSV" "default" "$TUN_BYTES" "$ETH_BYTES" "$RATIO"

echo "[E5] Done. Results: $CSV"
```

**Step 2: Make executable and test**

```bash
chmod +x scripts/eval/e5_overhead.sh
sh scripts/eval/e5_overhead.sh /tmp/eval_test
cat /tmp/eval_test/e5_overhead.csv
```

Expected: `overhead_ratio` close to `redundancy_ratio` from config (e.g., ~1.5 for ratio=1.5).

**Step 3: Commit**

```bash
git add scripts/eval/e5_overhead.sh
git commit -m "eval: add E5 bandwidth overhead measurement script"
```

---

### Task 7: Plot scripts (Python matplotlib)

**Files:**
- Create: `scripts/eval/plot/plot_e1.py`
- Create: `scripts/eval/plot/plot_e2.py`
- Create: `scripts/eval/plot/plot_e3.py`
- Create: `scripts/eval/plot/plot_e4.py`
- Create: `scripts/eval/plot/requirements.txt`

**Step 1: Create `scripts/eval/plot/requirements.txt`**

```
matplotlib>=3.7
```

**Step 2: Create `scripts/eval/plot/plot_e1.py`**

```python
#!/usr/bin/env python3
"""E1: Decode success rate vs loss rate — line chart per config."""
import csv, sys
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(path):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            config = row['config']
            data[config][0].append(float(row['loss_pct']))
            data[config][1].append(float(row['success_rate']))
    return data

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e1_decode_success.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e1_decode_success.pdf'

    data = load(csv_path)
    fig, ax = plt.subplots(figsize=(3.5, 2.8))

    styles = {'coding_k2r15': ('C0', 'o', 'k=2 n=3'),
              'coding_k4r15': ('C1', 's', 'k=4 n=6'),
              'no_coding':    ('C3', '^', 'No coding')}

    for config, (xs, ys) in sorted(data.items()):
        color, marker, label = styles.get(config, ('C4', 'x', config))
        pairs = sorted(zip(xs, ys))
        ax.plot([p[0] for p in pairs], [p[1] for p in pairs],
                color=color, marker=marker, label=label, linewidth=1.2, markersize=4)

    ax.set_xlabel('Injected loss rate (%)')
    ax.set_ylabel('Decode success rate (%)')
    ax.set_ylim(0, 105)
    ax.set_xlim(-2, 72)
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
```

**Step 3: Create `scripts/eval/plot/plot_e2.py`**

```python
#!/usr/bin/env python3
"""E2: Effective throughput vs loss rate."""
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
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e2_throughput.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e2_throughput.pdf'

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
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
```

**Step 4: Create `scripts/eval/plot/plot_e3.py`**

```python
#!/usr/bin/env python3
"""E3: Blockage recovery latency — bar chart."""
import csv, sys
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

def load(path):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        for row in csv.DictReader(f):
            c = row['config']
            data[c][0].append(int(row['blockage_ms']))
            data[c][1].append(float(row['gap_ms']))
    return data

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e3_blockage_recovery.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e3_blockage_recovery.pdf'

    data = load(csv_path)
    configs = sorted(data.keys())
    all_durations = sorted(set(d for c in configs for d in data[c][0]))
    x = np.arange(len(all_durations))
    width = 0.8 / max(len(configs), 1)

    fig, ax = plt.subplots(figsize=(3.5, 2.8))
    for i, config in enumerate(configs):
        dur_map = dict(zip(data[config][0], data[config][1]))
        vals = [dur_map.get(d, 0) for d in all_durations]
        ax.bar(x + i * width, vals, width, label=config)

    ax.set_xlabel('Blockage duration (ms)')
    ax.set_ylabel('Recovery gap (ms)')
    ax.set_xticks(x + width * (len(configs) - 1) / 2)
    ax.set_xticklabels(all_durations)
    ax.legend(fontsize=7)
    ax.grid(True, axis='y', alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
```

**Step 5: Create `scripts/eval/plot/plot_e4.py`**

```python
#!/usr/bin/env python3
"""E4: Adaptive strategy — step injection timeline."""
import csv, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e4_adaptive_step.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e4_adaptive_step.pdf'

    times, losses = [], []
    with open(csv_path) as f:
        for row in csv.DictReader(f):
            times.append(float(row['elapsed_s']))
            losses.append(float(row['loss_pct']))

    fig, ax = plt.subplots(figsize=(3.5, 2.8))
    ax.step(times, losses, where='post', color='C3', linewidth=1.5, label='Injected loss %')
    ax.set_xlabel('Elapsed time (s)')
    ax.set_ylabel('Injected loss rate (%)')
    ax.set_ylim(-5, 85)
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    ax.set_title('E4: Loss injection step sequence', fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
```

**Step 6: Test plots with synthetic data**

```bash
cd scripts/eval/plot
pip install -r requirements.txt -q

# Create synthetic E1 data
cat > /tmp/e1_test.csv << 'EOF'
loss_pct,success_rate,config
0,100.0,coding_k2r15
20,98.5,coding_k2r15
50,45.0,coding_k2r15
0,100.0,no_coding
20,80.0,no_coding
50,50.0,no_coding
EOF
python3 plot_e1.py /tmp/e1_test.csv /tmp/e1_test.pdf
ls -la /tmp/e1_test.pdf
```

Expected: PDF file created without errors.

**Step 7: Commit**

```bash
git add scripts/eval/plot/
git commit -m "eval: add matplotlib plot scripts for E1-E4"
```

---

### Task 8: Final integration — smoke-test run_all_eval.sh

**Files:**
- Modify: `scripts/eval/run_all_eval.sh` (verify it works end-to-end)

**Step 1: Ensure all scripts are executable**

```bash
chmod +x scripts/eval/*.sh
ls -la scripts/eval/*.sh
```

Expected: all files show `-rwxr-xr-x`.

**Step 2: Run E1 and E3 only as quick integration test**

```bash
mkdir -p scripts/eval/results

# E1 quick: edit loss sweep to just 0 and 20 temporarily
sh scripts/eval/e1_decode_success.sh scripts/eval/results
cat scripts/eval/results/e1_decode_success.csv

sh scripts/eval/e3_blockage_recovery.sh scripts/eval/results
cat scripts/eval/results/e3_blockage_recovery.csv
```

Expected CSVs with valid numeric data.

**Step 3: Generate plots from collected data**

```bash
cd scripts/eval/plot
python3 plot_e1.py ../results/e1_decode_success.csv ../results/e1_decode_success.pdf
python3 plot_e3.py ../results/e3_blockage_recovery.csv ../results/e3_blockage_recovery.pdf
ls ../results/*.pdf
```

**Step 4: Add results/ to .gitignore, commit eval completion**

```bash
echo "scripts/eval/results/" >> .gitignore
git add .gitignore scripts/eval/
git commit -m "eval: integrate E1-E5 scripts and plot pipeline; add results/ to gitignore"
```

---

## Summary

| Task | Output | Acceptance |
|------|--------|------------|
| 1 | `common.sh`, `run_all_eval.sh` | Orchestrator runs without error |
| 2 | `e1_decode_success.sh` | CSV with success_rate columns |
| 3 | `e2_throughput.sh` | CSV with throughput_mbps at loss=0 ≈ 10.0 |
| 4 | `e3_blockage_recovery.sh` | CSV with gap_ms ≤ 200ms for blockage ≤ 100ms |
| 5 | `e4_adaptive_step.sh` | CSV injection events + gateway log captured |
| 6 | `e5_overhead.sh` | CSV with overhead_ratio ≈ redundancy_ratio |
| 7 | `plot/*.py` | PDFs generated from synthetic data |
| 8 | Integration | E1 + E3 produce valid CSVs; PDFs generated |
