#!/bin/sh

# Regression test for expression filtering

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../src/ffe}"
srcdir="${srcdir:-.}"

# Input files
config="$srcdir/fixed_length.fferc"
input="$srcdir/fixed_length.input"
expected="$srcdir/expression.expected"

# Run ffe with expression -e FirstName^Scott
output=$(mktemp)
"$FFE" -c "$config" -e FirstName^Scott "$input" > "$output"

# Compare with expected
if diff -u "$expected" "$output"; then
    echo "PASS: expression filtering test"
    rm -f "$output"
    exit 0
else
    echo "FAIL: output does not match expected"
    rm -f "$output"
    exit 1
fi