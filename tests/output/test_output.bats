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

# Helper function for output tests (similar to run_test_with_args)
run_output_test_with_args() {
    local expected_file="$1"
    shift 1  # Remove first argument, rest are ffe arguments

    run "$FFE_BIN" "$@"
    assert_success
    assert_output_matches_file "$expected_file"
}

@test "basic output with indent and field names" {
    run_output_test_with_args \
        "expected_basic.expected" \
        -c "output_basic.fferc" -s test_structure "test_input.input"
}

@test "file-related directives (file_header, record_header, etc.)" {
    run_output_test_with_args \
        "expected_file_directives.expected" \
        -c "output_file_directives.fferc" -s test_structure -p file_info "./test_input_28.input"
}

@test "various output directives (%s, %r, %o, %O, %i, %I, %n, %t, %d, %D, %C, %p)" {
    run_output_test_with_args \
        "expected_directives.expected" \
        -c "output_directives.fferc" -s test_structure -p directives "test_input.input"
}

@test "separator option" {
    run_output_test_with_args \
        "expected_separator.expected" \
        -c "output_separator.fferc" -s test_structure -p csv "test_input.input"
}

@test "field-list option" {
    run_output_test_with_args \
        "expected_fieldlist.expected" \
        -c "output_fieldlist.fferc" -s test_structure -p selected "test_input.input"
}

@test "hexadecimal output directives" {
    run_output_test_with_args \
        "expected_hex.expected" \
        -c "output_hex.fferc" -s hex_test -p hex_output "hex_test.input"
}

@test "lookup output directives" {
    run_output_test_with_args \
        "expected_lookup.expected" \
        -c "output_lookup.fferc" -s lookup_test -p lookup_output "output_lookup.input"
}

@test "empty field directive" {
    run_output_test_with_args \
        "expected_empty.expected" \
        -c "output_empty.fferc" -s empty_test -p empty_output "output_empty.input"
}

@test "percent sign literal" {
    run_output_test_with_args \
        "expected_percent.expected" \
        -c "output_percent.fferc" -s test -p percent_output "output_percent.input"
}

@test "hex-caps option" {
    run_output_test_with_args \
        "expected_hex_caps.expected" \
        -c "output_hex_caps.fferc" -s hex_test -p hex_output "hex_caps.input"
}