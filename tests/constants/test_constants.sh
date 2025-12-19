#!/bin/sh

# Regression test for constants functionality

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../src/ffe}"
srcdir="${srcdir:-.}"

# Test counter
total_tests=0
passed_tests=0
failed_tests=0

echo "=== Running constants tests ==="

# Function to run a test case
run_test() {
    local test_name="$1"
    local config="$2"
    local input="$3"
    local expected="$4"
    local output_profile="$5"

    total_tests=$((total_tests + 1))

    echo "Running test: $test_name"

    # Create temporary output file
    output=$(mktemp)

    # Run ffe with the configuration
    if ! "$FFE" -c "$config" -p "$output_profile" "$input" > "$output" 2>&1; then
        echo "  ERROR: ffe command failed for test '$test_name'"
        echo "  Config: $config, Input: $input"
        failed_tests=$((failed_tests + 1))
        rm -f "$output"
        return 1
    fi

    # Compare with expected output
    if diff -u "$expected" "$output" > /dev/null 2>&1; then
        echo "  PASS: $test_name"
        passed_tests=$((passed_tests + 1))
    else
        echo "  FAIL: $test_name"
        echo "  Config: $config, Input: $input"
        echo "  Diff output:"
        diff -u "$expected" "$output" || true
        failed_tests=$((failed_tests + 1))
    fi

    rm -f "$output"
}

# Run all test cases

# Test 1: Basic constants in separated output
run_test "basic_constants" "$srcdir/constants_basic.fferc" "$srcdir/constants.input" "$srcdir/expected_basic.expected" "basic"

# Test 2: Multiple constants in separated output
run_test "all_constants" "$srcdir/constants_basic.fferc" "$srcdir/constants.input" "$srcdir/expected_all_constants.expected" "all_constants"

# Test 3: Constants overriding input fields
run_test "override_constants" "$srcdir/constants_override.fferc" "$srcdir/constants.input" "$srcdir/expected_override.expected" "test"

# Test 4: Fixed-length default output (baseline)
run_test "fixed_default" "$srcdir/constants_fixed_exact.fferc" "$srcdir/constants_fixed_exact.input" "$srcdir/expected_fixed_default.expected" "default"

# Test 5: Fixed-length with dot constants (%D trimmed)
run_test "fixed_with_dots" "$srcdir/constants_fixed_exact.fferc" "$srcdir/constants_fixed_exact.input" "$srcdir/expected_fixed_with_dots.expected" "with_dots"

# Test 6: Fixed-length with dot constants (%D trimmed) - raw output
run_test "fixed_raw_with_dots" "$srcdir/constants_fixed_exact.fferc" "$srcdir/constants_fixed_exact.input" "$srcdir/expected_fixed_raw_with_dots.expected" "raw_with_dots"

echo "=== Test summary ==="
echo "Total tests: $total_tests"
echo "Passed: $passed_tests"
echo "Failed: $failed_tests"

if [ $failed_tests -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "$failed_tests test(s) failed"
    exit 1
fi