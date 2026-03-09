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

---

## Design Principles

**Zero dependencies.** The entire implementation — GF(2⁸) arithmetic, matrix operations, encode, decode, TUN interface, UDP transport, configuration — is written in portable C99 against POSIX interfaces only. No external libraries. No kernel modules. The same source compiles for x86, ARM (ZedBoard), and MIPS/ARM (OpenWrt) by changing a single `make` target.

**Transparent to endpoints.** TX2 and RX2 speak ordinary IP. They have no knowledge of shards, coding coefficients, or redundancy ratios. The gateway is entirely invisible to the application layer.

**Strategy-driven.** Redundancy ratio, path weights, and shard distribution policy are governed by a pluggable strategy engine. The same binary supports fixed redundancy for simple deployments and adaptive strategies that respond to measured path quality.

**Observable by design.** Path statistics, decode success rates, per-link loss estimates, and shard timing are exposed as Prometheus metrics from day one — not bolted on afterward. The feedback loop from observability to adaptive strategy is a first-class design concern.

---

## How It Works

### Encoding

Given a block of `k` original packets `{P₀, P₁, ..., Pₖ₋₁}`, the encoder generates `n = k + r` coded shards. Each shard `Sᵢ` is a linear combination:

```
Sᵢ = c₀·P₀ ⊕ c₁·P₁ ⊕ ... ⊕ cₖ₋₁·Pₖ₋₁
```

where `·` and `⊕` are multiplication and addition in GF(2⁸), and the coefficient vector `{c₀...cₖ₋₁}` is chosen randomly for each shard. The coefficients are transmitted alongside the shard payload in a compact header.

### Distribution

Shards are distributed across available paths according to the active strategy:

- **Fixed:** round-robin across enabled interfaces
- **Weighted:** probabilistic distribution proportional to per-path quality scores
- **Adaptive:** quality scores updated continuously from probe packets and loss measurements

### Decoding

The receiver buffers incoming shards. Once any `k` linearly independent shards for a given block have arrived, Gaussian elimination over GF(2⁸) recovers the original `k` packets. Shards arriving after decoding is complete are discarded.

### The GF(2⁸) Core

All field arithmetic is implemented via precomputed lookup tables — a 256×256 multiplication table and a 256-entry inverse table, both generated at startup from the primitive polynomial `x⁸ + x⁴ + x³ + x² + 1`. This makes every field operation O(1) with no branching, suitable for the constrained compute environment of embedded targets.

---

## Configuration

```toml
# coding-gateway.toml

[general]
mode = "tx"          # tx | rx | both
tun_name = "tun0"

[coding]
k = 3                # original shards per block
redundancy_ratio = 1.5   # generates k * ratio coded shards
max_block_size = 1400    # bytes, should not exceed path MTU

[strategy]
type = "adaptive"    # fixed | adaptive | multipath

[paths]
  [[paths.link]]
  name = "mmwave_direct"
  interface = "eth1"
  weight = 1.0
  enabled = true

  [[paths.link]]
  name = "mmwave_via_A"
  interface = "eth2"
  weight = 1.0
  enabled = true

  [[paths.link]]
  name = "mmwave_via_C"
  interface = "eth3"
  weight = 1.0
  enabled = false    # disabled = single-path mode

[observability]
metrics_port = 9090
log_level = "info"
```

Single-path mode and multi-path mode are the same binary, the same configuration file format, and the same running process. Enabling additional paths requires only setting `enabled = true` and optionally adjusting weights.

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

## Getting Started

### Step 1 — Verify codec correctness on localhost

```bash
# Terminal 1: RX mode
./coding-gateway --config config/loopback-rx.conf

# Terminal 2: TX mode
./coding-gateway --config config/loopback-tx.conf

# Terminal 3: send traffic through the TUN interface
ping 10.0.0.2
```

Intentionally drop shards by adjusting `drop_simulation_rate` in the config. Decoding should succeed as long as fewer than `r` shards per block are lost.

### Step 2 — Two machines

Deploy the TX binary on one Linux machine and the RX binary on another. Update `[paths]` to point at the remote address. Run `iperf3` through the tunnel. Observe metrics at `:9090/metrics`.

### Step 3 — Deploy to ZedBoard and OpenWrt

Cross-compile, copy the binary and config, run. The deployment procedure is identical to Step 2.

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

---

## Roadmap

- [x] GF(2⁸) arithmetic core
- [x] Systematic encode / decode
- [x] TUN/TAP interface
- [x] UDP multi-path transport
- [x] Fixed and weighted strategies
- [ ] Adaptive strategy with loss feedback
- [ ] Probe-based RTT and loss measurement
- [ ] Runtime config reload (SIGHUP)
- [ ] Prometheus metrics exporter
- [ ] Grafana dashboard
- [ ] OpenWrt package feed

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

MIT
