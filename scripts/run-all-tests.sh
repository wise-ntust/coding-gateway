#!/bin/sh
# Run all integration tests in sequence and report PASS/FAIL per test.
# Usage: ./scripts/run-all-tests.sh
# Exit code: 0 if all pass, 1 if any fail.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

TESTS="
test_01_basic.sh
test_02_loss_20pct.sh
test_03_loss_50pct.sh
test_04_singlepath_block.sh
test_05_multipath_failover.sh
"

passed=0
failed=0
total=0

for t in $TESTS; do
    script="$SCRIPT_DIR/$t"
    if [ ! -x "$script" ]; then
        echo "[SKIP] $t (not found or not executable)"
        continue
    fi

    total=$((total + 1))
    if sh "$script"; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
done

echo ""
echo "Results: $total tests, $passed passed, $failed failed"

[ "$failed" -eq 0 ]
