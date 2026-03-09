#!/usr/bin/env python3
"""E4: Adaptive strategy — loss injection step timeline."""
import csv, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else 'results/e4_adaptive_step.csv'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'results/e4_adaptive_step.pdf'
    times, losses = [], []
    with open(csv_path) as f:
        for row in csv.DictReader(f):
            times.append(float(row['elapsed_s']))
            losses.append(float(row['loss_pct']))
    fig, ax = plt.subplots(figsize=(3.5, 2.8))
    ax.step(times, losses, where='post', color='C3', linewidth=1.5, label='Injected loss %')
    ax.set_xlabel('Elapsed time (s)')
    ax.set_ylabel('Injected loss rate (%)')
    ax.set_ylim(-5, 85)
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    ax.set_title('E4: Loss injection step sequence', fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f'Saved: {out_path}')

if __name__ == '__main__':
    main()
