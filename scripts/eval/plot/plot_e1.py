#!/usr/bin/env python3
"""E1: Decode success rate vs loss rate — line chart per config."""
import csv, sys
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(path):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            config = row['config']
            data[config][0].append(float(row['loss_pct']))
            data[config][1].append(float(row['success_rate']))
    return data

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e1_decode_success.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e1_decode_success.pdf'
    data = load(csv_path)
    fig, ax = plt.subplots(figsize=(3.5, 2.8))
    for config, (xs, ys) in sorted(data.items()):
        pairs = sorted(zip(xs, ys))
        ax.plot([p[0] for p in pairs], [p[1] for p in pairs],
                marker='o', label=config, linewidth=1.2, markersize=4)
    ax.set_xlabel('Injected loss rate (%)')
    ax.set_ylabel('Decode success rate (%)')
    ax.set_ylim(0, 105)
    ax.set_xlim(-2, 72)
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
