#!/bin/sh

# Regression test for output formatting features

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../../src/ffe}"
srcdir="${srcdir:-.}"

# Input files
basic_config="$srcdir/output_basic.fferc"
basic_input="$srcdir/test_input.input"
file_directives_config="$srcdir/output_file_directives.fferc"
file_directives_input="$srcdir/test_input_28.input"
directives_config="$srcdir/output_directives.fferc"
directives_input="$srcdir/test_input.input"
separator_config="$srcdir/output_separator.fferc"
fieldlist_config="$srcdir/output_fieldlist.fferc"
hex_config="$srcdir/output_hex.fferc"
hex_input="$srcdir/hex_test.input"
lookup_config="$srcdir/output_lookup.fferc"
lookup_input="$srcdir/output_lookup.input"
empty_config="$srcdir/output_empty.fferc"
empty_input="$srcdir/output_empty.input"
percent_config="$srcdir/output_percent.fferc"
percent_input="$srcdir/output_percent.input"
hex_caps_config="$srcdir/output_hex_caps.fferc"
hex_caps_input="$srcdir/hex_caps.input"

# Test counter
total_tests=0
passed_tests=0
failed_tests=0

echo "=== Running output tests ==="

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
    if ! "$FFE" "$@" > "$output" 2>&1; then
        echo "  ERROR: ffe command failed for test '$test_name'"
        echo "  Command: $FFE $@"
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
        echo "  Command: $FFE $@"
        echo "  Diff output:"
        diff -u "$expected_file" "$output" || true
        failed_tests=$((failed_tests + 1))
    fi

    rm -f "$output"
}

# Test 1: Basic output with indent and field names
run_test_with_args "basic_output" "$srcdir/expected_basic.expected" \
    -c "$basic_config" -s test_structure "$basic_input"

# Test 2: File-related directives (file_header, record_header, etc.)
run_test_with_args "file_directives" "$srcdir/expected_file_directives.expected" \
    -c "$file_directives_config" -s test_structure -p file_info "$file_directives_input"

# Test 3: Various output directives (%s, %r, %o, %O, %i, %I, %n, %t, %d, %D, %C, %p)
run_test_with_args "output_directives" "$srcdir/expected_directives.expected" \
    -c "$directives_config" -s test_structure -p directives "$directives_input"

# Test 4: Separator option
run_test_with_args "separator" "$srcdir/expected_separator.expected" \
    -c "$separator_config" -s test_structure -p csv "$basic_input"

# Test 5: Field-list option
run_test_with_args "fieldlist" "$srcdir/expected_fieldlist.expected" \
    -c "$fieldlist_config" -s test_structure -p selected "$basic_input"

# Test 6: Hexadecimal output directives
run_test_with_args "hex_output" "$srcdir/expected_hex.expected" \
    -c "$hex_config" -s hex_test -p hex_output "$hex_input"

# Test 7: Lookup output directives
run_test_with_args "lookup_output" "$srcdir/expected_lookup.expected" \
    -c "$lookup_config" -s lookup_test -p lookup_output "$lookup_input"

# Test 8: Empty field directive
run_test_with_args "empty_output" "$srcdir/expected_empty.expected" \
    -c "$empty_config" -s empty_test -p empty_output "$empty_input"

# Test 9: Percent sign literal
run_test_with_args "percent_output" "$srcdir/expected_percent.expected" \
    -c "$percent_config" -s test -p percent_output "$percent_input"

# Test 10: Hex-caps option
run_test_with_args "hex_caps_output" "$srcdir/expected_hex_caps.expected" \
    -c "$hex_caps_config" -s hex_test -p hex_output "$hex_caps_input"

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