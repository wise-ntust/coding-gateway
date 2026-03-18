#!/usr/bin/env python3
"""Render the generated evaluation inventory block inside README.md."""

from __future__ import annotations

from pathlib import Path
import sys


BEGIN_MARKER = "<!-- BEGIN GENERATED: eval-inventory -->"
END_MARKER = "<!-- END GENERATED: eval-inventory -->"

EXPERIMENTS = [
    ("E2", "e2_throughput.sh", "TCP throughput vs loss: throughput collapses rapidly; ARQ does not recover high-loss performance"),
    ("E3", "e3_blockage_recovery.sh", "Blockage recovery latency (single-path)"),
    ("E3-MP", "e3_multipath_blockage.sh", "Multi-path blockage: 0 ms recovery gap for all tested blockage durations"),
    ("E4", "e4_adaptive_step.sh", "Loss step-injection trace and adaptive runtime behavior"),
    ("E5", "e5_overhead.sh", "Bandwidth overhead ratio: measured wire overhead vs configured coding ratio"),
    ("E6", "results/e6_arq_*", "FEC-only vs FEC+ARQ decode success: ARQ helps little at low loss and hurts at high loss"),
    ("E8-R", "e8_k_sweep_repeated.sh", "k-value sweep, 30 reps: latency vs decode success at 20% loss"),
    ("E10", "e10_tripath_degradation.sh", "Path degradation: 2->1->0 alive paths"),
    ("E12", "e12_mptcp_compare.sh", "MPTCP-equivalent (no coding) vs FEC-2x: success rate comparison"),
    ("E13", "e13_path_count_sweep.sh", "Path-count sweep: N=2,3,4 x mptcp_equiv/fec_2x x loss 0-40%"),
    ("E14", "e14_path_degradation.sh", "Path degradation: N=3,4 topologies; interpretation remains methodology-sensitive"),
    ("E15", "e15_blockage_recovery.sh", "Multi-path blockage recovery: N=3,4 paths, 0 ms gap for >=100 ms blockage"),
    ("E16", "e16_k_multipath_sweep.sh", "k-sweep (k=1,2,4) x N-path (2,3,4) x ratio=2.0 under symmetric/asymmetric loss"),
    ("E17", "e17_iperf_4node.sh", "iperf3 4-node end-to-end throughput: FEC vs no-FEC, 0-40% path loss"),
]


def build_block() -> str:
    lines = [
        BEGIN_MARKER,
        "### Additional Experiments",
        "",
        "> This index is generated from the tracked evaluation script inventory. Detailed result sections below remain manually curated.",
        "",
        "| Experiment | Script | Description |",
        "|-----------|--------|-------------|",
    ]
    for exp_id, script_name, description in EXPERIMENTS:
        lines.append(f"| {exp_id} | `{script_name}` | {description} |")
    lines.append(END_MARKER)
    return "\n".join(lines)


def main() -> int:
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("README.md")
    text = target.read_text(encoding="utf-8")

    if BEGIN_MARKER not in text or END_MARKER not in text:
        raise SystemExit(f"missing markers in {target}")

    start = text.index(BEGIN_MARKER)
    end = text.index(END_MARKER) + len(END_MARKER)
    rendered = build_block()
    updated = text[:start] + rendered + text[end:]
    target.write_text(updated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
