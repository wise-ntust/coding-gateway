#!/usr/bin/env python3
"""E1 comparison: single-path vs multi-path decode success rate."""
import csv, sys, os
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(path, label_override=None):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        for row in csv.DictReader(f):
            c = label_override if label_override else row['config']
            data[c][0].append(float(row['loss_pct']))
            data[c][1].append(float(row['success_rate']))
    return data

def main():
    results_dir = sys.argv[1] if len(sys.argv) > 1 else 'results'
    out_path = os.path.join(results_dir, 'e1_compare.pdf')

    single_path = os.path.join(results_dir, 'e1_decode_success.csv')
    multi_path  = os.path.join(results_dir, 'e1_multipath_decode.csv')

    data = {}
    if os.path.exists(single_path):
        data.update(load(single_path, 'Single-path (k=2, n=3)'))
    if os.path.exists(multi_path):
        data.update(load(multi_path, 'Multi-path (k=1, n=2, 2 paths)'))

    if not data:
        print(f'No CSV files found in {results_dir}.')
        sys.exit(1)

    styles = {
        'Single-path (k=2, n=3)':         {'color': '#1f77b4', 'marker': 'o', 'linestyle': '-'},
        'Multi-path (k=1, n=2, 2 paths)': {'color': '#2ca02c', 'marker': 's', 'linestyle': '--'},
    }

    fig, ax = plt.subplots(figsize=(4.5, 3.2))
    for config, (xs, ys) in sorted(data.items()):
        pairs = sorted(zip(xs, ys))
        st = styles.get(config, {})
        ax.plot([p[0] for p in pairs], [p[1] for p in pairs],
                label=config, linewidth=1.5, markersize=5, **st)

    ax.set_xlabel('Injected loss rate (%)')
    ax.set_ylabel('Decode success rate (%)')
    ax.set_ylim(0, 105)
    ax.set_xlim(-2, 72)
    ax.legend(fontsize=7, loc='lower left')
    ax.grid(True, alpha=0.3)
    ax.set_title('Single-path vs Multi-path', fontsize=10)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
