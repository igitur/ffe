#!/bin/sh

# Regression test for separated (CSV) parsing

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../src/ffe}"
srcdir="${srcdir:-.}"

# Input files
config="$srcdir/separated.fferc"
input="$srcdir/separated.input"
expected="$srcdir/separated.expected"

# Run ffe
output=$(mktemp)
"$FFE" -c "$config" "$input" > "$output"

# Compare with expected
if diff -u "$expected" "$output"; then
    echo "PASS: separated parsing test"
    rm -f "$output"
    exit 0
else
    echo "FAIL: output does not match expected"
    rm -f "$output"
    exit 1
fi