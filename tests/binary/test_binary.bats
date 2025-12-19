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
    # Setup runs before each test
    # Use the environment variables set by test_helper.bash
    cd "$srcdir" || exit 1
}

teardown() {
    # Cleanup if needed
    :
}

@test "binary parsing test" {
    # Use helper function to run test
    run_ffe_test "binary.fferc" "binary.input" "binary.expected" -s bin_data
}