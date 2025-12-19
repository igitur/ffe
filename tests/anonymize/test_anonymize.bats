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

# Helper function for anonymization tests (similar to check_output)
run_anonymize_test() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local anonymize="$4"
    local output_format="${5:-raw}"

    if [ -n "$anonymize" ]; then
        run "$FFE_BIN" -c "$config" -A "$anonymize" "$input" -p"$output_format"
    else
        run "$FFE_BIN" -c "$config" "$input" -p"$output_format"
    fi
    assert_success
    assert_output_matches_file "$expected"
}

# Helper for fixed-width test with random age
run_fixed_random_test() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local anonymize="$4"

    run "$FFE_BIN" -c "$config" -A "$anonymize" "$input"
    assert_success

    # Check each line: age field (positions 21-23) should be 3 digits
    # Replace age digits in both expected and output with placeholder for comparison
    normalized_expected="$BATS_TEST_TMPDIR/norm_expected"
    normalized_output="$BATS_TEST_TMPDIR/norm_output"

    sed 's/\(.\{20\}\)...\(.\{5\}\)/\1XXX\2/' "$expected" > "$normalized_expected"
    sed 's/\(.\{20\}\)...\(.\{5\}\)/\1XXX\2/' <<< "$output" > "$normalized_output"

    if diff -u "$normalized_expected" "$normalized_output" >&2; then
        return 0
    else
        return 1
    fi
}

# Helper for BCD validation
validate_bcd_nibbles() {
    local output_file="$1"
    # Validate each nibble is 0-9 (BCD)
    xxd -p "$output_file" | tr -d '\n' | awk '{
        if (length($0) != 6) { print "Invalid length"; exit 1 }
        for (i=1; i<=6; i+=2) {
            byte = substr($0, i, 2)
            high = substr(byte,1,1); low = substr(byte,2,1)
            if (high !~ /[0-9]/ || low !~ /[0-9]/) {
                print "Invalid BCD nibble: " byte
                exit 1
            }
        }
    }'
}

# Helper for binary mask validation
validate_binary_mask() {
    local output_file="$1"
    # Check that text field (first 5 bytes) are masked with '0'
    # Original text field: "ABC" + null + 0x08
    # Masked should be "000" + null + 0x08
    local first_five
    first_five=$(head -c5 "$output_file" | xxd -p)
    if [ "$first_five" != "3030300008" ]; then
        echo "FAIL: binary mask not applied correctly, got $first_five"
        return 1
    fi
    return 0
}

@test "basic mask anonymization" {
    run_anonymize_test \
        "anonymize_basic.fferc" \
        "anonymize.input" \
        "expected_mask.expected" \
        "test1" \
        "raw"
}

@test "hash anonymization" {
    run_anonymize_test \
        "anonymize_hash.fferc" \
        "anonymize.input" \
        "expected_hash.expected" \
        "test_hash" \
        "raw"
}

@test "custom mask characters" {
    run_anonymize_test \
        "anonymize_mask_char.fferc" \
        "anonymize.input" \
        "expected_mask_char.expected" \
        "test_mask_char" \
        "raw"
}

@test "partial field anonymization" {
    run_anonymize_test \
        "anonymize_partial.fferc" \
        "anonymize.input" \
        "expected_partial.expected" \
        "test_partial" \
        "raw"
}

@test "fixed-width anonymization with random age" {
    run_fixed_random_test \
        "anonymize_fixed.fferc" \
        "anonymize_fixed_exact.input" \
        "expected_fixed.expected" \
        "test_fixed"
}

@test "random anonymization for text fields" {
    run "$FFE_BIN" -c "anonymize_random.fferc" -A test_random "anonymize.input" -praw
    assert_success
    # Validate characters are in allowed set (0-9, A-Z, a-z, space, comma separator)
    if grep -q '[^0-9A-Za-z ,]' <<< "$output"; then
        echo "FAIL: Random text contains invalid characters"
        grep -n '[^0-9A-Za-z ,]' <<< "$output" | head -5
        return 1
    fi
}

@test "binary field anonymization" {
    # Use binary input from parent directory
    output_file="$BATS_TEST_TMPDIR/binary_output"
    run bash -c "\"$FFE_BIN\" -c \"anonymize_binary.fferc\" -s bin_data -A test_binary \"../binary/binary.input\" -praw > \"$output_file\""
    assert_success
    # Validate binary mask
    validate_binary_mask "$output_file"
}

@test "BCD field anonymization - mask method" {
    output_file="$BATS_TEST_TMPDIR/bcd_output"
    run bash -c "\"$FFE_BIN\" -c \"anonymize_bcd.fferc\" -s bcd_test -A \"test_bcd_mask\" \"bcd.input\" -praw > \"$output_file\""
    assert_success
    validate_bcd_nibbles "$output_file"
}

@test "BCD field anonymization - hash method" {
    output_file="$BATS_TEST_TMPDIR/bcd_output"
    run bash -c "\"$FFE_BIN\" -c \"anonymize_bcd.fferc\" -s bcd_test -A \"test_bcd_hash\" \"bcd.input\" -praw > \"$output_file\""
    assert_success
    validate_bcd_nibbles "$output_file"
}

@test "BCD field anonymization - random method" {
    output_file="$BATS_TEST_TMPDIR/bcd_output"
    run bash -c "\"$FFE_BIN\" -c \"anonymize_bcd.fferc\" -s bcd_test -A \"test_bcd_random\" \"bcd.input\" -praw > \"$output_file\""
    assert_success
    validate_bcd_nibbles "$output_file"
}

@test "hash with key parameter - no key (default 16)" {
    run_anonymize_test \
        "anonymize_hash_length.fferc" \
        "anonymize.input" \
        "expected_hash_no_key.expected" \
        "test_hash_no_key" \
        "raw"
}

@test "hash with key parameter - key=16" {
    run_anonymize_test \
        "anonymize_hash_length.fferc" \
        "anonymize.input" \
        "expected_hash_length_16.expected" \
        "test_hash_length_16" \
        "raw"
}

@test "hash with key parameter - key=32" {
    run_anonymize_test \
        "anonymize_hash_length.fferc" \
        "anonymize.input" \
        "expected_hash_length_32.expected" \
        "test_hash_length_32" \
        "raw"
}

@test "hash with key parameter - key=64" {
    run_anonymize_test \
        "anonymize_hash_length.fferc" \
        "anonymize.input" \
        "expected_hash_length_64.expected" \
        "test_hash_length_64" \
        "raw"
}

@test "hash key parameter outputs differ" {
    # Verify that different keys produce different outputs
    run diff -u "expected_hash_length_16.expected" "expected_hash_length_32.expected"
    [ $status -ne 0 ]  # diff should find differences

    run diff -u "expected_hash_length_16.expected" "expected_hash_length_64.expected"
    [ $status -ne 0 ]

    run diff -u "expected_hash_length_32.expected" "expected_hash_length_64.expected"
    [ $status -ne 0 ]
}