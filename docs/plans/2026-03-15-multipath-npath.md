# N-Path Multipath Experiments Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the 2-path multipath experiments to 3-path and 4-path topologies, making path count a first-class experimental variable.

**Architecture:** Create Docker Compose infrastructure for 3-path and 4-path topologies (new networks, new config files), then implement three new experiment scripts (E13 path-count sweep, E14 path degradation, E15 blockage recovery) that mirror existing 2-path experiments. All results update README.md.

**Tech Stack:** POSIX shell, Docker Compose, `tc netem`, `iptables`, existing `coding-gateway` binary.

## Status Audit

As of 2026-03-18, the infrastructure, scripts, result artifacts, and README updates from this plan have landed in the repository.

- Completed: 3-path and 4-path Docker topologies and config files
- Completed: E13, E14, E15 experiment scripts and result CSVs
- Completed: README evaluation updates for E13, E14, E15
- Follow-up only: E14 methodology rework, because the current repeated results are anomalous and should not be treated as a firm conclusion

---

## Chunk 1: Infrastructure — 3-path and 4-path Docker topologies

### Task 1: Create `docker-compose.tripath.yml`

**Files:**
- Create: `docker-compose.tripath.yml`
- Create: `config/tripath-tx.conf`
- Create: `config/tripath-rx.conf`

Existing 2-path reference: `docker-compose.multipath.yml` uses testnet1 (172.20.0.0/24) and testnet2 (172.21.0.0/24). 3-path adds testnet3 (172.22.0.0/24).

- [x] **Step 1: Create `docker-compose.tripath.yml`**

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

- [x] **Step 2: Create `config/tripath-tx.conf`**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.1/30
listen_port = 7001

[coding]
k = 1
redundancy_ratio = 2.0
block_timeout_ms = 10
max_payload = 1400
window_size = 8

[strategy]
type = fixed
probe_interval_ms = 100
probe_loss_threshold = 0.3
# ewma_alpha = 0.2    # EWMA smoothing for probe RTT/loss (0 < alpha ≤ 1)

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

[path.path3]
interface = eth2
remote_ip = 172.22.0.3
remote_port = 7000
weight = 1.0
enabled = true
```

- [x] **Step 3: Create `config/tripath-rx.conf`**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.2/30
listen_port = 7000

[coding]
k = 1
redundancy_ratio = 2.0
block_timeout_ms = 10
max_payload = 1400
window_size = 8

[strategy]
type = fixed
probe_interval_ms = 100
probe_loss_threshold = 0.3
# ewma_alpha = 0.2    # EWMA smoothing for probe RTT/loss (0 < alpha ≤ 1)

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

[path.path3]
interface = eth2
remote_ip = 172.22.0.2
remote_port = 7001
weight = 1.0
enabled = true
```

- [x] **Step 4: Smoke-test the 3-path topology**

```bash
cd /Users/loki/coding-gateway
docker compose -f docker-compose.tripath.yml up -d --build 2>&1 | tail -5
sleep 6
docker compose -f docker-compose.tripath.yml exec -T tx-node ping -c 3 -W 2 10.0.0.2
docker compose -f docker-compose.tripath.yml down
```

Expected: ping succeeds (0% loss), containers start without errors.

- [x] **Step 5: Commit**

```bash
git add docker-compose.tripath.yml config/tripath-tx.conf config/tripath-rx.conf
git commit -m "feat: add 3-path Docker topology and config files"
```

---

### Task 2: Create `docker-compose.quadpath.yml`

**Files:**
- Create: `docker-compose.quadpath.yml`
- Create: `config/quadpath-tx.conf`
- Create: `config/quadpath-rx.conf`

4-path adds testnet4 (172.23.0.0/24) on top of tripath.

- [x] **Step 1: Create `docker-compose.quadpath.yml`**

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
      testnet4:
        ipv4_address: 172.23.0.2
    command: ["/app/coding-gateway", "--config", "/app/config/quadpath-tx.conf"]

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
      testnet4:
        ipv4_address: 172.23.0.3
    command: ["/app/coding-gateway", "--config", "/app/config/quadpath-rx.conf"]

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
  testnet4:
    driver: bridge
    ipam:
      config:
        - subnet: 172.23.0.0/24
```

- [x] **Step 2: Create `config/quadpath-tx.conf`**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.1/30
listen_port = 7001

[coding]
k = 1
redundancy_ratio = 2.0
block_timeout_ms = 10
max_payload = 1400
window_size = 8

[strategy]
type = fixed
probe_interval_ms = 100
probe_loss_threshold = 0.3
# ewma_alpha = 0.2    # EWMA smoothing for probe RTT/loss (0 < alpha ≤ 1)

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

[path.path3]
interface = eth2
remote_ip = 172.22.0.3
remote_port = 7000
weight = 1.0
enabled = true

[path.path4]
interface = eth3
remote_ip = 172.23.0.3
remote_port = 7000
weight = 1.0
enabled = true
```

- [x] **Step 3: Create `config/quadpath-rx.conf`**

```ini
[general]
mode = both
tun_name = tun0
tun_addr = 10.0.0.2/30
listen_port = 7000

[coding]
k = 1
redundancy_ratio = 2.0
block_timeout_ms = 10
max_payload = 1400
window_size = 8

[strategy]
type = fixed
probe_interval_ms = 100
probe_loss_threshold = 0.3
# ewma_alpha = 0.2    # EWMA smoothing for probe RTT/loss (0 < alpha ≤ 1)

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

[path.path3]
interface = eth2
remote_ip = 172.22.0.2
remote_port = 7001
weight = 1.0
enabled = true

[path.path4]
interface = eth3
remote_ip = 172.23.0.2
remote_port = 7001
weight = 1.0
enabled = true
```

- [x] **Step 4: Smoke-test the 4-path topology**

```bash
cd /Users/loki/coding-gateway
docker compose -f docker-compose.quadpath.yml up -d --build 2>&1 | tail -5
sleep 6
docker compose -f docker-compose.quadpath.yml exec -T tx-node ping -c 3 -W 2 10.0.0.2
docker compose -f docker-compose.quadpath.yml down
```

Expected: ping succeeds, all 4 networks visible (`ip addr` shows eth0–eth3 in the tx-node container).

- [x] **Step 5: Commit**

```bash
git add docker-compose.quadpath.yml config/quadpath-tx.conf config/quadpath-rx.conf
git commit -m "feat: add 4-path Docker topology and config files"
```

---

## Chunk 2: E13 — Path-count vs loss sweep

### Task 3: Create `scripts/eval/e13_path_count_sweep.sh`

**Files:**
- Create: `scripts/eval/e13_path_count_sweep.sh`

This is the main new experiment: for each path count N ∈ {2, 3, 4}, test mptcp_equiv (ratio=1.0) and fec_2x (ratio=2.0) at loss ∈ {0, 10, 20, 30, 40}%, 30 reps each. Answers "does more paths improve success, and does FEC still help?"

CSV columns: `paths,mode,loss_pct,rep,success_rate`
Summary columns: `paths,mode,loss_pct,mean,std,n`

Compose files and config prefixes by path count:
- 2 paths: `docker-compose.multipath.yml`, config prefix `multipath`
- 3 paths: `docker-compose.tripath.yml`, config prefix `tripath`
- 4 paths: `docker-compose.quadpath.yml`, config prefix `quadpath`

Interfaces by path count:
- 2 paths: eth0 eth1
- 3 paths: eth0 eth1 eth2
- 4 paths: eth0 eth1 eth2 eth3

- [x] **Step 1: Create the script**

```sh
#!/bin/sh
# E13: Path-count sweep — compare 2, 3, 4 paths with and without FEC
# Usage: e13_path_count_sweep.sh [RESULTS_DIR] [REPS]
#
# For each N in {2, 3, 4}:
#   mptcp_equiv = N-path topology, redundancy_ratio=1.0 (no FEC)
#   fec_2x      = N-path topology, redundancy_ratio=2.0 (FEC)
# At symmetric loss 0%, 10%, 20%, 30%, 40% — 30 reps each.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e13_path_count_sweep.csv"
SUMMARY="$RESULTS_DIR/e13_path_count_sweep_summary.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"

cleanup() {
    docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT

echo "paths,mode,loss_pct,rep,success_rate" > "$CSV"

for n_paths in 2 3 4; do
    case "$n_paths" in
        2) COMPOSE="$REPO/docker-compose.multipath.yml"
           IFACES="eth0 eth1"
           CFG_PFX="multipath" ;;
        3) COMPOSE="$REPO/docker-compose.tripath.yml"
           IFACES="eth0 eth1 eth2"
           CFG_PFX="tripath" ;;
        4) COMPOSE="$REPO/docker-compose.quadpath.yml"
           IFACES="eth0 eth1 eth2 eth3"
           CFG_PFX="quadpath" ;;
    esac
    CURRENT_COMPOSE="$COMPOSE"

    for ratio in 1.0 2.0; do
        if [ "$ratio" = "1.0" ]; then MODE="mptcp_equiv"; else MODE="fec_2x"; fi

        echo "=== ${n_paths}-path ${MODE} (ratio=${ratio}) ==="

        docker compose -f "$COMPOSE" down 2>/dev/null || true
        docker compose -f "$COMPOSE" up -d --build 2>/dev/null
        sleep 5

        # Patch ratio via SIGHUP-reloadable config
        docker compose -f "$COMPOSE" exec -T tx-node \
            sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" \
            "/app/config/${CFG_PFX}-tx.conf"
        docker compose -f "$COMPOSE" exec -T rx-node \
            sed -i "s/redundancy_ratio = .*/redundancy_ratio = ${ratio}/" \
            "/app/config/${CFG_PFX}-rx.conf"
        docker compose -f "$COMPOSE" exec -T tx-node \
            sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
        docker compose -f "$COMPOSE" exec -T rx-node \
            sh -c 'kill -HUP $(pgrep coding-gateway)' 2>/dev/null || true
        sleep 3

        # Verify baseline
        if ! docker compose -f "$COMPOSE" exec -T tx-node \
                ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
            echo "  [FAIL] ${n_paths}p ${MODE}: baseline failed"
            docker compose -f "$COMPOSE" down 2>/dev/null || true
            continue
        fi

        for loss in 0 10 20 30 40; do
            # Apply loss to all interfaces
            for iface in $IFACES; do
                docker compose -f "$COMPOSE" exec -T tx-node \
                    tc qdisc del dev "$iface" root 2>/dev/null || true
                docker compose -f "$COMPOSE" exec -T tx-node \
                    tc qdisc add dev "$iface" root netem loss "${loss}%"
            done
            sleep 1

            rep=1
            while [ "$rep" -le "$REPS" ]; do
                PING_OUT=$(docker compose -f "$COMPOSE" exec -T tx-node \
                    ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)
                LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
                    for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
                }')
                [ -z "$LOSS_PCT" ] && LOSS_PCT=100
                RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
                echo "${n_paths},${MODE},${loss},${rep},${RATE}" >> "$CSV"
                rep=$((rep + 1))
            done
            echo "  [${n_paths}p ${MODE}] loss=${loss}%: ${REPS} reps done"
        done

        docker compose -f "$COMPOSE" down 2>/dev/null || true
    done
done

# Aggregate summary
echo "paths,mode,loss_pct,mean,std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2","$3
    sum[key]+=$5; sumsq[key]+=$5*$5; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if(v<0)v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n -k2 -k3 -n >> "$SUMMARY"

echo "[E13] Done. Summary:"
cat "$SUMMARY"
```

- [x] **Step 2: Make executable**

```bash
chmod +x scripts/eval/e13_path_count_sweep.sh
```

- [x] **Step 3: Dry-run check (syntax only)**

```bash
sh -n scripts/eval/e13_path_count_sweep.sh
```

Expected: no output (syntax clean).

- [x] **Step 4: Commit**

```bash
git add scripts/eval/e13_path_count_sweep.sh
git commit -m "eval: add E13 path-count vs loss sweep script"
```

---

## Chunk 3: E14 — Path degradation for 3-path and 4-path

### Task 4: Create `scripts/eval/e14_path_degradation.sh`

**Files:**
- Create: `scripts/eval/e14_path_degradation.sh`

Extends E10 (which only tested 2-path 2→1→0 degradation) to 3-path and 4-path topologies. For each topology, paths are blocked one at a time and success rate is measured with 30% loss on surviving paths.

CSV columns: `paths,alive_paths,loss_on_alive,rep,success_rate`
Summary columns: `paths,alive_paths,mean,std,n`

Status note: implementation and repeated runs exist, but the current summary is methodologically anomalous and remains a follow-up item for redesign/revalidation.

- [x] **Step 1: Create the script**

```sh
#!/bin/sh
# E14: Path degradation sweep — 3-path and 4-path topologies
# Usage: e14_path_degradation.sh [RESULTS_DIR] [REPS]
#
# For each topology (3-path, 4-path):
#   Block paths one at a time. At each stage, measure success rate
#   with 30% loss on the remaining alive paths (30 reps).
# Extends E10 (which only covered 2-path).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
REPS="${2:-30}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e14_path_degradation.csv"
SUMMARY="$RESULTS_DIR/e14_path_degradation_summary.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"

CURRENT_COMPOSE=""
cleanup() {
    [ -n "$CURRENT_COMPOSE" ] && docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT

echo "paths,alive_paths,loss_on_alive,rep,success_rate" > "$CSV"

measure_success() {
    # $1 = compose file, $2 = n_paths (total), $3 = alive_count
    MS_COMPOSE="$1"
    MS_N="$2"
    MS_ALIVE="$3"
    MS_REP=1

    while [ "$MS_REP" -le "$REPS" ]; do
        PING_OUT=$(docker compose -f "$MS_COMPOSE" exec -T tx-node \
            ping -c 20 -i 0.2 -W 2 10.0.0.2 2>&1 || true)
        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        RATE=$(awk "BEGIN { printf \"%.1f\", 100 - $LOSS_PCT }")
        echo "${MS_N},${MS_ALIVE},30,${MS_REP},${RATE}" >> "$CSV"
        MS_REP=$((MS_REP + 1))
    done
    echo "  [${MS_N}p] alive=${MS_ALIVE}: ${REPS} reps done"
}

# ---- 3-path topology ----
COMPOSE="$REPO/docker-compose.tripath.yml"
CURRENT_COMPOSE="$COMPOSE"
echo "=== 3-path degradation ==="

docker compose -f "$COMPOSE" down 2>/dev/null || true
docker compose -f "$COMPOSE" up -d --build 2>/dev/null
sleep 6

if ! docker compose -f "$COMPOSE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E14] FAIL: 3-path baseline"
else
    # Stage 1: all 3 paths alive, 30% loss
    for iface in eth0 eth1 eth2; do
        docker compose -f "$COMPOSE" exec -T tx-node \
            tc qdisc del dev "$iface" root 2>/dev/null || true
        docker compose -f "$COMPOSE" exec -T tx-node \
            tc qdisc add dev "$iface" root netem loss 30%
    done
    sleep 2
    measure_success "$COMPOSE" 3 3

    # Stage 2: block path3 (eth2), 2 paths alive
    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth2 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth2 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 3 2

    # Stage 3: block path2 (eth1), 1 path alive
    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth1 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth1 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 3 1

    # Stage 4: block path1 (eth0), 0 paths alive
    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth0 -p udp -j DROP
    sleep 2
    measure_success "$COMPOSE" 3 0
fi
docker compose -f "$COMPOSE" down 2>/dev/null || true

# ---- 4-path topology ----
COMPOSE="$REPO/docker-compose.quadpath.yml"
CURRENT_COMPOSE="$COMPOSE"
echo "=== 4-path degradation ==="

docker compose -f "$COMPOSE" down 2>/dev/null || true
docker compose -f "$COMPOSE" up -d --build 2>/dev/null
sleep 6

if ! docker compose -f "$COMPOSE" exec -T tx-node ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
    echo "[E14] FAIL: 4-path baseline"
else
    # Stage 1: all 4 paths alive, 30% loss
    for iface in eth0 eth1 eth2 eth3; do
        docker compose -f "$COMPOSE" exec -T tx-node \
            tc qdisc del dev "$iface" root 2>/dev/null || true
        docker compose -f "$COMPOSE" exec -T tx-node \
            tc qdisc add dev "$iface" root netem loss 30%
    done
    sleep 2
    measure_success "$COMPOSE" 4 4

    # Stage 2: block path4 (eth3), 3 paths alive
    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth3 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth3 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 4 3

    # Stage 3: block path3 (eth2), 2 paths alive
    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth2 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth2 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 4 2

    # Stage 4: block path2 (eth1), 1 path alive
    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth1 -p udp -j DROP
    docker compose -f "$COMPOSE" exec -T tx-node \
        tc qdisc del dev eth1 root 2>/dev/null || true
    sleep 2
    measure_success "$COMPOSE" 4 1

    # Stage 5: block path1 (eth0), 0 paths alive
    docker compose -f "$COMPOSE" exec -T rx-node \
        iptables -A INPUT -i eth0 -p udp -j DROP
    sleep 2
    measure_success "$COMPOSE" 4 0
fi
docker compose -f "$COMPOSE" down 2>/dev/null || true

# Summary
echo "paths,alive_paths,mean,std,n" > "$SUMMARY"
awk -F, 'NR>1 {
    key=$1","$2
    sum[key]+=$5; sumsq[key]+=$5*$5; n[key]++
} END {
    for (k in sum) {
        m = sum[k]/n[k]; v = (sumsq[k]/n[k]) - m*m
        if(v<0)v=0; s = sqrt(v)
        printf "%s,%.2f,%.2f,%d\n", k, m, s, n[k]
    }
}' "$CSV" | sort -t, -k1 -n -k2 -n >> "$SUMMARY"

echo "[E14] Done. Summary:"
cat "$SUMMARY"
```

- [x] **Step 2: Make executable and syntax-check**

```bash
chmod +x scripts/eval/e14_path_degradation.sh
sh -n scripts/eval/e14_path_degradation.sh
```

Expected: no output.

- [x] **Step 3: Commit**

```bash
git add scripts/eval/e14_path_degradation.sh
git commit -m "eval: add E14 path degradation script for 3-path and 4-path"
```

---

## Chunk 4: E15 — Blockage recovery for 3-path and 4-path

### Task 5: Create `scripts/eval/e15_blockage_recovery.sh`

**Files:**
- Create: `scripts/eval/e15_blockage_recovery.sh`

Extends E3-MP (2-path blockage recovery) to 3-path and 4-path. Tests whether blocking 1 path still produces a 0 ms recovery gap when N-1 paths remain alive. blockage durations: 50, 100, 200, 500 ms.

CSV columns: `paths,blockage_ms,lost_pings,gap_ms`

- [x] **Step 1: Create the script**

```sh
#!/bin/sh
# E15: Multi-path blockage recovery — 3-path and 4-path topologies
# Usage: e15_blockage_recovery.sh [RESULTS_DIR]
#
# Extends E3-MP (2-path) to 3 and 4 paths.
# Blocks path1 (eth0) at rx-node for each blockage duration;
# N-1 paths remain alive. Measures lost_pings and effective gap_ms.
# With FEC and N-1 live paths, expect 0 ms recovery gap.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/common.sh"

RESULTS_DIR="${1:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/e15_blockage_recovery.csv"

REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"

CURRENT_COMPOSE=""
cleanup() {
    [ -n "$CURRENT_COMPOSE" ] && docker compose -f "$CURRENT_COMPOSE" down 2>/dev/null || true
}
trap cleanup EXIT

echo "paths,blockage_ms,lost_pings,gap_ms" > "$CSV"

run_blockage_test() {
    # $1 = compose file, $2 = n_paths
    local COMPOSE="$1"
    local n_paths="$2"
    CURRENT_COMPOSE="$COMPOSE"

    echo "=== ${n_paths}-path blockage recovery ==="

    for blockage_ms in 50 100 200 500; do
        docker compose -f "$COMPOSE" down 2>/dev/null || true
        docker compose -f "$COMPOSE" up -d --build 2>/dev/null
        sleep 8

        if ! docker compose -f "$COMPOSE" exec -T tx-node \
                ping -c 3 -W 2 10.0.0.2 >/dev/null 2>&1; then
            echo "  [SKIP] ${n_paths}p blockage=${blockage_ms}ms: baseline failed"
            docker compose -f "$COMPOSE" down 2>/dev/null || true
            continue
        fi

        PING_LOG=$(mktemp)
        docker compose -f "$COMPOSE" exec -T tx-node \
            ping -c 100 -i 0.1 -W 1 10.0.0.2 > "$PING_LOG" 2>&1 &
        PING_PID=$!
        sleep 1

        # Block path1 (eth0) only — all other paths remain alive
        docker compose -f "$COMPOSE" exec -T rx-node \
            iptables -A INPUT -i eth0 -p udp -j DROP

        sleep "$(awk "BEGIN { printf \"%.3f\", $blockage_ms / 1000 }")"

        docker compose -f "$COMPOSE" exec -T rx-node \
            iptables -D INPUT -i eth0 -p udp -j DROP 2>/dev/null || true

        wait "$PING_PID" 2>/dev/null || true
        PING_OUT=$(cat "$PING_LOG")
        rm -f "$PING_LOG"

        LOSS_PCT=$(echo "$PING_OUT" | awk '/packet loss/{
            for(i=1;i<=NF;i++) if($i~/%/){sub(/%/,"",$i); printf "%d",int($i+0.5); exit}
        }')
        [ -z "$LOSS_PCT" ] && LOSS_PCT=100
        LOST_PINGS=$LOSS_PCT
        GAP_MS=$((LOST_PINGS * 100))

        echo "  ${n_paths}p blockage=${blockage_ms}ms lost=${LOST_PINGS} gap=${GAP_MS}ms"
        echo "${n_paths},${blockage_ms},${LOST_PINGS},${GAP_MS}" >> "$CSV"

        docker compose -f "$COMPOSE" down 2>/dev/null || true
        sleep 1
    done
}

run_blockage_test "$REPO/docker-compose.tripath.yml"  3
run_blockage_test "$REPO/docker-compose.quadpath.yml" 4

echo "[E15] Done. Results: $CSV"
cat "$CSV"
```

- [x] **Step 2: Make executable and syntax-check**

```bash
chmod +x scripts/eval/e15_blockage_recovery.sh
sh -n scripts/eval/e15_blockage_recovery.sh
```

Expected: no output.

- [x] **Step 3: Commit**

```bash
git add scripts/eval/e15_blockage_recovery.sh
git commit -m "eval: add E15 blockage recovery script for 3-path and 4-path"
```

---

## Chunk 5: Run experiments and update README

### Task 6: Run E13, E14, E15 and update README.md

**Files:**
- Modify: `README.md` (Evaluation section)

Run all three experiments and update README with results tables.

- [x] **Step 1: Run E13 (path-count sweep, ~30-45 min)**

```bash
cd /Users/loki/coding-gateway
bash scripts/eval/e13_path_count_sweep.sh scripts/eval/results 30
```

Expected output ends with: `[E13] Done. Summary:` followed by CSV rows.

- [x] **Step 2: Read E13 summary**

```bash
cat scripts/eval/results/e13_path_count_sweep_summary.csv
```

Expected columns: `paths,mode,loss_pct,mean,std,n`

- [x] **Step 3: Run E14 (path degradation, ~15-20 min)**

```bash
bash scripts/eval/e14_path_degradation.sh scripts/eval/results 30
```

Expected output ends with: `[E14] Done. Summary:`

- [x] **Step 4: Read E14 summary**

```bash
cat scripts/eval/results/e14_path_degradation_summary.csv
```

Expected columns: `paths,alive_paths,mean,std,n`

- [x] **Step 5: Run E15 (blockage recovery, ~5-10 min)**

```bash
bash scripts/eval/e15_blockage_recovery.sh scripts/eval/results
```

Expected output ends with: `[E15] Done. Results:`

- [x] **Step 6: Read E15 results**

```bash
cat scripts/eval/results/e15_blockage_recovery.csv
```

Expected columns: `paths,blockage_ms,lost_pings,gap_ms`

- [x] **Step 7: Update README.md Additional Experiments table**

In the `### Additional Experiments` section, add E13, E14, E15 rows:

```markdown
| E13 | `e13_path_count_sweep.sh` | Path-count sweep: 2/3/4 paths × mptcp_equiv vs fec_2x |
| E14 | `e14_path_degradation.sh` | Path degradation: 3/4 paths, N→N-1→…→0 alive |
| E15 | `e15_blockage_recovery.sh` | Blockage recovery: 3/4 paths, block 1 of N |
```

- [x] **Step 8: Add E13 results table to README.md**

After the E12 section, add a new `#### E13: Path-count sweep (30 reps)` section. Use the actual numbers from Step 2. Format:

```markdown
#### E13: Path-count sweep — 2/3/4 paths × FEC (30 reps, ratio=2.0)

| Paths | Loss | mptcp_equiv (%) | std | fec_2x (%) | std |
|-------|------|----------------|-----|------------|-----|
| 2     | 10%  | XX.X           | X.X | XX.X       | X.X |
| 3     | 10%  | XX.X           | X.X | XX.X       | X.X |
| 4     | 10%  | XX.X           | X.X | XX.X       | X.X |
...
```

Fill in actual values from the summary CSV. Include a 1–2 sentence interpretation.

- [x] **Step 9: Add E14 results table to README.md**

Add a `#### E14: Path degradation (30 reps)` section after E13. Format:

```markdown
#### E14: Path degradation — success rate as paths die (30% loss, 30 reps)

| Topology | Alive paths | Success (%) | std |
|----------|------------|------------|-----|
| 3-path   | 3          | XX.X       | X.X |
| 3-path   | 2          | XX.X       | X.X |
| 3-path   | 1          | XX.X       | X.X |
| 3-path   | 0          | 0.0        | 0.0 |
| 4-path   | 4          | XX.X       | X.X |
...
```

- [x] **Step 10: Add E15 results to README.md**

Add a `#### E15: Blockage recovery — 3/4 paths (1 path blocked)` section after E14:

```markdown
#### E15: Blockage recovery — 3-path and 4-path (30 reps each)

| Paths | Blockage (ms) | Lost pings | Gap (ms) |
|-------|--------------|-----------|---------|
| 3     | 50           | X         | X       |
...
```

If all gap_ms values are 0 (expected with N-1 alive paths), note: "0 ms recovery gap confirmed for both 3-path and 4-path topologies."

- [x] **Step 11: Update roadmap in README.md**

Add to the Roadmap section:
```markdown
- [x] 3-path and 4-path Docker topologies with config files
- [x] E13/E14/E15: N-path experiments (path count as experimental variable)
```

- [x] **Step 12: Commit and push everything**

## Remaining Follow-up

- Rework E14 path degradation methodology so per-path blocking and loss injection produce trustworthy repeated results for 3-path and 4-path topologies.
- After E14 is rerun with a corrected method, refresh `scripts/eval/results/e14_*` and the corresponding README interpretation.

```bash
git add README.md scripts/eval/results/e13_*.csv scripts/eval/results/e14_*.csv scripts/eval/results/e15_*.csv
git commit -m "eval: run E13/E14/E15 N-path experiments and update README"
git push
```

Expected: push succeeds to remote.
