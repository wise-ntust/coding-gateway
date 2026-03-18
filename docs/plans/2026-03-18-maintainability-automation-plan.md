# Maintainability Automation Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve long-term maintainability by adding layered CI automation, reducing README/result drift, and standardizing real-hardware validation workflows.

**Architecture:** Split the work into three independent chunks. First, tighten CI so docs, native tests, and Docker integration each have clear gates. Second, add a small README/evaluation synchronization tool so experiment summaries stop depending on manual transcription. Third, add reusable hardware-validation templates so OpenWrt and ZedBoard runs can be repeated and documented consistently once toolchains and hardware are available.

**Tech Stack:** GitHub Actions, shell scripts, Python 3 for markdown/CSV generation if needed, existing `Makefile` targets, Docker Compose, existing `scripts/eval/results/*.csv`, README/rules workflow.

---

## File Structure

### Expected new files
- `.github/workflows/docs.yml` — docs-only validation and README freshness checks
- `.github/workflows/native.yml` — native build and unit-test workflow
- `.github/workflows/docker-integration.yml` — Docker integration smoke workflow
- `scripts/eval/render_readme_eval.py` — generate README evaluation fragment from result summaries
- `scripts/eval/check_readme_sync.sh` — fail when README and generated fragment diverge
- `docs/hardware/openwrt-validation-template.md` — repeatable OpenWrt validation template
- `docs/hardware/zedboard-validation-template.md` — repeatable ZedBoard/OpenWifi validation template
- `docs/hardware/validation-checklist.md` — minimum matrix, evidence checklist, and artifact naming rules

### Expected modified files
- `.github/workflows/ci.yml` — either simplify into orchestrator role or retire in favor of split workflows
- `README.md` — consume generated evaluation fragment or document the generation flow
- `rules/documentation-sync.md` — define README/evaluation sync enforcement
- `rules/build-and-test.md` — document CI expectations if workflow changes become normative
- `scripts/eval/run_all_eval.sh` — optionally hook rendering/check steps into the eval pipeline
- `docs/plans/2026-03-18-project-improvement-backlog.md` — mark follow-on maintainability work once this plan lands

---

## Chunk 1: Layered CI Gates

### Task 1: Audit the current CI workflow and split responsibilities

**Files:**
- Modify: `.github/workflows/ci.yml`
- Create: `.github/workflows/docs.yml`
- Create: `.github/workflows/native.yml`
- Create: `.github/workflows/docker-integration.yml`
- Verify: `README.md`

- [ ] **Step 1: Read the current CI workflow**

Open `.github/workflows/ci.yml` and list which jobs are docs-only, native-only, and Docker-dependent.

- [ ] **Step 2: Write down the target CI contract**

Document the intended gates in the plan implementation notes:
- docs changes must validate markdown/rule consistency and README freshness
- code changes must run native build plus `make test`
- Docker-related changes must run `./scripts/run-all-tests.sh`

- [ ] **Step 3: Create the docs workflow**

Add `.github/workflows/docs.yml` with triggers for:
- `README.md`
- `AGENTS.md`
- `CLAUDE.md`
- `rules/**`
- `docs/**`
- `scripts/eval/results/**`

Run commands:
```bash
git diff --check
./scripts/eval/check_readme_sync.sh
```

- [ ] **Step 4: Create the native workflow**

Add `.github/workflows/native.yml` with triggers for:
- `src/**`
- `include/**`
- `Makefile`
- `config/**`

Run commands:
```bash
make native
make test
```

- [ ] **Step 5: Create the Docker integration workflow**

Add `.github/workflows/docker-integration.yml` with triggers for:
- `src/**`
- `include/**`
- `scripts/test_*.sh`
- `scripts/run-all-tests.sh`
- `config/docker-*.conf`

Run command:
```bash
./scripts/run-all-tests.sh
```

- [ ] **Step 6: Decide how `ci.yml` should behave**

Either:
- keep `.github/workflows/ci.yml` as a thin dispatcher/status aggregator, or
- replace it entirely and remove duplicated logic

Pick one approach and update the file(s) accordingly.

- [ ] **Step 7: Validate workflow syntax**

Run:
```bash
python3 - <<'PY'
import yaml, pathlib
for path in pathlib.Path('.github/workflows').glob('*.yml'):
    with open(path, 'r', encoding='utf-8') as f:
        yaml.safe_load(f)
    print(f"OK {path}")
PY
```

Expected: every workflow prints `OK`.

- [ ] **Step 8: Update rules if CI is now normative**

If workflow split becomes the official verification path, update `rules/build-and-test.md` to mention the three CI gate types.

- [ ] **Step 9: Commit**

```bash
git add .github/workflows README.md rules/build-and-test.md
git commit -m "ci: split docs, native, and docker verification"
```

---

## Chunk 2: README and Evaluation Sync Automation

### Task 2: Generate README evaluation content from result summaries

**Files:**
- Create: `scripts/eval/render_readme_eval.py`
- Create: `scripts/eval/check_readme_sync.sh`
- Modify: `README.md`
- Modify: `rules/documentation-sync.md`
- Optionally modify: `scripts/eval/run_all_eval.sh`

- [ ] **Step 1: Define the generation boundary**

Choose exactly which README section becomes generated. Keep the generated block narrow:
- only the Evaluation experiment index and summary tables
- keep narrative interpretation above and below manually edited

- [ ] **Step 2: Add markers to README**

Insert stable markers like:
```md
<!-- BEGIN GENERATED: eval-summary -->
<!-- END GENERATED: eval-summary -->
```

Place them around the section that should be regenerated.

- [ ] **Step 3: Implement the renderer with fixed inputs**

Create `scripts/eval/render_readme_eval.py` that:
- reads selected CSV summary files from `scripts/eval/results/`
- formats deterministic markdown tables
- replaces only the marked block in `README.md`

The first version should only cover summaries already used in README, for example:
- `e0_baseline_summary.csv`
- `e1_repeated_summary.csv`
- `e8_k_sweep_repeated_summary.csv`
- `e11_ratio_sweep_summary.csv`
- `e12_mptcp_compare_summary.csv`
- `e13_path_count_sweep_summary.csv`
- `e14_path_degradation_summary.csv`
- `e16_k_multipath_sweep_summary.csv`
- `e17_iperf_4node_summary.csv`

- [ ] **Step 4: Make the renderer deterministic**

Ensure:
- fixed experiment ordering
- fixed decimal precision
- explicit handling for missing or anomalous rows
- no dependence on locale or CSV row ordering

- [ ] **Step 5: Add a sync-check script**

Create `scripts/eval/check_readme_sync.sh` that:
- copies `README.md` to a temp file
- runs the renderer on the temp file
- diffs the temp file against the committed README
- exits nonzero if they differ

- [ ] **Step 6: Run the renderer once and review the diff**

Run:
```bash
python3 scripts/eval/render_readme_eval.py README.md
git diff -- README.md
```

Expected: README changes are limited to the generated block and reflect current summary CSVs.

- [ ] **Step 7: Wire the sync check into docs validation**

Either:
- call `./scripts/eval/check_readme_sync.sh` from `.github/workflows/docs.yml`, or
- call it from an existing docs validation entrypoint

- [ ] **Step 8: Update the documentation rule**

Update `rules/documentation-sync.md` so it explicitly says:
- `README.md` remains canonical
- generated fragments are allowed
- result summaries must be rendered before commit when they affect the generated block

- [ ] **Step 9: Optionally hook the renderer into eval execution**

If it reduces drift, update `scripts/eval/run_all_eval.sh` to print a reminder or optional command:
```bash
python3 scripts/eval/render_readme_eval.py README.md
```

- [ ] **Step 10: Verify the sync tooling**

Run:
```bash
./scripts/eval/check_readme_sync.sh
```

Expected: exit 0 when README is current.

- [ ] **Step 11: Commit**

```bash
git add README.md rules/documentation-sync.md scripts/eval/render_readme_eval.py scripts/eval/check_readme_sync.sh scripts/eval/run_all_eval.sh .github/workflows/docs.yml
git commit -m "docs: automate README evaluation sync"
```

---

## Chunk 3: Hardware Validation Templates

### Task 3: Standardize OpenWrt and ZedBoard validation workflows

**Files:**
- Create: `docs/hardware/validation-checklist.md`
- Create: `docs/hardware/openwrt-validation-template.md`
- Create: `docs/hardware/zedboard-validation-template.md`
- Modify: `README.md`
- Modify: `docs/plans/2026-03-18-project-improvement-backlog.md`

- [ ] **Step 1: Define the minimum hardware matrix**

Write the minimum acceptable validation set:
- one OpenWrt build + deploy + smoke run
- one ZedBoard/OpenWifi build + deploy + smoke run
- one metrics capture per platform
- one result artifact per platform stored under a documented naming convention

- [ ] **Step 2: Create the checklist document**

Add `docs/hardware/validation-checklist.md` with:
- prerequisites
- toolchain expectations
- deploy inputs
- smoke-test commands
- metrics/log artifacts to capture
- pass/fail criteria

- [ ] **Step 3: Create the OpenWrt template**

Add `docs/hardware/openwrt-validation-template.md` with sections for:
- SDK/toolchain version
- target board and firmware
- package install steps
- service/config deployment
- smoke test results
- metrics/log attachment references
- deviations and limitations

- [ ] **Step 4: Create the ZedBoard template**

Add `docs/hardware/zedboard-validation-template.md` with sections for:
- cross-compiler version
- OpenWifi/bitstream/software context
- gateway binary deployment
- tunnel bring-up checklist
- smoke test results
- metrics/log attachment references
- deviations and limitations

- [ ] **Step 5: Link the templates from README**

Add a short subsection in `README.md` near deployment or validation that points to:
- `docs/hardware/validation-checklist.md`
- `docs/hardware/openwrt-validation-template.md`
- `docs/hardware/zedboard-validation-template.md`

Make it clear these are the required formats for future hardware evidence.

- [ ] **Step 6: Update the backlog status**

Modify `docs/plans/2026-03-18-project-improvement-backlog.md` so Task 7 references the new checklist/templates as the prerequisite path for actual hardware evidence collection.

- [ ] **Step 7: Verify link and markdown integrity**

Run:
```bash
rg -n "docs/hardware/" README.md docs/plans/2026-03-18-project-improvement-backlog.md docs/hardware
git diff --check
```

Expected: all new paths resolve in text and `git diff --check` is clean.

- [ ] **Step 8: Commit**

```bash
git add README.md docs/hardware docs/plans/2026-03-18-project-improvement-backlog.md
git commit -m "docs: add hardware validation templates"
```

---

## Recommended Execution Order

1. Task 1: layered CI gates
2. Task 2: README/eval sync automation
3. Task 3: hardware validation templates

## Success Criteria

- Docs, native, and Docker verification each have a clear CI home.
- README evaluation summaries are no longer maintained by manual copy-editing alone.
- Hardware evidence collection has a repeatable template instead of ad hoc notes.
- `rules/documentation-sync.md` and the automation agree on how README freshness is enforced.
