#!/bin/sh

# Regression test for binary parsing

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../src/ffe}"
srcdir="${srcdir:-.}"

# Input files
config="$srcdir/binary.fferc"
input="$srcdir/binary.input"
expected="$srcdir/binary.expected"

# Run ffe
output=$(mktemp)
"$FFE" -c "$config" -s bin_data "$input" > "$output"

# Compare with expected
if diff -u "$expected" "$output"; then
    echo "PASS: binary parsing test"
    rm -f "$output"
    exit 0
else
    echo "FAIL: output does not match expected"
    rm -f "$output"
    exit 1
fi