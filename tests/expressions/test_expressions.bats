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

# Common configuration and input files
CONFIG="expressions.fferc"
INPUT="expressions.input"

@test "equals operator" {
    run "$FFE_BIN" -c "$CONFIG" -e "FirstName=Scott" "$INPUT"
    assert_success
    assert_output_matches_file "expected_equals.expected"
}

@test "starts-with operator" {
    run "$FFE_BIN" -c "$CONFIG" -e "FirstName^S" "$INPUT"
    assert_success
    assert_output_matches_file "expected_starts_with.expected"
}

@test "contains operator" {
    run "$FFE_BIN" -c "$CONFIG" -e "LastName~er" "$INPUT"
    assert_success
    assert_output_matches_file "expected_contains.expected"
}

@test "does-not-contain operator" {
    run "$FFE_BIN" -c "$CONFIG" -e "LastName#er" "$INPUT"
    assert_success
    assert_output_matches_file "expected_not_contains.expected"
}

@test "not-equals operator" {
    run "$FFE_BIN" -c "$CONFIG" -e "FirstName!Scott" "$INPUT"
    assert_success
    assert_output_matches_file "expected_not_equals.expected"
}

@test "regex operator" {
    run "$FFE_BIN" -c "$CONFIG" -e "FirstName?^S.*" "$INPUT"
    assert_success
    assert_output_matches_file "expected_regex.expected"
}

@test "multiple expressions (OR logic, default)" {
    run "$FFE_BIN" -c "$CONFIG" -e "FirstName=Scott" -e "FirstName=John" "$INPUT"
    assert_success
    assert_output_matches_file "expected_multiple_or.expected"
}

@test "multiple expressions with AND logic (-a)" {
    run "$FFE_BIN" -c "$CONFIG" -a -e "FirstName^S" -e "Age=45" "$INPUT"
    assert_success
    assert_output_matches_file "expected_multiple_and.expected"
}

@test "invert match (-v)" {
    run "$FFE_BIN" -c "$CONFIG" -v -e "FirstName=Scott" "$INPUT"
    assert_success
    assert_output_matches_file "expected_invert.expected"
}

@test "case-insensitive matching (-X)" {
    run "$FFE_BIN" -c "$CONFIG" -X -e "FirstName=scott" "$INPUT"
    assert_success
    assert_output_matches_file "expected_case_insensitive.expected"
}

@test "file value syntax (file:)" {
    # Create temporary file with valid values
    value_file="$BATS_TEST_TMPDIR/values.txt"
    echo "Scott" > "$value_file"
    echo "John" >> "$value_file"
    run "$FFE_BIN" -c "$CONFIG" -e "FirstName=file:$value_file" "$INPUT"
    assert_success
    assert_output_matches_file "expected_file_value.expected"
    # Temporary file automatically cleaned up by BATS_TEST_TMPDIR
}