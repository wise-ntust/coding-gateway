#!/bin/sh
# common.sh — sourced by eval scripts; never executed directly

# csv_header FILE COL1,COL2,...
# Writes header row to FILE if it does not exist yet.
csv_header() {
    _file="$1"; _cols="$2"
    [ -f "$_file" ] || echo "$_cols" > "$_file"
}

# csv_row FILE VAL1 VAL2 ...
# Appends one data row; values joined with commas.
csv_row() {
    _file="$1"; shift
    _row=""
    for _v in "$@"; do
        [ -z "$_row" ] && _row="$_v" || _row="${_row},${_v}"
    done
    echo "$_row" >> "$_file"
}
