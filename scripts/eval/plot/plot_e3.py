#!/usr/bin/env python3
"""E3: Blockage recovery latency — bar chart."""
import csv, sys
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

def load(path):
    data = defaultdict(lambda: ([], []))
    with open(path) as f:
        for row in csv.DictReader(f):
            c = row['config']
            data[c][0].append(int(row['blockage_ms']))
            data[c][1].append(float(row['gap_ms']))
    return data

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e3_blockage_recovery.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e3_blockage_recovery.pdf'
    data = load(csv_path)
    configs = sorted(data.keys())
    all_durations = sorted(set(d for c in configs for d in data[c][0]))
    x = np.arange(len(all_durations))
    width = 0.8 / max(len(configs), 1)
    fig, ax = plt.subplots(figsize=(3.5, 2.8))
    for i, config in enumerate(configs):
        dur_map = dict(zip(data[config][0], data[config][1]))
        vals = [dur_map.get(d, 0) for d in all_durations]
        ax.bar(x + i * width, vals, width, label=config)
    ax.set_xlabel('Blockage duration (ms)')
    ax.set_ylabel('Recovery gap (ms)')
    ax.set_xticks(x + width * (len(configs) - 1) / 2)
    ax.set_xticklabels(all_durations)
    ax.legend(fontsize=7)
    ax.grid(True, axis='y', alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
