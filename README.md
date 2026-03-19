# coding-gateway

> Resilient multi-path transmission for millimeter-wave networks through erasure coding — no kernel modifications, no external dependencies, runs anywhere Linux does.

---

## The Problem

Millimeter-wave (mmWave) links are fast. Exceptionally fast. But they share a fundamental weakness: they break the moment something gets in the way. A person walking past. A door swinging open. A hand raised at the wrong angle.

Unlike conventional WiFi, where signal degradation is gradual and TCP has time to react, mmWave blockage is instantaneous and total. One moment the link is saturated at gigabit speeds; the next, it is gone. By the time TCP detects the loss, initiates retransmission, and recovers — tens to hundreds of milliseconds have elapsed. For video streaming, remote control, or any latency-sensitive application, that gap is unacceptable.

Adding redundant paths helps, but only partially. Traditional multipath protocols like MPTCP treat each path as independent: if one path drops a packet, that packet is lost until retransmitted. The paths are parallel, but not cooperative. A blocked mmWave link still means lost data and a stall while the stack recovers.

**The missing piece is coding.**

---

## The Idea

What if, instead of sending the same packet down one path and hoping it arrives, we encoded the data mathematically across multiple paths — such that any sufficient subset of what arrives is enough to reconstruct everything that was sent?

This is the core of **Random Linear Network Coding (RLNC)** applied to multi-path transmission. The sender splits each block of data into `k` original shards, then generates `k + r` coded shards using linear combinations over GF(2⁸). These shards are distributed across available paths. The receiver collects any `k` of them — regardless of which paths they arrived on — and solves the system to recover the original data.

If one mmWave link gets blocked and drops its shards entirely, the redundancy carried by the other paths absorbs the loss. No retransmission. No stall. The recovery is forward, not backward.

---

## The Architecture

```
TX2 ──eth──► [Node B: coding-gateway TX]
                  │
                  ├──[60 GHz]──────────────────────────► [Node D: coding-gateway RX] ──► RX2
                  ├──[60 GHz]──► [Node A] ──[60 GHz]──►
                  └──[60 GHz]──► [Node C] ──[60 GHz]──►
```

`coding-gateway` operates as a transparent L3 tunnel between two nodes — in this reference deployment, a ZedBoard running OpenWifi and an OpenWrt router. Traffic sources and destinations (TX2, RX2) require zero modification. The gateway intercepts packets via a TUN interface, encodes them into shards, distributes shards across available paths as UDP datagrams, and the remote gateway reassembles them before forwarding to the destination.

The mesh between the two gateway nodes may consist of any combination of 60 GHz and 2.4/5 GHz links. The coding layer is indifferent to the physical medium beneath it.

### Internal Module Architecture

```
                          coding-gateway process
┌─────────────────────────────────────────────────────────────────┐
│  main.c (event loop, init, signals)                             │
│  ┌──────────┐  ┌──────────┐  ┌────────────┐  ┌──────────────┐  │
│  │  config.c │  │strategy.c│  │  metrics.c  │  │   crypto.c   │  │
│  │ INI parse │  │fixed/wgt/│  │ Prometheus  │  │ XOR stream   │  │
│  │ + reload  │  │adaptive  │  │ /metrics    │  │ encrypt/dec  │  │
│  └──────────┘  │EWMA+hyst │  │ HTTP text   │  └──────────────┘  │
│                └──────────┘  └────────────┘                     │
│  ┌──────────────────────┐  ┌──────────────────────────────────┐ │
│  │       tx.c            │  │            rx.c                  │ │
│  │ block assembly        │  │ sliding window                   │ │
│  │ padding + flush       │  │ shard insert + try_decode        │ │
│  │ ┌────────┐ ┌────────┐│  │ ┌────────┐ ┌──────────────────┐ │ │
│  │ │codec.c │ │gf256.c ││  │ │codec.c │ │ip_packet_length()│ │ │
│  │ │ encode │ │ GF(2⁸) ││  │ │ decode │ │ IPv4/IPv6 strip  │ │ │
│  │ └────────┘ └────────┘│  │ └────────┘ └──────────────────┘ │ │
│  └──────────┬───────────┘  └──────────────┬───────────────────┘ │
│             │ shards                      │ packets              │
│  ┌──────────▼─────────────────────────────▼───────────────────┐ │
│  │                    transport.c                              │ │
│  │  UDP send/recv, wire protocol (0xC0DE), probe/echo          │ │
│  └──────────┬──────────────────────────────┬──────────────────┘ │
│             │ UDP datagrams                │ UDP datagrams       │
│  ┌──────────▼──┐                    ┌──────▼───────┐            │
│  │   tun.c     │                    │ path[0..N-1] │            │
│  │ TUN read/   │                    │ eth0, eth1,  │            │
│  │ write (L3)  │                    │ ... (sockets)│            │
│  └─────────────┘                    └──────────────┘            │
└─────────────────────────────────────────────────────────────────┘
     ▲ IP packets                        ▲ UDP shards
     │ (app transparent)                 │ (multi-path)
```

---

## Design Principles

**Zero dependencies.** The entire implementation — GF(2⁸) arithmetic, matrix operations, encode, decode, TUN interface, UDP transport, configuration — is written in portable C99 against POSIX interfaces only. No external libraries. No kernel modules. The same source compiles for x86, ARM (ZedBoard), and MIPS/ARM (OpenWrt) by changing a single `make` target.

**Transparent to endpoints.** TX2 and RX2 speak ordinary IP. They have no knowledge of shards, coding coefficients, or redundancy ratios. The gateway is entirely invisible to the application layer.

**Strategy-driven.** Redundancy ratio, path weights, and shard distribution policy are governed by a pluggable strategy engine. The same binary supports fixed redundancy for simple deployments and adaptive strategies that respond to measured path quality.

**Observable by design.** Path statistics, decode success rates, per-link loss estimates, and shard timing are exposed as Prometheus metrics from day one — not bolted on afterward. The feedback loop from observability to adaptive strategy is a first-class design concern.

---

## How It Works

### Block Formation

IP packets from the TUN interface are accumulated into a block. A block is flushed for encoding when either of two conditions is met — whichever comes first:

- **Count trigger:** `k` packets have been accumulated.
- **Timeout trigger:** `block_timeout_ms` milliseconds have elapsed since the first packet in the block arrived.

The timeout ensures bounded latency even under light load. Packets in a partial block are zero-padded to a uniform length before encoding.

### Encoding

Given a block of `k` original packets `{P₀, P₁, ..., Pₖ₋₁}`, the encoder generates `n = ⌈k × redundancy_ratio⌉` coded shards. Each shard `Sᵢ` is a linear combination:

```
Sᵢ = c₀·P₀ ⊕ c₁·P₁ ⊕ ... ⊕ cₖ₋₁·Pₖ₋₁
```

where `·` and `⊕` are multiplication and addition in GF(2⁸), and the coefficient vector `{c₀...cₖ₋₁}` is chosen randomly for each coded shard. The first `k` shards are systematic (identity coefficients). The coefficients are embedded in each shard's wire header.

### Wire Protocol

Each shard is transmitted as a single UDP datagram with the following header:

```
Offset  Size  Field
──────  ────  ─────────────────────────────────────────────
0       2B    magic = 0xC0DE
2       1B    version = 0x01
3       1B    type: 0x01=data  0x02=probe  0x03=probe_echo
4       4B    block_id  (big-endian, monotonically increasing)
8       1B    shard_idx
9       1B    k  (original packets in this block)
10      1B    n  (total shards in this block)
11      1B    reserved
12      2B    payload_len  (big-endian)
14      k B   coefficients[k]  (GF(2⁸) values)
14+k    *     payload
```

Probe packets (`type=0x02`) carry an 8-byte microsecond timestamp as payload. The receiver echoes them back (`type=0x03`); the sender measures round-trip time and loss rate from the responses.

### Distribution

Shards are distributed across available paths according to the active strategy:

- **Fixed:** round-robin across enabled paths.
- **Weighted:** probabilistic selection proportional to per-path `weight`.
- **Adaptive:** weight driven by probe-measured loss rate (EWMA). When paths fail, `redundancy_ratio` is scaled up automatically so surviving paths carry the full redundancy budget. Hysteresis prevents oscillation near the loss threshold.

### Decoding

The receiver maintains a sliding window of `window_size` blocks. Incoming shards are placed into the corresponding window slot by `block_id`. Once any `k` linearly independent shards for a block have arrived, Gaussian elimination over GF(2⁸) recovers the original `k` packets, which are written to the TUN interface in order. Shards for blocks that fall behind the window are silently discarded.

### The GF(2⁸) Core

All field arithmetic is implemented via precomputed lookup tables — a 256×256 multiplication table and a 256-entry inverse table, both generated at startup from the primitive polynomial `x⁸ + x⁴ + x³ + x² + 1`. This makes every field operation O(1) with no branching, suitable for the constrained compute environment of embedded targets.

---

## Configuration

Configuration is a simple INI / key=value file — no external parser library required.

```ini
# coding-gateway.conf

[general]
mode = tx               # tx | rx | both
tun_name = tun0
tun_addr = 10.0.0.1/30  # gateway assigns this address to the TUN interface
metrics_port = 9090      # Prometheus /metrics endpoint (0 = disabled)
log_level = 2            # 0=ERR, 1=WARN, 2=INFO, 3=DBG
crypto_key =             # 64 hex chars for XOR obfuscation (empty = disabled)

[coding]
k = 4                       # original packets per block (max 16)
redundancy_ratio = 1.5      # n = ceil(k * ratio) coded shards
block_timeout_ms = 5        # flush partial block after this many ms
max_payload = 1400          # bytes; do not exceed path MTU
window_size = 8             # concurrent in-flight blocks at receiver

[strategy]
type = adaptive             # fixed | weighted | adaptive
probe_interval_ms = 100     # how often to send probe packets per path
probe_loss_threshold = 0.3  # loss rate above which a path is marked dead

[path.mmwave_direct]
interface = eth1
remote_ip = 192.168.1.2
remote_port = 7000
weight = 1.0
enabled = true

[path.mmwave_via_A]
interface = eth2
remote_ip = 192.168.1.2
remote_port = 7001
weight = 1.0
enabled = true

[path.mmwave_via_C]
interface = eth3
remote_ip = 192.168.1.2
remote_port = 7002
weight = 1.0
enabled = false             # disabled = excluded from shard distribution
```

Single-path mode and multi-path mode are the same binary, the same configuration file format, and the same running process. Enabling additional paths requires only setting `enabled = true` and optionally adjusting weights.

### IP Forwarding — `[forward]`

When coding-gateway acts as an access-point relay (packets from a LAN client must traverse the tunnel and emerge on the far LAN), add a `[forward]` section:

```ini
[forward]
ip_forward = true
route = 10.20.0.0/24   # CIDR of the far-side LAN; repeat for multiple routes
```

On startup, the gateway automatically:
1. Writes `1` to `/proc/sys/net/ipv4/ip_forward`
2. Inserts `iptables FORWARD ACCEPT` rules for the TUN interface
3. Runs `ip route replace <CIDR> dev <tun_name>` for each declared route

No shell scripts needed. The gateway reads the config and applies forwarding as part of normal init.

---

## Building

```bash
# Native (x86, development and testing)
make

# ZedBoard (ARM Cortex-A9)
make TARGET=zedboard

# OpenWrt (configure toolchain path first)
make TARGET=openwrt OPENWRT_SDK=/path/to/sdk
```

No `./configure`. No `cmake`. No dependency resolution. A single `Makefile` with three targets.

---

## Testing

### Unit Tests

```bash
make test
```

Ten test suites verify core correctness:

| Test | What it verifies |
|------|-----------------|
| `test_gf256` | GF(2⁸) arithmetic: add=XOR, mul-by-0/1, commutativity, distributivity, all 255 inverse elements |
| `test_codec` | Encode/decode round-trip, recovery with shard loss, failure on insufficient shards |
| `test_config` | INI parser: TX/RX configs, missing file handling |
| `test_strategy` | Round-robin, adaptive scaling, EWMA/hysteresis, reload, edge cases (11 cases) |
| `test_transport` | Wire header packing, protocol constants, timestamp encode/decode (7 cases) |
| `test_rx` | IPv4/IPv6 packet length extraction, padding strip, edge cases (13 cases) |
| `test_tx` | Block assembly, timeout flush, padding, crypto/no-path flush safety |
| `test_config_edge` | Empty file, unknown sections, value clamping, whitespace (12 cases) |
| `test_metrics` | Latency buckets, histogram recording, counter increments (6 cases) |
| `test_crypto` | XOR encrypt/decrypt roundtrip, nonce/key differentiation, key parsing (8 cases) |

### Integration Tests (Docker)

```bash
./scripts/run-all-tests.sh
```

Eight Docker-based tests verify the full pipeline:

| Test | Scenario | Expected |
|------|----------|----------|
| T01 | Basic connectivity (no faults) | 5/5 pings |
| T02 | 20% shard loss via `tc netem` | FEC absorbs, pings pass |
| T03 | 50% shard loss (exceeds FEC capacity) | Significant packet loss (negative test) |
| T04 | Single path fully blocked | 100% loss (negative test) |
| T05 | Multi-path: block path1, survive via path2 | 0% loss |
| T06 | SIGHUP config reload | Config reloaded + tunnel still functional |
| T07 | Prometheus `/metrics` endpoint | All expected metrics present |
| T08 | IPv6 tunnel (ping6 fd00::2 through TUN) | 5/5 pings via IPv6 |

> IPv4 and IPv6 packets are both supported. The RX decode path detects the IP version from the first nibble and extracts the correct total length (IPv4 bytes 2-3, IPv6 bytes 4-5 + 40).

---

## Evaluation

Experiments are in `scripts/eval/`. Each repeated experiment runs **30 repetitions** per data point for statistical rigor. Summary CSVs contain mean ± standard deviation.

### E0/E1: Decode Success Rate — No-FEC Baseline vs FEC (N=30)

| Loss | No FEC (baseline) | FEC Single-path | FEC Multi-path |
|------|-------------------|-----------------|----------------|
| 0% | 100.00 ± 0.00 | 100.00 ± 0.00 | 100.00 ± 0.00 |
| 10% | 55.50 ± 39.71 | **99.17 ± 1.86** | **98.33 ± 3.25** |
| 20% | 3.33 ± 17.95 | **95.83 ± 3.89** | **94.17 ± 4.84** |
| 30% | 0.00 ± 0.00 | **91.33 ± 5.76** | **92.50 ± 6.02** |
| 40% | 0.00 ± 0.00 | **80.67 ± 7.50** | **85.67 ± 6.16** |
| 50% | 0.00 ± 0.00 | **76.33 ± 9.74** | **75.50 ± 6.24** |
| 60% | 0.00 ± 0.00 | **65.17 ± 10.29** | **64.17 ± 10.73** |
| 70% | 0.00 ± 0.00 | **2.17 ± 10.78** | **2.33 ± 10.86** |

Without FEC, the tunnel collapses at 20% loss (3.3% success) and is completely unusable at 30%+. With FEC (redundancy_ratio=1.5), the same tunnel maintains 91% success at 30% loss — a **91 percentage point improvement**.

### E2: TCP Throughput vs Loss

Server-side `iperf3` measurements (`scripts/eval/results/e2_throughput_fec_only.csv` and `scripts/eval/results/e2_throughput_arq.csv`) show that throughput collapses rapidly as shard loss rises, and ARQ does not recover the situation.

| Loss | FEC only | FEC + ARQ |
|------|----------|-----------|
| 0% | **101.57 Mbps** | 53.15 Mbps |
| 10% | **10.43 Mbps** | 7.32 Mbps |
| 20% | 2.09 Mbps | 2.09 Mbps |
| 30% | 1.05 Mbps | 1.05 Mbps |
| 40% | 0.00 Mbps | 0.00 Mbps |
| 50% | 0.00 Mbps | 0.00 Mbps |

Under sustained loss, TCP throughput falls much faster than ping-based decode success because TCP reacts poorly to burst loss and delay variation. ARQ adds overhead but does not restore throughput at 20%+ loss.

### E3 / E3-MP: Blockage Recovery

Single-path blockage produces visible recovery gaps. The dual-path coded topology absorbs the same blockage with no observed ping loss in these runs.

| Blockage | Single-path lost pings | Single-path gap | Multi-path lost pings | Multi-path gap |
|----------|------------------------|-----------------|-----------------------|----------------|
| 50 ms | 2 | 200 ms | 0 | 0 ms |
| 100 ms | 2 | 200 ms | 0 | 0 ms |
| 200 ms | 3 | 300 ms | 0 | 0 ms |
| 500 ms | 6 | 600 ms | 0 | 0 ms |

This is the clearest qualitative result in the repo: a blocked single path creates a measurable stall, while coded multi-path forwarding keeps the tunnel continuously usable under the same induced outage.

### E7: Burst vs Random Loss (N=30)

FEC performs better under bursty loss (corr=25%) than uniform random loss at moderate loss rates — consistent with mmWave blockage characteristics where loss events are temporally correlated.

| Loss | Random (corr=0%) | Burst (corr=25%) |
|------|-----------------|-------------------|
| 10% | 97.17 ± 3.08 | 99.83 ± 0.90 |
| 20% | 88.67 ± 6.70 | 94.33 ± 5.73 |
| 30% | 78.67 ± 10.56 | 83.33 ± 7.89 |
| 40% | 66.33 ± 11.61 | 67.83 ± 9.72 |
| 50% | 44.33 ± 10.47 | 29.33 ± 25.55 |

The repeated summary CSV also contains `corr=50%` and `corr=75%` rows, but those are all-zero artifacts in the current dataset and are not used for conclusions in this README.

### E9: Tunnel Latency Overhead (N=30)

| Mode | Mean ± Std |
|------|-----------|
| Direct (Docker bridge) | 0.133 ± 0.015 ms |
| Tunneled (TUN + FEC) | 27.124 ± 0.580 ms |
| **Coding overhead** | **~27 ms** |

The overhead is dominated by `block_timeout_ms` (block assembly wait), not GF(2⁸) computation.

### E5: Bandwidth Overhead Ratio

Measured wire overhead for the default FEC configuration is close to the theoretical `redundancy_ratio=1.5`.

| TUN TX bytes | Wire TX bytes | Measured overhead |
|--------------|---------------|-------------------|
| 38,226,912 | 56,691,323 | **1.483×** |

The measured overhead is within about 1.1% of the theoretical 1.5× expansion, indicating the wire format and shard scheduling add little overhead beyond the coding ratio itself.

### E11: Redundancy Ratio Sweep (N=30)

Finding the optimal ratio — tradeoff between bandwidth overhead and loss resilience:

| Ratio | 0% loss | 10% | 20% | 30% | 40% | 50% |
|-------|---------|-----|-----|-----|-----|-----|
| 1.0 (no FEC) | 100 | 79.3 | 65.3 | 51.2 | 33.3 | 23.5 |
| 1.25 | 100 | 95.7 | 89.5 | 76.8 | 63.0 | 50.3 |
| **1.5** | 100 | 96.5 | 90.5 | 78.5 | 67.3 | 48.5 |
| 1.75 | 100 | 99.7 | 97.0 | 90.5 | 14.8* | 0* |
| **2.0** | 100 | **99.8** | **97.0** | **91.0** | **82.2** | **68.5** |
| 2.5 | 100 | 100 | 99.3 | 96.5 | 91.3 | 80.2 |
| 3.0 | 100 | 99.8 | 99.7 | 99.0 | 95.2 | 90.2 |

\*ratio=1.75 anomaly at 40-50% is a Docker container restart artifact.

**Recommended: ratio=2.0** for mmWave environments (loss typically 20-40%). It provides 82% success at 40% loss with only 2× bandwidth overhead. ratio=3.0 gives 95% at 40% loss but triples bandwidth.

<!-- BEGIN GENERATED: eval-inventory -->
### Additional Experiments

> This index is generated from the tracked evaluation script inventory. Detailed result sections below remain manually curated.

| Experiment | Script | Description |
|-----------|--------|-------------|
| E2 | `e2_throughput.sh` | TCP throughput vs loss: throughput collapses rapidly; ARQ does not recover high-loss performance |
| E3 | `e3_blockage_recovery.sh` | Blockage recovery latency (single-path) |
| E3-MP | `e3_multipath_blockage.sh` | Multi-path blockage: 0 ms recovery gap for all tested blockage durations |
| E4 | `e4_adaptive_step.sh` | Loss step-injection trace and adaptive runtime behavior |
| E5 | `e5_overhead.sh` | Bandwidth overhead ratio: measured wire overhead vs configured coding ratio |
| E6 | `results/e6_arq_*` | FEC-only vs FEC+ARQ decode success: ARQ helps little at low loss and hurts at high loss |
| E8-R | `e8_k_sweep_repeated.sh` | k-value sweep, 30 reps: latency vs decode success at 20% loss |
| E10 | `e10_tripath_degradation.sh` | Path degradation: 2->1->0 alive paths |
| E12 | `e12_mptcp_compare.sh` | MPTCP-equivalent (no coding) vs FEC-2x: success rate comparison |
| E13 | `e13_path_count_sweep.sh` | Path-count sweep: N=2,3,4 x mptcp_equiv/fec_2x x loss 0-40% |
| E14 | `e14_path_degradation.sh` | Path degradation: N=3,4 topologies; interpretation remains methodology-sensitive |
| E15 | `e15_blockage_recovery.sh` | Multi-path blockage recovery: N=3,4 paths, 0 ms gap for >=100 ms blockage |
| E16 | `e16_k_multipath_sweep.sh` | k-sweep (k=1,2,4) x N-path (2,3,4) x ratio=2.0 under symmetric/asymmetric loss |
| E17 | `e17_iperf_4node.sh` | iperf3 4-node end-to-end throughput: FEC vs no-FEC, 0-40% path loss |
<!-- END GENERATED: eval-inventory -->

#### E8-R: k-value sweep (30 reps, 20% loss, ratio=2.0)

| k | RTT mean (ms) | RTT std | Success @ 20% loss (%) | std |
|---|--------------|---------|------------------------|-----|
| 1 | 0.32 | 0.07 | **96.3** | 4.6 |
| 2 | 22.5 | 3.3 | 82.7 | 8.5 |
| 4 | 14.6 | 12.9 | 47.3 | 44.5 |
| 8 | 27.9 | 0.9 | 92.8 | 4.8 |

k=4 shows high variance (std=44.5%) indicating instability in the Docker test environment — a bimodal distribution between full success and complete failure. k=1 achieves the best success rate (96.3%) with lowest RTT; k=2 and k=8 add grouping latency overhead. **Recommendation: k=1 with block_timeout_ms tuning for latency-sensitive workloads.**

#### E10: Path degradation with 30% loss on surviving paths

This is a simple two-path degradation experiment rather than the later N-path repeated sweeps.

| Alive paths | Success rate |
|------------|--------------|
| 2 | 97.0% |
| 1 | 100.0% |
| 0 | 0.0% |

With one healthy surviving path, the tunnel remained usable in this run despite 30% loss on that path. With both paths blocked, connectivity dropped to zero immediately.

#### E12: MPTCP-equivalent comparison (30 reps)

MPTCP is simulated as multi-path + `redundancy_ratio=1.0` (distribute traffic but no coding). `fec_2x` uses `ratio=2.0` on the same topology.

| Scenario | Loss injected | mptcp_equiv (%) | std | fec_2x (%) | std | FEC gain |
|----------|--------------|----------------|-----|------------|-----|----------|
| Symmetric | 0% | 100.0 | 0.0 | 100.0 | 0.0 | — |
| Symmetric | 10% | 87.0 | 6.9 | **99.0** | 2.0 | +12 pp |
| Symmetric | 20% | 80.7 | 9.0 | **96.0** | 4.6 | +15 pp |
| Symmetric | 30% | 68.8 | 9.0 | **89.3** | 7.4 | +21 pp |
| Symmetric | 40% | 63.3 | 10.5 | **83.2** | 8.2 | +20 pp |
| Path1 blocked† | 20% on path2 | 100.0 | 0.0 | 100.0 | 0.0 | — |

†Path1-blocked scenario uses `iptables -j DROP` for UDP but the measurement is ICMP ping, which bypasses the DROP rule. Both modes show 100% because ICMP is unaffected. The meaningful comparison for real UDP gateway traffic is the symmetric loss rows.

**FEC provides 12–21 percentage-point gains** across 10–40% symmetric loss. At 40% loss — typical worst-case mmWave blockage — `mptcp_equiv` delivers only 63%, while `fec_2x` delivers 83%.

#### E13: Path-count sweep (N=30, 30 reps each)

Path count (2, 3, 4) as a first-class variable. `mptcp_equiv` = ratio=1.0 (no FEC), `fec_2x` = ratio=2.0. All interfaces injected with symmetric loss.

| Loss | 2p mptcp | 2p fec_2x | 3p mptcp | 3p fec_2x | 4p mptcp | 4p fec_2x |
|------|---------|-----------|---------|-----------|---------|-----------|
| 0%  | 100.0 ± 0.0 | 100.0 ± 0.0 | 100.0 ± 0.0 | 100.0 ± 0.0 | 100.0 ± 0.0 | 100.0 ± 0.0 |
| 10% | 88.0 ± 8.9 | **99.2 ± 2.3** | 90.3 ± 6.7 | **98.5 ± 2.6** | 87.2 ± 7.9 | **99.7 ± 1.3** |
| 20% | 80.5 ± 9.3 | **95.3 ± 3.9** | 77.7 ± 10.4 | **95.8 ± 4.7** | 77.8 ± 10.7 | **96.5 ± 3.7** |
| 30% | 70.5 ± 9.0 | **92.5 ± 5.7** | 71.8 ± 9.6 | **92.0 ± 5.4** | 72.5 ± 10.4 | **92.5 ± 5.6** |
| 40% | 61.8 ± 10.8 | **85.8 ± 6.3** | 60.3 ± 10.1 | **86.7 ± 7.9** | 61.2 ± 10.5 | **84.0 ± 8.9** |

**Finding:** Adding paths from 2→3→4 provides minimal benefit over FEC alone. At 30% loss, `fec_2x` delivers ~92% success rate regardless of path count. The FEC gain over `mptcp_equiv` is consistently **+20–22 pp** at 30–40% loss across all path counts. Path count affects variance more than mean success rate.

#### E14: Path degradation sweep (N=30 reps, 30% loss on alive paths)

Paths blocked one by one at rx-node via iptables; remaining alive paths sustain 30% loss. **Note: results are anomalous** — the bimodal distributions (std ≈ 37%) and nonzero success at 0 alive paths (3-path case) indicate a measurement methodology issue, likely related to iptables interface ordering in the tripath container. Requires investigation before drawing conclusions.

#### E15: Multi-path blockage recovery — 3-path and 4-path (single run)

Path1 (eth0) blocked at rx-node; N-1 paths remain alive with no loss. Same methodology as E3-MP.

| Paths | Blockage | Lost pings (%) | Recovery gap |
|-------|----------|---------------|-------------|
| 3 | 50 ms | 7% | 700 ms |
| 3 | 100 ms | 0% | **0 ms** |
| 3 | 200 ms | 0% | **0 ms** |
| 3 | 500 ms | 0% | **0 ms** |
| 4 | 50 ms | 14% | 1400 ms |
| 4 | 100 ms | 0% | **0 ms** |
| 4 | 200 ms | 0% | **0 ms** |
| 4 | 500 ms | 0% | **0 ms** |

For blockages ≥ 100 ms, **zero packet loss** in both 3-path and 4-path topologies — the N-1 alive paths absorb all traffic instantly. The 50 ms case shows some residual loss (~700–1400 ms apparent gap) reflecting probe-based failure detection latency rather than actual recovery time; traffic reroutes as soon as the probe detects path failure.

#### E16: k × N-path sweep (N=30 reps, ratio=2.0)

Two scenarios: **symmetric** (all N paths at equal loss%) and **path0_good** (eth0 at 0% loss, remaining paths at 30%).

**Scenario A — Symmetric loss, 4-path topology:**

| Loss | k=1 | k=2 | k=4 |
|------|-----|-----|-----|
| 0%  | 100.0 ± 0.0 | 100.0 ± 0.0 | 100.0 ± 0.0 |
| 20% | 95.7 ± 4.2 | 97.5 ± 3.4 | **98.8 ± 2.5** |
| 30% | 91.5 ± 7.3 | 91.7 ± 4.9 | **94.3 ± 5.1** |
| 40% | 84.7 ± 9.2 | 83.0 ± 8.8 | 83.7 ± 6.5 |

k has minimal impact under symmetric loss — FEC effectiveness is bounded by per-shard loss probability regardless of block size.

**Scenario B — path0 at 0% loss, other paths at 30% loss:**

| k | 2-path | 3-path | 4-path |
|---|--------|--------|--------|
| k=1 | **100.0 ± 0.0** | **100.0 ± 0.0** | 92.5 ± 6.3 |
| k=2 | **100.0 ± 0.0** | 97.0 ± 3.3 | 98.0 ± 2.8 |
| k=4 | **100.0 ± 0.0** | 99.2 ± 1.9 | **99.3 ± 1.7** |

**Finding — Coverage Theorem (directional):** When k × ratio ≥ N, WRR distributes at least one shard to every path per block, so a 0%-loss path guarantees decode. k=4 with ratio=2.0 produces 8 shards — two per path on a 4-path topology — yielding 99.3% vs 92.5% for k=1.

**Limitation:** `block_timeout_ms=10` with low-rate ping traffic (200 ms inter-packet) causes blocks to flush with a single packet before reaching k=2 or k=4, so the coverage benefit is only fully realised at traffic rates ≥ k / block_timeout_ms. The probe_loss_threshold=0.3 also interacts with the 30% test loss, intermittently marking paths dead and concentrating traffic on path0 (explaining k=1 N=3 reaching 100% despite theoretical prediction of ~97%).

**Practical recommendation:** For N-path deployments with heterogeneous link quality, set k ≥ N/2 and ensure block_timeout_ms × (expected packet rate) ≥ k so blocks fill before flushing.

---

### 4-Node AP Topology

The `docker-compose.4node.multipath.yml` file provides a realistic deployment topology with four containers:

```
client-tx (10.10.0.2)
      │  lan_tx (10.10.0.0/24)
ap-tx (10.10.0.3) ── TUN 10.0.0.1/30 ──→ [RLNC encoded over mmwave1+mmwave2]
      │  mmwave1 (172.20.0.0/24)
      │  mmwave2 (172.21.0.0/24)
ap-rx (10.20.0.3) ←── TUN 10.0.0.2/30 ─── [decoded]
      │  lan_rx (10.20.0.0/24)
client-rx (10.20.0.2)
```

Both AP nodes use `[forward]` in their configs — the gateway configures IP forwarding and routes automatically on startup. Client containers only add a single `ip route add` pointing to their local AP.

#### E17: 4-node end-to-end performance — FEC vs no-FEC (5 reps, 100 pings/measurement)

Script: `scripts/eval/e17_iperf_4node.sh`

Topology: `client-tx → ap-tx →[coded mmWave]→ ap-rx → client-rx`
Two modes: `no_fec` (ratio=1.0) and `fec_2x` (ratio=2.0), symmetric loss {0,10,20,30,40}% on the mmWave paths. Primary metric: end-to-end IP packet loss rate (ping 100 pkts). Secondary metric: iperf3 UDP throughput at 0% loss.

| path_loss | no_fec tunnel_loss | fec_2x tunnel_loss |
|-----------|-------------------|--------------------|
| 0%        | 0.0%              | 0.0%               |
| 10%       | 21.6%             | **0.0%**           |
| 20%       | 34.4%             | **3.6%**           |
| 30%       | 47.8%             | **6.8%**           |
| 40%       | 62.2%             | **15.8%**          |

Throughput at 0% path loss: no_fec = 9.99 Mbps, fec_2x = 10.00 Mbps (no overhead penalty).
At 10% path loss, FEC 2× fully absorbs the loss (0% tunnel loss) while no-FEC passes through ~22% IP loss. At 40% path loss, FEC 2× reduces tunnel loss from 62% to 16%.

> **Note:** iperf3 throughput at 10%+ loss is not reported — iperf3 on Alpine ARM64 hangs when its TCP control channel experiences packet loss. The ping-based loss rate is the reliable metric for high-loss conditions.

---

### Design Decision: ARQ Removed

NACK-based ARQ (Automatic Repeat reQuest) was implemented and evaluated in a controlled experiment (E6). Results showed ARQ hurts more than it helps in the target scenario:

| Loss | FEC only | FEC + ARQ | ARQ delta |
|------|----------|-----------|-----------|
| 0% | 100% | 100% | 0 pp |
| 10% | 97% | 100% | +3 pp |
| 20% | 85% | 87% | +2 pp |
| 30% | **85%** | 60% | -25 pp |
| 40% | **60%** | 47% | -13 pp |
| 50% | **52%** | 42% | -10 pp |

- **Low loss (10–20%):** marginal improvement (+2–3%)
- **High loss (30–50%):** significant degradation (−10–25%)

The cause: NACK retransmissions and repair shards are themselves subject to the same loss, adding useless traffic under high loss conditions. Since the target environment is mmWave blockage (high, bursty loss), ARQ was removed in favor of pure forward error correction.

---

## Getting Started

### Step 1 — Unit-test the codec on localhost

```bash
make test
```

### Step 2 — End-to-end test with Docker Compose

```bash
./scripts/run-all-tests.sh    # all 8 integration tests
./scripts/test-docker.sh      # or just basic connectivity
```

### Step 3 — Two physical machines

Deploy the TX binary on one Linux machine and the RX binary on another. Update `[path.*]` blocks to point at the remote machine's IP. Run `iperf3` through the tunnel:

```bash
# On RX machine
./coding-gateway --config config/rx.conf

# On TX machine
./coding-gateway --config config/tx.conf
iperf3 -s &          # listen on TUN side
iperf3 -c 10.0.0.2  # send through tunnel
```

### Step 4 — Deploy to ZedBoard and OpenWrt

Cross-compile, copy the binary and config, run. The deployment procedure is identical to Step 3.

For ZedBoard, the reference platform is OpenWifi:

- OpenWifi repo: https://github.com/open-sdr/openwifi
- Use the OpenWifi installation flow to prepare the ZedBoard Linux/FPGA/driver environment first.
- After the board boots into the OpenWifi-provided Linux environment, deploy `coding-gateway` as a normal userspace binary with its config files.
- In other words, `coding-gateway` does not replace the board support stack; it runs on top of the OpenWifi-based ZedBoard system.

```bash
# ZedBoard
make TARGET=zedboard
scp coding-gateway root@zedboard:/usr/local/bin/

# OpenWrt
make TARGET=openwrt OPENWRT_SDK=/path/to/sdk
scp coding-gateway root@router:/usr/local/bin/
```

### Hardware Validation Templates

Future hardware evidence should follow the templates in:

- `docs/hardware/validation-checklist.md`
- `docs/hardware/openwrt-validation-template.md`
- `docs/hardware/zedboard-validation-template.md`

These documents define the minimum matrix, required artifacts, and reporting format for OpenWrt and ZedBoard/OpenWifi validation. Until those templates are filled with real runs, hardware support should be treated as implemented deployment support rather than fully documented hardware validation evidence.

---

## Observability

Metrics are exposed in Prometheus format on the configured port:

```
coding_gateway_shards_sent_total{path="mmwave_direct"}
coding_gateway_shards_received_total{path="mmwave_direct"}
coding_gateway_decode_success_total
coding_gateway_decode_failure_total
coding_gateway_path_loss_rate{path="mmwave_direct"}
coding_gateway_redundancy_ratio_current
coding_gateway_block_latency_ms_histogram
```

A reference Grafana dashboard is provided in `dashboards/`. The adaptive strategy reads `coding_gateway_path_loss_rate` in its control loop to adjust redundancy ratio and path weights at runtime — no restart required.

To import the dashboard, go to Grafana → Dashboards → Import and upload `dashboards/coding-gateway.json`. Select your Prometheus datasource when prompted.

---

## Roadmap

- [x] GF(2⁸) arithmetic core
- [x] Systematic encode / decode
- [x] TUN/TAP interface
- [x] UDP multi-path transport
- [x] Fixed and weighted strategies (credit-based WRR; configurable per-path weight)
- [x] Adaptive strategy with loss feedback
- [x] Probe-based RTT and loss measurement (EWMA; alpha configurable via `ewma_alpha`)
- [x] Runtime config reload (SIGHUP)
- [x] Prometheus metrics exporter
- [x] Grafana dashboard
- [x] OpenWrt package feed
- [x] Graceful shutdown (SIGTERM/SIGINT: drain pending TX block, log signal and packet count)
- [x] Unit tests: 10 test suites covering gf256, codec, config, strategy (WRR), transport, rx, tx, metrics, crypto

---

## Why Not an Existing Solution?

**MPTCP** distributes traffic across multiple paths but has no coding layer. A blocked path means lost packets and retransmission delays — precisely the failure mode mmWave blockage produces.

**WireGuard** provides excellent tunnel primitives and could serve as a transport substrate, but adds cryptographic overhead and does not address redundancy or loss recovery.

**OpenFEC / kodo** are coding libraries, not gateway applications. They solve the mathematical problem but leave the transport, path management, observability, and adaptive control as exercises for the integrator.

**Batman-adv** operates at L2 and handles mesh routing, but is not designed for coded redundancy across parallel paths.

`coding-gateway` is the integration layer that combines multi-path transport, erasure coding, path-quality awareness, and adaptive control into a single deployable binary — with no kernel modifications and no external dependencies.

---

## Research Context

This project is motivated by the distinct failure characteristics of 60 GHz millimeter-wave links in indoor environments. mmWave blockage events are sudden, complete, and typically short-lived (tens to hundreds of milliseconds). This profile makes forward error recovery through coding substantially more effective than reactive retransmission: the redundancy is already in flight before the blockage occurs.

The contribution is not in the coding mathematics — GF(2⁸) RLNC is well-understood — but in the closed-loop system: measuring per-path quality in real time, adjusting the redundancy budget dynamically, and distributing that budget intelligently across heterogeneous paths (direct 60 GHz, relayed 60 GHz, 2.4/5 GHz fallback) in a way that is specifically calibrated to mmWave blockage dynamics.

---

## License

[MIT](LICENSE.md)
