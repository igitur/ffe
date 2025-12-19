#!/bin/sh

# Regression test for anonymization functionality

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../src/ffe}"
srcdir="${srcdir:-.}"

# Helper function to compare with expected output
check_output() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local anonymize="$4"
    local output_format="${5:-raw}"

    # Run ffe
    output=$(mktemp)
    if [ -n "$anonymize" ]; then
        "$FFE" -c "$config" -A "$anonymize" "$input" -p"$output_format" > "$output"
    else
        "$FFE" -c "$config" "$input" -p"$output_format" > "$output"
    fi

    # Compare with expected
    if diff -u "$expected" "$output"; then
        echo "PASS: anonymization test $config"
        rm -f "$output"
        return 0
    else
        echo "FAIL: output does not match expected for $config"
        rm -f "$output"
        return 1
    fi
}

# Helper for fixed-width test with random age
check_fixed_random() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local anonymize="$4"

    # Run ffe
    output=$(mktemp)
    "$FFE" -c "$config" -A "$anonymize" "$input" > "$output"

    # Check each line: age field (positions 21-23) should be 3 digits
    # Replace age digits in both expected and output with placeholder for comparison
    normalized_expected=$(mktemp)
    normalized_output=$(mktemp)

    sed 's/\(.\{20\}\)...\(.\{5\}\)/\1XXX\2/' "$expected" > "$normalized_expected"
    sed 's/\(.\{20\}\)...\(.\{5\}\)/\1XXX\2/' "$output" > "$normalized_output"

    if diff -u "$normalized_expected" "$normalized_output"; then
        echo "PASS: fixed-width anonymization test $config (with random age)"
        rm -f "$output" "$normalized_expected" "$normalized_output"
        return 0
    else
        echo "FAIL: output does not match expected for $config"
        rm -f "$output" "$normalized_expected" "$normalized_output"
        return 1
    fi
}

# Test 1: Basic mask anonymization
check_output \
    "$srcdir/anonymize_basic.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_mask.expected" \
    "test1" \
    "raw"

# Test 2: Hash anonymization
check_output \
    "$srcdir/anonymize_hash.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_hash.expected" \
    "test_hash" \
    "raw"

# Test 3: Custom mask characters
check_output \
    "$srcdir/anonymize_mask_char.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_mask_char.expected" \
    "test_mask_char" \
    "raw"

# Test 4: Partial field anonymization
check_output \
    "$srcdir/anonymize_partial.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_partial.expected" \
    "test_partial" \
    "raw"

# Test 5: Fixed-width anonymization with random age
check_fixed_random \
    "$srcdir/anonymize_fixed.fferc" \
    "$srcdir/anonymize_fixed_exact.input" \
    "$srcdir/expected_fixed.expected" \
    "test_fixed"

echo "All anonymization tests passed"