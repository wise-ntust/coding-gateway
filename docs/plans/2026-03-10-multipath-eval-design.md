# Multi-Path Evaluation & Parameter Sweep Design

## Problem

Current evaluation (E1–E5) only tests single-path topology. All shards go through one link, so FEC cannot demonstrate its core value: distributing shards across independent paths to survive per-link failures. With k=2, n=3 on a single link, throughput collapses above 33% loss.

## Goal

Add evaluation infrastructure for 2-path and 3-path topologies, sweep codec parameters (redundancy ratio), and measure link-failure resilience. Produce comparison plots that show the benefit of multi-path RLNC under realistic heterogeneous link conditions.

## Architecture

### Docker Topologies

Three compose files, one per topology:

| File | Links | Networks |
|------|-------|----------|
| `docker-compose.dev.yml` (existing) | 1: eth0 | testnet 172.20.0.0/24 |
| `docker-compose.multipath.yml` (existing) | 2: eth0, eth1 | testnet1 172.20.0.0/24, testnet2 172.21.0.0/24 |
| `docker-compose.tripath.yml` (new) | 3: eth0, eth1, eth2 | testnet1 172.20.0.0/24, testnet2 172.21.0.0/24, testnet3 172.22.0.0/24 |

Link profiles model heterogeneous wireless:

| Interface | Role | Base delay | Loss range in eval |
|-----------|------|-----------|-------------------|
| eth0 | mmWave | 1ms | 0–60%, or 100% (blockage) |
| eth1 | Sub-6GHz | 5ms | fixed 15% (asymmetric) or swept (symmetric) |
| eth2 | WiFi | 10ms | fixed 10% (asymmetric) or swept (symmetric) |

### Config Generation

Eval scripts dynamically generate temp configs via `sed` from template files. Parameters controlled per-run:
- `REDUNDANCY_RATIO`: 1.5, 2.0, 2.5, 3.0
- Number of `[path.*]` sections: 1, 2, or 3
- ARQ on/off

### New Evaluations

**E6 — Multi-path throughput comparison**
- Compares 1-path / 2-path / 3-path on same plot
- Two sub-experiments:
  - Symmetric: all links same loss rate (0–60%)
  - Asymmetric: mmWave loss swept, Sub-6GHz=15%, WiFi=10%
- Output: `e6_multipath_symmetric.pdf`, `e6_multipath_asymmetric.pdf`

**E7 — Redundancy ratio sweep**
- Fixed 2-path topology
- Sweeps ratio=1.5, 2.0, 2.5, 3.0 at each loss point
- Output: `e7_ratio_sweep.pdf`

**E8 — Link failure resilience**
- Time-series throughput with mid-test link failure injection
- Scenarios:
  - 8a: 2-path, mmWave dies at t=10s, recovers at t=20s
  - 8b: 3-path, mmWave dies at t=10s, recovers at t=20s
  - 8c: 3-path, mmWave+Sub-6GHz die at t=10s, recover at t=20s
- Output: `e8_link_failure.pdf` (time-series with event markers)

### Plot Style

- Matplotlib, consistent with existing E1–E5 (figsize 3.5×2.8, 150 dpi)
- Multi-curve per figure, color-coded with legend
- E8 uses vertical dashed lines for failure/recovery events

## Files to Create/Modify

| Action | File |
|--------|------|
| Create | `docker-compose.tripath.yml` |
| Create | `config/tripath-tx.conf`, `config/tripath-rx.conf` |
| Modify | `config/multipath-tx.conf` (add [arq] section, fix k=2) |
| Modify | `config/multipath-rx.conf` (add [arq] section, fix k=2) |
| Create | `scripts/eval/e6_multipath.sh` |
| Create | `scripts/eval/e7_ratio_sweep.sh` |
| Create | `scripts/eval/e8_link_failure.sh` |
| Create | `scripts/eval/plot/plot_e6.py` |
| Create | `scripts/eval/plot/plot_e7.py` |
| Create | `scripts/eval/plot/plot_e8.py` |
| Modify | `scripts/eval/plot/plot_all.sh` (add E6–E8) |
