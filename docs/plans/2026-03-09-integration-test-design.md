# coding-gateway — Integration Test Suite Design

Date: 2026-03-09

## Goal

Verify that coding-gateway meets its core requirements through automated Docker-based tests that cover the full requirement spectrum: basic connectivity, erasure coding correctness, loss tolerance boundaries, and multi-path failover.

## Approach

All tests run inside Docker containers (approach A). A single entry point `scripts/run-all-tests.sh` executes all tests in sequence and reports PASS/FAIL per test with a final summary. Each test is an independent shell script that starts its own containers, runs verification, and tears down on exit.

Network faults are injected using:
- `tc qdisc add dev eth0 netem loss X%` — probabilistic UDP packet loss
- `iptables -A INPUT -i ethX -p udp -j DROP` — complete path block

---

## Test Cases

| ID | Name | Scenario | Expected | Topology |
|----|------|----------|----------|----------|
| T01 | Basic connectivity | Normal operation, no faults | PASS: 5/5 ping | Single-path |
| T02 | Loss tolerance 20% | `tc netem loss 20%` on tx→rx path | PASS: ping succeeds (k=2, n=3, can tolerate 1/3 loss) | Single-path |
| T03 | Loss exceeds redundancy | `tc netem loss 50%` | FAIL (expected): ping drops (codec cannot recover) | Single-path |
| T04 | Single-path block | `iptables DROP` on only path | FAIL (expected): tunnel goes down | Single-path |
| T05 | Multi-path failover | Two paths; block path1 with `iptables DROP`, path2 survives | PASS: tunnel continues via path2 | Dual-path |

T03 and T04 are **negative tests** — they verify that the system fails correctly at the boundary, not that everything always passes.

---

## Docker Topologies

### Single-path (T01–T04)

Reuses `docker-compose.dev.yml`:

```
tx-node (172.20.0.2) ──── testnet (172.20.0.0/24) ──── rx-node (172.20.0.3)
tun0: 10.0.0.1/30                                       tun0: 10.0.0.2/30
```

Config: `config/docker-tx.conf` / `config/docker-rx.conf` (already exist)

### Dual-path (T05)

New `docker-compose.multipath.yml`:

```
tx-node ──── testnet1 (172.20.0.0/24, eth0) ──── rx-node
tx-node ──── testnet2 (172.21.0.0/24, eth1) ──── rx-node
```

- TX configures two paths: path1 → `172.20.0.3:7000`, path2 → `172.21.0.3:7000`
- RX listens on `0.0.0.0:7000` (INADDR_ANY), receives from both networks
- To block path1: `iptables -A INPUT -i eth0 -p udp -j DROP` on rx-node
- With path1 blocked, shards still arrive via path2 (eth1)

New config files: `config/multipath-tx.conf`, `config/multipath-rx.conf`

---

## File Structure

```
scripts/
├── run-all-tests.sh              ← entry point: runs T01–T05, prints summary
├── test_01_basic.sh
├── test_02_loss_20pct.sh
├── test_03_loss_50pct.sh
├── test_04_singlepath_block.sh
└── test_05_multipath_failover.sh
config/
├── multipath-tx.conf             ← two paths, mode=both
└── multipath-rx.conf             ← mode=both, listen_port=7000
docker-compose.multipath.yml      ← dual-network topology
```

---

## Test Runner Contract

Each sub-script:
- Accepts no arguments
- Returns exit code 0 = PASS, non-zero = FAIL
- Uses `trap 'docker compose ... down' EXIT` to guarantee teardown
- Prints `[PASS] T0X: <name>` or `[FAIL] T0X: <name>` to stdout

`run-all-tests.sh`:
- Runs each script, collects exit codes
- Prints `Results: N tests, P passed, F failed`
- Exits 0 if all pass, 1 if any fail

---

## Configuration for Loss Tests

T02 (20% loss, should PASS):
- k=2, redundancy_ratio=1.5 → n=3 shards per block
- Can tolerate loss of 1 shard out of 3 (33% max)
- 20% loss → on average 0.6 shards lost per block → decoder recovers

T03 (50% loss, should FAIL):
- 50% loss → on average 1.5 shards lost per block
- With k=2, n=3: need 2 of 3 shards → if 1.5 lost on average, many blocks fail
- Expected: ping sees significant packet loss → test asserts FAIL

---

## Decisions

| Question | Decision |
|----------|----------|
| Loss injection tool | `tc netem` (in `iproute2`, already installed in Alpine image) |
| Path block tool | `iptables` (requires `iptables` package in Alpine) |
| Multi-path simulation | Two Docker networks (true L3 isolation) |
| Negative test assertion | Assert that ping **fails** (exit code non-zero or 100% loss) |
| Container cleanup | `trap` in every test script |
