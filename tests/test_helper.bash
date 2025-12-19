#!/usr/bin/env bash

# Test helper for ffe bats tests
# Sets up environment variables and provides helper functions

# Set up FFE binary path
# Use FFE_BIN from environment if set, otherwise default to ../src/ffe relative to tests directory
if [ -z "$FFE_BIN" ]; then
    FFE_BIN="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../src/ffe"
fi

# Make FFE_BIN absolute if it's relative
case "$FFE_BIN" in
    /*) ;;  # already absolute
    *) FFE_BIN="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$FFE_BIN" ;;
esac

# Verify ffe binary exists and is executable
if [ ! -x "$FFE_BIN" ]; then
    echo "ERROR: ffe binary not found or not executable: $FFE_BIN" >&2
    exit 1
fi

# srcdir is used by automake, default to test directory
srcdir="${srcdir:-.}"

# Export variables
export FFE_BIN
export srcdir

# Helper to assert command output matches expected file content
# Usage: assert_output_matches_file expected_file
# Requires: run command must have been used previously
assert_output_matches_file() {
    local expected_file="$1"
    local expected_output
    expected_output=$(cat "$expected_file")
    assert_output "$expected_output"
}

# Helper to run a ffe test case and compare with expected output
# Usage: run_ffe_test config input expected [additional ffe args]
run_ffe_test() {
    local config="$1"
    local input="$2"
    local expected="$3"
    shift 3
    # Remaining arguments are passed to ffe

    run "$FFE_BIN" -c "$config" "$@" "$input"
    assert_success
    assert_output_matches_file "$expected"
}