#!/bin/sh

# Regression test for expression filtering with all operators

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../src/ffe}"
srcdir="${srcdir:-.}"

# Input files
config="$srcdir/expressions.fferc"
input="$srcdir/expressions.input"

# Test counter
total_tests=0
passed_tests=0
failed_tests=0

echo "=== Running expression tests ==="

# Function to run a test case with arbitrary ffe arguments
run_test_with_args() {
    local test_name="$1"
    local expected_file="$2"
    shift 2  # Remove first two arguments, rest are ffe arguments

    total_tests=$((total_tests + 1))

    echo "Running test: $test_name"

    # Create temporary output file
    output=$(mktemp)

    # Run ffe with all remaining arguments
    if ! "$FFE" -c "$config" "$@" "$input" > "$output" 2>&1; then
        echo "  ERROR: ffe command failed for test '$test_name'"
        echo "  Command: $FFE -c $config $@ $input"
        failed_tests=$((failed_tests + 1))
        rm -f "$output"
        return 1
    fi

    # Compare with expected output
    if diff -u "$expected_file" "$output" > /dev/null 2>&1; then
        echo "  PASS: $test_name"
        passed_tests=$((passed_tests + 1))
    else
        echo "  FAIL: $test_name"
        echo "  Command: $FFE -c $config $@ $input"
        echo "  Diff output:"
        diff -u "$expected_file" "$output" || true
        failed_tests=$((failed_tests + 1))
    fi

    rm -f "$output"
}

# Run all test cases

# Test 1: Equality operator (=)
run_test_with_args "equals_operator" "$srcdir/expected_equals.expected" -e "FirstName=Scott"

# Test 2: Starts-with operator (^)
run_test_with_args "starts_with_operator" "$srcdir/expected_starts_with.expected" -e "FirstName^S"

# Test 3: Contains operator (~)
run_test_with_args "contains_operator" "$srcdir/expected_contains.expected" -e "LastName~er"

# Test 4: Does-not-contain operator (#)
run_test_with_args "not_contains_operator" "$srcdir/expected_not_contains.expected" -e "LastName#er"

# Test 5: Not-equals operator (!)
run_test_with_args "not_equals_operator" "$srcdir/expected_not_equals.expected" -e "FirstName!Scott"

# Test 6: Regular expression operator (?)
run_test_with_args "regex_operator" "$srcdir/expected_regex.expected" -e "FirstName?^S.*"

# Test 7: Multiple expressions (OR logic, default)
run_test_with_args "multiple_or" "$srcdir/expected_multiple_or.expected" -e "FirstName=Scott" -e "FirstName=John"

# Test 8: Multiple expressions with AND logic (-a)
run_test_with_args "multiple_and" "$srcdir/expected_multiple_and.expected" -a -e "FirstName^S" -e "Age=45"
# Matches records where FirstName starts with S AND Age equals 45 (only Scott)

# Test 9: Invert match (-v)
run_test_with_args "invert_match" "$srcdir/expected_invert.expected" -v -e "FirstName=Scott"

# Test 10: Case-insensitive matching (-X)
run_test_with_args "case_insensitive" "$srcdir/expected_case_insensitive.expected" -X -e "FirstName=scott"

# Test 11: File value syntax (file:)
# Create a file with valid values
value_file=$(mktemp)
echo "Scott" > "$value_file"
echo "John" >> "$value_file"
run_test_with_args "file_value" "$srcdir/expected_file_value.expected" -e "FirstName=file:$value_file"
rm -f "$value_file"

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