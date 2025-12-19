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

@test "fixed-length parsing test" {
    run_ffe_test "fixed_length.fferc" "fixed_length.input" "fixed_length.expected"
}