#!/usr/bin/env bats

# Set srcdir to the test directory
srcdir="$BATS_TEST_DIRNAME"
# Source our test helper
source "$BATS_TEST_DIRNAME/../test_helper.bash"

setup() {
    # Setup temporary directory for tests
    setup_bats_tempdir
    # Change to test directory
    cd "$srcdir" || exit 1
}

teardown() {
    # Cleanup if needed
    :
}

# Helper function for lookup tests
run_lookup_test() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local output_profile="$4"

    run "$FFE_BIN" -c "$config" -p "$output_profile" "$input"
    assert_success
    assert_output_matches_file "$expected"
}

@test "inline pair lookup" {
    run_lookup_test "lookup_test.fferc" "lookup_simple.input" "expected_test.expected" "test"
}

@test "file lookup (semicolon separator default)" {
    run_lookup_test "lookup_file.fferc" "lookup_simple.input" "expected_file.expected" "test"
}

@test "file lookup with comma separator" {
    run_lookup_test "lookup_file_comma.fferc" "lookup_simple.input" "expected_comma.expected" "test"
}

@test "longest search (exact matches)" {
    run_lookup_test "lookup_longest_search.fferc" "lookup_longest.input" "expected_longest.expected" "test"
}

@test "exact search (exact matches)" {
    run_lookup_test "lookup_exact_search.fferc" "lookup_longest.input" "expected_exact.expected" "test"
}

@test "prefix matching with longest search" {
    run_lookup_test "lookup_prefix_longest.fferc" "lookup_prefix.input" "expected_prefix_longest.expected" "test"
}

@test "prefix matching with exact search" {
    run_lookup_test "lookup_prefix_exact.fferc" "lookup_prefix.input" "expected_prefix_exact.expected" "test"
}

@test "uppercase lookup directive %L" {
    run_lookup_test "lookup_uppercase.fferc" "lookup_simple.input" "expected_upper.expected" "test_upper"
}

@test "lowercase lookup directive %l" {
    run_lookup_test "lookup_uppercase.fferc" "lookup_simple.input" "expected_lower.expected" "test_lower"
}