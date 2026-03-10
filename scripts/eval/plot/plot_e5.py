#!/usr/bin/env python3
"""E5: Bandwidth overhead ratio — bar chart per config."""
import csv, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e5_overhead.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e5_overhead.pdf'

    configs, ratios = [], []
    with open(csv_path) as f:
        for row in csv.DictReader(f):
            try:
                ratios.append(float(row['overhead_ratio']))
                configs.append(row['config'])
            except (ValueError, KeyError):
                pass

    fig, ax = plt.subplots(figsize=(3.5, 2.8))
    ax.bar(configs, ratios, color='steelblue', width=0.4)
    ax.axhline(1.5, color='red', linestyle='--', linewidth=1,
               label='Theoretical (k=2, n=3)')
    ax.set_ylabel('Overhead ratio (wire / payload)')
    ax.set_ylim(0, 2.5)
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3, axis='y')
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
