# Project Improvement Backlog Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Raise `coding-gateway` from a feature-complete research prototype to a more trustworthy, testable, and deployment-ready system by fixing the current credibility gaps first, then strengthening tests and evaluation depth.

**Architecture:** Execute the backlog in three phases. Phase 1 fixes correctness and observability gaps that currently weaken experiment credibility, especially path attribution and anomalous N-path degradation results. Phase 2 adds regression coverage around transport, adaptive control, and process-level behavior. Phase 3 expands evaluation quality with parameter sweeps and real-hardware validation, then folds the findings back into `README.md`.

**Tech Stack:** C99/POSIX, Docker Compose, `tc netem`, `iptables`, shell eval scripts, Makefile test/build targets, existing README/rules workflow.

---

## File Structure

### Expected new files
- `src/test_main_integration.c` — process/event-loop level regression coverage if a test harness is introduced for signal/reload behavior
- `scripts/eval/e4_adaptive_quantify.sh` — extracts adaptive response metrics into a machine-readable CSV
- `scripts/eval/e18_timeout_rate_sweep.sh` — packet-rate × `k` × `block_timeout_ms` parameter sweep
- `scripts/eval/results/e18_timeout_rate_sweep*.csv` — new summary artifacts for the sweep
- `docs/plans/2026-03-18-project-improvement-backlog.md` — this backlog plan

### Expected modified files
- `include/transport.h` — path identity fields or path-demux metadata
- `src/transport.c` — robust per-path demux and matching logic
- `src/main.c` — adaptive probe accounting, signal/reload integration hooks, possibly improved metrics plumbing
- `src/strategy.c` — if adaptive state transitions or observability need refinement
- `src/test_transport.c` — demux regression tests
- `src/test_strategy.c` — adaptive-state tests
- `Makefile` — add any new test binaries
- `scripts/eval/e14_path_degradation.sh` — corrected methodology for path blocking and loss injection
- `scripts/eval/e4_adaptive_step.sh` — if reused as stimulus generator
- `README.md` — canonical summary for new valid results and behavior changes
- `rules/documentation-sync.md` — only if the workflow itself changes

---

## Chunk 1: Correctness and Credibility Gaps

### Task 1: Implement robust per-path demux

**Files:**
- Modify: `include/transport.h`
- Modify: `src/transport.c`
- Test: `src/test_transport.c`
- Verify: `README.md` if path-identification semantics are externally documented

- [ ] **Step 1: Audit current path matching**

Read `src/transport.c` and document how `transport_recv()` currently resolves `path_idx`. Confirm the current behavior against `README.md` and `rules/roadmap.md`.

- [ ] **Step 2: Write failing transport tests for ambiguous path identity**

Add tests covering:
- same remote IP with different remote ports
- multiple enabled paths sharing the same subnet/IP family
- fallback behavior when no configured path matches

Expected: current code fails at least the same-IP/different-port case.

- [ ] **Step 3: Run the transport test binary to verify failure**

Run:
```bash
make test
```

Expected: `test_transport` fails or a new targeted transport test fails before the implementation change.

- [ ] **Step 4: Extend transport path identity**

Update the receive-side matching logic so `path_idx` is derived from enough sender information to distinguish paths reliably. At minimum, match on remote IP + UDP source port. If the current wire/runtime model makes this insufficient, add an explicit per-path identifier and parse it centrally.

- [ ] **Step 5: Update or add focused transport tests**

Cover:
- IP+port demux success
- unmatched sender handling
- no regression for existing data/probe/probe_echo parsing

- [ ] **Step 6: Re-run tests**

Run:
```bash
make test
```

Expected: all test binaries pass, including the new transport demux cases.

- [ ] **Step 7: Commit**

```bash
git add include/transport.h src/transport.c src/test_transport.c Makefile
git commit -m "fix: improve per-path transport demux"
```

---

### Task 2: Rework E14 path degradation methodology

**Files:**
- Modify: `scripts/eval/e14_path_degradation.sh`
- Verify: `scripts/eval/results/e14_path_degradation*.csv`
- Modify: `README.md`
- Reference: `docs/plans/2026-03-15-multipath-npath.md`

- [ ] **Step 1: Define the failure mode precisely**

Use the current anomalies recorded in:
- `README.md`
- `docs/plans/2026-03-15-multipath-npath.md`
- `scripts/eval/results/e14_path_degradation_summary.csv`

Success criteria: no nonzero success when `alive_paths=0`, and no obvious interface-order artifact.

- [ ] **Step 2: Replace fragile blocking logic**

Adjust the script so path blocking is tied to deterministic path identity rather than interface ordering assumptions alone. If needed, block by destination port or use a compose/config arrangement that makes path mapping explicit.

- [ ] **Step 3: Add script-level sanity checks**

Before each measurement stage, verify:
- which paths are intended alive
- which loss rules are active
- that blocked paths are actually blocked

Persist those checks in stderr/log output so a failed run is diagnosable.

- [ ] **Step 4: Dry-run syntax check**

Run:
```bash
sh -n scripts/eval/e14_path_degradation.sh
```

Expected: no output.

- [ ] **Step 5: Re-run E14**

Run:
```bash
bash scripts/eval/e14_path_degradation.sh scripts/eval/results 30
```

Expected: a fresh summary CSV without the old impossible cases.

- [ ] **Step 6: Review the new summary**

Run:
```bash
cat scripts/eval/results/e14_path_degradation_summary.csv
```

Expected: monotonic degradation or at least physically plausible behavior.

- [ ] **Step 7: Update README**

Replace the current anomalous note in `README.md` with either:
- a valid results table and interpretation, or
- a narrower, more accurate limitation statement if one issue remains.

- [ ] **Step 8: Commit**

```bash
git add scripts/eval/e14_path_degradation.sh scripts/eval/results/e14_* README.md docs/plans/2026-03-15-multipath-npath.md
git commit -m "eval: rework E14 path degradation methodology"
```

---

### Task 3: Quantify adaptive response instead of logging only step stimuli

**Files:**
- Create: `scripts/eval/e4_adaptive_quantify.sh`
- Modify: `scripts/eval/e4_adaptive_step.sh` if needed
- Modify: `src/main.c` and/or `src/strategy.c` if additional logging/metrics are required
- Modify: `README.md`

- [ ] **Step 1: Define measurable adaptive outputs**

Pick the exact metrics to extract, for example:
- time-to-mark-dead
- time-to-recover
- redundancy-ratio change delay
- path alive/dead transition count

- [ ] **Step 2: Add machine-readable instrumentation if missing**

If the current binary does not emit enough signal, add minimal structured logs or exporter-visible state so the adaptive response can be quantified without manual interpretation.

- [ ] **Step 3: Write the quantification script**

Create a script that:
- applies the same step-loss pattern as E4
- collects the adaptive metrics
- writes a CSV summary suitable for README

- [ ] **Step 4: Add or update tests if strategy instrumentation changes**

Run:
```bash
make test
```

Expected: strategy/transport/main-related changes do not regress existing tests.

- [ ] **Step 5: Run the new adaptive quantification experiment**

Run:
```bash
bash scripts/eval/e4_adaptive_quantify.sh scripts/eval/results
```

- [ ] **Step 6: Update README**

Document the measured adaptive response rather than listing E4 as an incomplete trace-only artifact.

- [ ] **Step 7: Commit**

```bash
git add scripts/eval/e4_adaptive_step.sh scripts/eval/e4_adaptive_quantify.sh src/main.c src/strategy.c README.md
git commit -m "eval: quantify adaptive response behavior"
```

---

## Chunk 2: Regression Coverage and Process-Level Tests

### Task 4: Add transport and adaptive regression coverage

**Files:**
- Modify: `src/test_transport.c`
- Modify: `src/test_strategy.c`
- Modify: `Makefile`

- [ ] **Step 1: Expand `test_transport` for probe/data edge cases**

Cover:
- probe/probe_echo path attribution
- truncated headers with ambiguous sender identity
- invalid sender/path combinations

- [ ] **Step 2: Expand `test_strategy` for adaptive transitions**

Cover:
- path dead/alive flips around threshold boundaries
- behavior when one path is persistently good and others flap
- reload preserving runtime state under active adaptive history

- [ ] **Step 3: Run tests**

Run:
```bash
make test
```

Expected: all strategy and transport tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/test_transport.c src/test_strategy.c Makefile
git commit -m "test: expand transport and adaptive regression coverage"
```

---

### Task 5: Add process-level regression checks for reload and shutdown

**Files:**
- Modify: `src/main.c`
- Test: existing Docker scripts and/or a new process-level test harness
- Modify: `README.md` only if behavior changes

- [ ] **Step 1: Choose the lowest-cost regression path**

Prefer the smallest viable approach:
- strengthen Docker tests if that is enough
- add a focused process test harness only if Docker-only validation is too coarse

- [ ] **Step 2: Add reload regression coverage**

Verify that SIGHUP-reloadable settings actually change runtime behavior and do not corrupt active state.

- [ ] **Step 3: Add shutdown/drain regression coverage**

Verify that SIGTERM/SIGINT drains a pending TX block exactly once and exits cleanly.

- [ ] **Step 4: Run unit and integration checks**

Run at minimum:
```bash
make test
./scripts/run-all-tests.sh
```

Expected: docs-only exceptions do not apply here; the runtime verification must pass.

- [ ] **Step 5: Commit**

```bash
git add src/main.c scripts/run-all-tests.sh scripts/test_*.sh README.md
git commit -m "test: add reload and shutdown regression coverage"
```

---

## Chunk 3: Higher-Value Evaluation and Deployment Readiness

### Task 6: Add timeout/rate/k parameter sweep

**Files:**
- Create: `scripts/eval/e18_timeout_rate_sweep.sh`
- Create: `scripts/eval/results/e18_timeout_rate_sweep*.csv`
- Modify: `README.md`

- [ ] **Step 1: Define the matrix**

Start with a tight, useful set:
- `k ∈ {1,2,4}`
- `block_timeout_ms ∈ {1,5,10,20}`
- packet interval or offered rate buckets representing sparse vs saturated traffic

- [ ] **Step 2: Write the script**

The script should produce both raw results and a summary CSV containing mean/std where repeated runs are used.

- [ ] **Step 3: Syntax-check**

Run:
```bash
sh -n scripts/eval/e18_timeout_rate_sweep.sh
```

- [ ] **Step 4: Execute the sweep**

Run:
```bash
bash scripts/eval/e18_timeout_rate_sweep.sh scripts/eval/results 30
```

- [ ] **Step 5: Update README**

Document the recommended operating region for:
- latency-sensitive workloads
- low-rate traffic
- heterogeneous N-path deployments

- [ ] **Step 6: Commit**

```bash
git add scripts/eval/e18_timeout_rate_sweep.sh scripts/eval/results/e18_* README.md
git commit -m "eval: add timeout-rate-k parameter sweep"
```

---

### Task 7: Add a real-hardware validation pass

**Files:**
- Modify: `README.md`
- Optionally create: `docs/` or `scripts/eval/results/` hardware-specific notes/artifacts if needed

- [ ] **Step 1: Pick the minimum viable hardware matrix**

At minimum:
- native x86 localhost baseline
- one OpenWrt run
- one ZedBoard/OpenWifi run if available

- [ ] **Step 2: Record the exact configs and topology**

Do not rely on memory. Persist:
- config files used
- path topology
- traffic generator
- measured outputs

- [ ] **Step 3: Capture a small but defendable result set**

Focus on:
- baseline connectivity
- one loss-tolerance comparison
- one blockage/failover observation

- [ ] **Step 4: Update README**

Add a short hardware-validation subsection with exact limitations, so the project no longer reads as Docker-only.

- [ ] **Step 5: Commit**

```bash
git add README.md scripts/eval/results
git commit -m "docs: add hardware validation summary"
```

---

## Recommended Execution Order

1. Task 1: robust per-path demux
2. Task 2: E14 methodology rework
3. Task 3: adaptive-response quantification
4. Task 4: transport/adaptive regression coverage
5. Task 5: reload/shutdown regression coverage
6. Task 6: timeout/rate/k parameter sweep
7. Task 7: real-hardware validation

## Success Criteria

- `README.md` no longer carries unresolved credibility gaps except explicitly bounded limitations.
- Per-path attribution is reliable enough to support adaptive control and N-path experiments.
- `make test` and `./scripts/run-all-tests.sh` provide meaningful regression protection for the most important runtime behavior.
- Evaluation results answer deployment questions, not just demonstrate that coding helps in Docker.
