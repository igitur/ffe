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

# Helper function for replace tests
run_replace_test() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local output_format="${4:-fixed}"
    shift 4

    run "$FFE_BIN" -c "$config" "$input" -p"$output_format" "$@"
    assert_success
    assert_output_matches_file "$expected"
}

@test "basic field replacement in fixed format" {
    run_replace_test \
        "replace_basic.fferc" \
        "replace_basic.input" \
        "expected_basic_fixed.expected" \
        "fixed" \
        -r "EmpType=B"
}

@test "different replacement value" {
    run_replace_test \
        "replace_basic.fferc" \
        "replace_basic.input" \
        "expected_different_value.expected" \
        "fixed" \
        -r "EmpType=X"
}

@test "multiple replacements" {
    run_replace_test \
        "replace_basic.fferc" \
        "replace_basic.input" \
        "expected_multiple_fixed.expected" \
        "fixed" \
        -r "EmpType=B" -r "Age=99"
}

@test "replacement with expression filter" {
    run_replace_test \
        "replace_basic.fferc" \
        "replace_basic.input" \
        "expected_filtered_fixed.expected" \
        "fixed" \
        -e "FirstName^J" -r "EmpType=B"
}

@test "replacement in separated format (default output)" {
    run_replace_test \
        "replace_separated.fferc" \
        "replace_separated.input" \
        "expected_separated_default.expected" \
        "default" \
        -r "EmpType=B"
}