#!/bin/sh

# Master test runner for ffe tests
# Runs all tests in subdirectories

set -e

# Base directory of tests (where this script is located)
test_dir="$(cd "$(dirname "$0")" && pwd)"

# Use FFE_BIN from environment if set, otherwise default to ../src/ffe relative to test directory
if [ -z "$FFE_BIN" ]; then
    FFE_BIN="$test_dir/../src/ffe"
fi

# Make FFE_BIN absolute if it's relative
case "$FFE_BIN" in
    /*) ;;  # already absolute
    *) FFE_BIN="$test_dir/$FFE_BIN" ;;
esac

if [ ! -x "$FFE_BIN" ]; then
    echo "ERROR: ffe binary not found or not executable: $FFE_BIN"
    echo "Please build ffe first with 'make' in the src directory"
    exit 1
fi

# srcdir is used by automake, default to test directory
srcdir="${srcdir:-$test_dir}"

# Find all test directories (subdirectories containing .sh files)
test_dirs="fixed_length separated binary expressions"

total=0
passed=0
failed=0

echo "=== Running ffe test suite ==="
echo "Using ffe binary: $FFE_BIN"

for dir in $test_dirs; do
    if [ ! -d "$dir" ]; then
        echo "WARNING: Test directory '$dir' not found, skipping"
        continue
    fi

    # Find test script in directory
    test_script=$(find "$dir" -maxdepth 1 -name "*.sh" | head -1)
    if [ -z "$test_script" ]; then
        echo "WARNING: No test script found in '$dir', skipping"
        continue
    fi

    total=$((total + 1))
    echo "Running test: $dir"

    # Run test in its directory with proper environment
    (cd "$dir" && FFE_BIN="$FFE_BIN" srcdir="." sh "./$(basename "$test_script")")

    if [ $? -eq 0 ]; then
        echo "  PASS: $dir"
        passed=$((passed + 1))
    else
        echo "  FAIL: $dir"
        failed=$((failed + 1))
    fi
done

echo "=== Test summary ==="
echo "Total:  $total"
echo "Passed: $passed"
echo "Failed: $failed"

if [ $failed -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "$failed test(s) failed"
    exit 1
fi