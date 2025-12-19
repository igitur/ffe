#!/usr/bin/env bats

# Load bats libraries relative to test file directory
load "$BATS_TEST_DIRNAME/../bats-support/load"
load "$BATS_TEST_DIRNAME/../bats-assert/load"
load "$BATS_TEST_DIRNAME/../bats-file/load"

# Set srcdir to the test directory before sourcing helper
srcdir="$BATS_TEST_DIRNAME"
# Source our test helper
source "$BATS_TEST_DIRNAME/../test_helper.bash"

setup() {
    # Change to test directory
    cd "$srcdir" || exit 1
}

teardown() {
    # Cleanup if needed
    :
}

# Helper function for constants tests
run_constants_test() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local output_profile="$4"

    run "$FFE_BIN" -c "$config" -p "$output_profile" "$input"
    assert_success
    assert_output_matches_file "$expected"
}

@test "basic constants in separated output" {
    run_constants_test "constants_basic.fferc" "constants.input" "expected_basic.expected" "basic"
}

@test "multiple constants in separated output" {
    run_constants_test "constants_basic.fferc" "constants.input" "expected_all_constants.expected" "all_constants"
}

@test "constants overriding input fields" {
    run_constants_test "constants_override.fferc" "constants.input" "expected_override.expected" "test"
}

@test "fixed-length default output (baseline)" {
    run_constants_test "constants_fixed_exact.fferc" "constants_fixed_exact.input" "expected_fixed_default.expected" "default"
}

@test "fixed-length with dot constants (%D trimmed)" {
    run_constants_test "constants_fixed_exact.fferc" "constants_fixed_exact.input" "expected_fixed_with_dots.expected" "with_dots"
}

@test "fixed-length with dot constants (%D trimmed) - raw output" {
    run_constants_test "constants_fixed_exact.fferc" "constants_fixed_exact.input" "expected_fixed_raw_with_dots.expected" "raw_with_dots"
}