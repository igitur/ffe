#!/bin/sh

# Master test runner for ffe tests
# Runs all tests in subdirectories
# This script can be called from any directory

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

# Find bats executable
find_bats() {
    if [ -x "$test_dir/bats-core/bin/bats" ]; then
        echo "$test_dir/bats-core/bin/bats"
    elif command -v bats >/dev/null 2>&1; then
        echo "bats"
    else
        echo "ERROR: bats not found" >&2
        exit 1
    fi
}
BATS="$(find_bats)"

# Find all test directories (subdirectories containing .sh or .bats files)
test_dirs="fixed_length separated binary expressions lookup constants anonymize replace output"

total=0
passed=0
failed=0

echo "=== Running ffe test suite ==="
echo "Using ffe binary: $FFE_BIN"
echo "Using bats: $BATS"

for dir in $test_dirs; do
    dir_path="$test_dir/$dir"
    if [ ! -d "$dir_path" ]; then
        echo "WARNING: Test directory '$dir_path' not found, skipping"
        continue
    fi

    # Find test script in directory - prefer .bats files
    test_script=""
    bats_script=$(find "$dir_path" -maxdepth 1 -name "*.bats" | head -1)
    sh_script=$(find "$dir_path" -maxdepth 1 -name "*.sh" | head -1)
    if [ -n "$bats_script" ]; then
        test_script="$bats_script"
        use_bats=true
    elif [ -n "$sh_script" ]; then
        test_script="$sh_script"
        use_bats=false
    else
        echo "WARNING: No test script found in '$dir_path', skipping"
        continue
    fi

    total=$((total + 1))
    echo "Running test: $dir ($(basename "$test_script"))"

    # Run test in its directory with proper environment
    if [ "$use_bats" = true ]; then
        (cd "$dir_path" && FFE_BIN="$FFE_BIN" srcdir="." "$BATS" "./$(basename "$test_script")")
    else
        (cd "$dir_path" && FFE_BIN="$FFE_BIN" srcdir="." sh "./$(basename "$test_script")")
    fi

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