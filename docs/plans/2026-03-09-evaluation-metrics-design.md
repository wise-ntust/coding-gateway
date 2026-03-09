# coding-gateway Evaluation Metrics Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create the implementation plan from this design.

**Goal:** A comprehensive evaluation framework that supports academic paper claims, demo presentations, and deployment acceptance testing for coding-gateway.

**Context:** coding-gateway implements RLNC-based erasure coding over multi-path UDP for mmWave resilience. The core research claim is that forward error recovery through coding is substantially more effective than TCP retransmission for the sudden, complete, short-lived blockage events characteristic of 60 GHz mmWave links.

**Environment:** Docker (tc netem) first; real hardware (ZedBoard + OpenWrt) data replaces Docker CSV later without changing scripts or plots.

**Baselines:** plain UDP (no coding) and TCP.

**Priority order:** Reliability > Throughput > Latency > Adaptive behavior > Overhead

---

## Section 1: Metrics

| Dimension | What to Measure | Tool |
|-----------|-----------------|------|
| **Reliability** | Decode success rate = blocks successfully decoded / blocks transmitted | Prometheus counter or log parsing |
| **Throughput** | Effective goodput at RX TUN interface (bytes/s) | iperf3 UDP through tunnel |
| **Latency** | ① Steady-state block decoding delay (first shard → TUN write) ② Recovery latency after sudden blockage | ping RTT gap + timestamp log |
| **Adaptive behavior** | `redundancy_ratio` time series as path loss changes step-function | Log parsing (Prometheus after implementation) |
| **Overhead** | Bandwidth overhead ratio (UDP bytes sent / TUN bytes in); CPU usage | netstat + /proc; `time` command |

---

## Section 2: Experiment Suite

### E1 — Decode Success Rate vs Loss Rate (Reliability)

**Purpose:** Validate that coding-gateway maintains near-100% decode success below the theoretical loss threshold, and drops off as expected above it. Supports paper's technical claims.

**Setup:**
- Loss sweep: 0%, 10%, 20%, 30%, 40%, 50%, 60%, 70%
- Three coding configurations: `k=2 n=3` (ratio=1.5), `k=2 n=4` (ratio=2.0), `k=4 n=6` (ratio=1.5)
- Baselines: plain UDP (no coding), TCP
- 1000 blocks per data point

**Tool:** tc netem on Docker bridge, custom counting shim or log parsing for decode success/failure counters.

**Expected result:** Success rate ≈ 100% below threshold, sharp drop above it. Plain UDP declines linearly. TCP shows retransmission stalls.

**Acceptance:** decode success rate ≥ 95% at loss ≤ 20% with k=4, ratio=1.5.

---

### E2 — Effective Throughput vs Loss Rate (Throughput)

**Purpose:** Quantify how much usable goodput coding-gateway preserves under packet loss compared to baselines.

**Setup:**
- Same loss sweep as E1
- iperf3 UDP `-b 10M` through TUN tunnel, 30-second runs
- Measure receiver-side reported throughput

**Tool:** iperf3 (server on RX TUN side, client on TX TUN side), Docker.

**Expected result:** coding-gateway goodput stays high through mid-range loss. Plain UDP declines linearly. TCP fluctuates sharply due to congestion control reacting to loss.

**Acceptance:** effective goodput ≥ 70% of no-loss baseline at loss ≤ 20%.

---

### E3 — Blockage Recovery Latency (Latency — core demo experiment)

**Purpose:** Demonstrate the key research claim: FEC absorbs short blockage events with near-zero gap; TCP must retransmit and shows a measurable stall.

**Setup:**
- Blockage durations: 50ms, 100ms, 200ms, 500ms
- Inject with: `iptables -A INPUT -i eth0 -j DROP` for T ms, then remove rule
- Run continuous ping, record timestamp of last successful reply before blockage and first after
- Gap = time between them

**Tool:** ping + iptables + Docker; timestamp parsing from ping output.

**Expected result:**
- coding-gateway: gap ≤ 2× RTT (≈ 4ms on LAN) for blockage ≤ 100ms (redundancy in flight absorbs it)
- TCP: gap tracks blockage duration + retransmission timeout (visible stall)

**Acceptance:** recovery gap ≤ 2× RTT for blockage ≤ 100ms with k=2, ratio=2.0.

---

### E4 — Adaptive Strategy Step Response (Adaptive behavior)

**Purpose:** Show the closed-loop behavior of the adaptive strategy — redundancy_ratio tracks path quality changes within probe cycles, with hysteresis preventing oscillation.

**Setup:**
- Path loss sequence: 0% → 40% → 0% → 70% → 0% (60-second steps)
- Inject with tc netem, change every 60 seconds
- Record redundancy_ratio time series from log output

**Tool:** tc netem + log parsing; Prometheus scraping after exporter is implemented.

**Expected result:** ratio responds within ≤ 5 probe cycles (≤ 500ms with probe_interval_ms=100). Hysteresis visible: ratio does not oscillate at transition edges.

**Acceptance:** redundancy_ratio reaches new steady state within 5 probe cycles of loss change.

---

### E5 — Embedded Target Overhead (Resource cost — lowest priority)

**Purpose:** Confirm that coding overhead is acceptable on constrained hardware. Validate bandwidth overhead matches theory.

**Setup:**
- Bandwidth overhead: measure UDP bytes sent vs TUN bytes in during iperf3 run; compute ratio
- CPU: record CPU% during saturated iperf3 run (x86 Docker first; ZedBoard when available)

**Tool:** netstat / /proc/net/dev for byte counts; `top` or `pidstat` for CPU.

**Expected result:** overhead ratio ≤ redundancy_ratio × 1.05 (wire header overhead < 5%). CPU < 10% on x86 at 10 Mbps.

**Acceptance:** bandwidth overhead ≤ redundancy_ratio × 1.05.

---

## Section 3: Tooling and Script Structure

```
scripts/eval/
├── e1_decode_success.sh      # E1: loss sweep + decode success rate
├── e2_throughput.sh          # E2: iperf3 goodput sweep
├── e3_blockage_recovery.sh   # E3: sudden blockage recovery latency
├── e4_adaptive_step.sh       # E4: adaptive strategy step response
├── e5_overhead.sh            # E5: bandwidth overhead ratio
├── run_all_eval.sh           # Run all, output CSV per experiment
└── plot/
    ├── plot_e1.py            # matplotlib: success rate vs loss curve
    ├── plot_e2.py            # matplotlib: throughput vs loss
    ├── plot_e3.py            # bar chart: recovery latency by blockage duration
    └── plot_e4.py            # time series: redundancy_ratio vs time
```

**Data format:** Each experiment outputs one CSV with fixed columns (`loss_pct, metric_value, config, baseline`). Consistent schema lets plot scripts work unchanged when Docker data is replaced with hardware data.

**Visualization:** Python matplotlib. Output PDF/PNG at 3.5-inch width (IEEE double-column format).

**Hardware replacement:** Docker tc netem and real hardware produce identical CSV format. Replace CSV source and re-run plot scripts — no script changes needed.

---

## Section 4: Acceptance Criteria Summary

| Experiment | Pass Condition | Config |
|------------|---------------|--------|
| E1 | decode success rate ≥ 95% at loss ≤ 20% | k=4, ratio=1.5 |
| E2 | goodput ≥ 70% of no-loss baseline at loss ≤ 20% | k=4, ratio=1.5 |
| E3 | recovery gap ≤ 2× RTT for blockage ≤ 100ms | k=2, ratio=2.0 |
| E4 | redundancy_ratio settles within 5 probe cycles of loss change | probe_interval_ms=100 |
| E5 | bandwidth overhead ≤ redundancy_ratio × 1.05 | any config |
