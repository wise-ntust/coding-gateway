#!/usr/bin/env python3
"""E2 ARQ comparison: overlay FEC-only vs FEC+ARQ throughput on one chart."""
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
            data[c][1].append(float(row['throughput_mbps']))
    return data

def main():
    results_dir = sys.argv[1] if len(sys.argv) > 1 else 'results'
    out_path = os.path.join(results_dir, 'e2_arq_compare.pdf')

    fec_only_path = os.path.join(results_dir, 'e2_throughput_fec_only.csv')
    arq_path      = os.path.join(results_dir, 'e2_throughput_arq.csv')

    if not os.path.exists(fec_only_path) or not os.path.exists(arq_path):
        print(f'Missing CSV files in {results_dir}. Run E2 experiments first.')
        sys.exit(1)

    data = {}
    data.update(load(fec_only_path, 'FEC only'))
    data.update(load(arq_path, 'FEC + ARQ'))

    styles = {
        'FEC only':  {'color': '#1f77b4', 'marker': 'o', 'linestyle': '-'},
        'FEC + ARQ': {'color': '#ff7f0e', 'marker': 's', 'linestyle': '--'},
    }

    fig, ax = plt.subplots(figsize=(4.5, 3.2))
    for config, (xs, ys) in sorted(data.items()):
        pairs = sorted(zip(xs, ys))
        st = styles.get(config, {})
        ax.plot([p[0] for p in pairs], [p[1] for p in pairs],
                label=config, linewidth=1.5, markersize=5, **st)

    ax.set_xlabel('Injected loss rate (%)')
    ax.set_ylabel('Effective throughput (Mbps)')
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    ax.set_title('FEC only vs FEC + ARQ', fontsize=10)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
