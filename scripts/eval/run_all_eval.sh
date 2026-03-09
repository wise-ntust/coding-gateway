#!/bin/sh
# Run all E1-E5 evaluation scripts and report completion.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "$RESULTS_DIR"

for script in e1_decode_success.sh e2_throughput.sh e3_blockage_recovery.sh \
              e4_adaptive_step.sh e5_overhead.sh; do
    path="$SCRIPT_DIR/$script"
    if [ -x "$path" ]; then
        sh "$path" "$RESULTS_DIR"
    else
        echo "[SKIP] $script (not found or not executable)"
    fi
done

echo ""
echo "Results written to $RESULTS_DIR/"
if [ "$(ls -A "$RESULTS_DIR" 2>/dev/null)" ]; then
    ls "$RESULTS_DIR/"
else
    echo "  (no results produced)"
fi
