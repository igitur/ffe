#!/bin/sh

# Regression test for lookup table functionality

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../src/ffe}"
srcdir="${srcdir:-.}"

# Test counter
total_tests=0
passed_tests=0
failed_tests=0

echo "=== Running lookup tests ==="

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

# Test 1: Inline pair lookup
run_test "inline_pair_lookup" "$srcdir/lookup_test.fferc" "$srcdir/lookup_simple.input" "$srcdir/expected_test.expected" "test"

# Test 2: File lookup (semicolon separator default)
run_test "file_lookup_semicolon" "$srcdir/lookup_file.fferc" "$srcdir/lookup_simple.input" "$srcdir/expected_file.expected" "test"

# Test 3: File lookup with comma separator
run_test "file_lookup_comma" "$srcdir/lookup_file_comma.fferc" "$srcdir/lookup_simple.input" "$srcdir/expected_comma.expected" "test"

# Test 4: Longest search (exact matches)
run_test "longest_search_exact" "$srcdir/lookup_longest_search.fferc" "$srcdir/lookup_longest.input" "$srcdir/expected_longest.expected" "test"

# Test 5: Exact search (exact matches)
run_test "exact_search_exact" "$srcdir/lookup_exact_search.fferc" "$srcdir/lookup_longest.input" "$srcdir/expected_exact.expected" "test"

# Test 6: Prefix matching with longest search
run_test "prefix_longest_search" "$srcdir/lookup_prefix_longest.fferc" "$srcdir/lookup_prefix.input" "$srcdir/expected_prefix_longest.expected" "test"

# Test 7: Prefix matching with exact search
run_test "prefix_exact_search" "$srcdir/lookup_prefix_exact.fferc" "$srcdir/lookup_prefix.input" "$srcdir/expected_prefix_exact.expected" "test"

# Test 8: Uppercase lookup directive %L
run_test "uppercase_directive" "$srcdir/lookup_uppercase.fferc" "$srcdir/lookup_simple.input" "$srcdir/expected_upper.expected" "test_upper"

# Test 9: Lowercase lookup directive %l
run_test "lowercase_directive" "$srcdir/lookup_uppercase.fferc" "$srcdir/lookup_simple.input" "$srcdir/expected_lower.expected" "test_lower"

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