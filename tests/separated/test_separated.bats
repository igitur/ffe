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

@test "separated parsing test" {
    run_ffe_test "separated.fferc" "separated.input" "separated.expected"
}