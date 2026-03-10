#!/bin/sh
# Generate all evaluation plots from CSV results.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="${1:-$SCRIPT_DIR/../results}"

for i in 1 2 3 4 5; do
    script="$SCRIPT_DIR/plot_e${i}.py"
    [ -f "$script" ] || continue
    # Derive CSV and output paths based on convention
    case "$i" in
        1) csv="$RESULTS_DIR/e1_decode_success.csv"    pdf="$RESULTS_DIR/e1_decode_success.pdf" ;;
        2) csv="$RESULTS_DIR/e2_throughput.csv"         pdf="$RESULTS_DIR/e2_throughput.pdf" ;;
        3) csv="$RESULTS_DIR/e3_blockage_recovery.csv"  pdf="$RESULTS_DIR/e3_blockage_recovery.pdf" ;;
        4) csv="$RESULTS_DIR/e4_adaptive_step.csv"      pdf="$RESULTS_DIR/e4_adaptive_step.pdf" ;;
        5) csv="$RESULTS_DIR/e5_overhead.csv"           pdf="$RESULTS_DIR/e5_overhead.pdf" ;;
    esac
    [ -f "$csv" ] || { echo "[SKIP] E${i}: $csv not found"; continue; }
    python3 "$script" "$csv" "$pdf" && echo "[OK] E${i}: $pdf"
done
