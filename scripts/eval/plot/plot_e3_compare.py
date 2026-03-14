#!/usr/bin/env python3
"""E3 comparison: single-path vs multi-path blockage recovery."""
import csv, sys, os
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

def load(path, label_override=None):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        for row in csv.DictReader(f):
            c = label_override if label_override else row['config']
            data[c][0].append(int(row['blockage_ms']))
            data[c][1].append(int(row['gap_ms']))
    return data

def main():
    results_dir = sys.argv[1] if len(sys.argv) > 1 else 'results'
    out_path = os.path.join(results_dir, 'e3_compare.pdf')

    single_path = os.path.join(results_dir, 'e3_blockage_recovery.csv')
    multi_path  = os.path.join(results_dir, 'e3_multipath_blockage.csv')

    data = {}
    if os.path.exists(single_path):
        data.update(load(single_path, 'Single-path'))
    if os.path.exists(multi_path):
        data.update(load(multi_path, 'Multi-path'))

    if not data:
        print(f'No CSV files found in {results_dir}.')
        sys.exit(1)

    fig, ax = plt.subplots(figsize=(4.5, 3.2))

    configs = sorted(data.keys())
    n_configs = len(configs)
    bar_width = 0.35
    colors = {'Single-path': '#1f77b4', 'Multi-path': '#2ca02c'}

    # Collect all blockage durations
    all_durations = sorted(set(d for c in configs for d in data[c][0]))
    x = np.arange(len(all_durations))

    for i, config in enumerate(configs):
        xs, ys = data[config]
        dur_to_gap = dict(zip(xs, ys))
        gaps = [dur_to_gap.get(d, 0) for d in all_durations]
        offset = (i - (n_configs - 1) / 2) * bar_width
        ax.bar(x + offset, gaps, bar_width, label=config,
               color=colors.get(config, None), alpha=0.85)

    ax.set_xlabel('Blockage duration (ms)')
    ax.set_ylabel('Recovery gap (ms)')
    ax.set_xticks(x)
    ax.set_xticklabels([str(d) for d in all_durations])
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3, axis='y')
    ax.set_title('Blockage Recovery: Single vs Multi-path', fontsize=10)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
