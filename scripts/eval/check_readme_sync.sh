#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
TMP_README="$(mktemp)"
trap 'rm -f "$TMP_README"' EXIT

cp "$REPO_ROOT/README.md" "$TMP_README"
python3 "$SCRIPT_DIR/render_readme_eval.py" "$TMP_README"

if ! diff -u "$REPO_ROOT/README.md" "$TMP_README"; then
    echo "README.md is out of sync with generated evaluation inventory." >&2
    echo "Run: python3 scripts/eval/render_readme_eval.py README.md" >&2
    exit 1
fi
