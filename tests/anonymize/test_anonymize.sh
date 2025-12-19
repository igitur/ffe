#!/bin/sh

# Regression test for anonymization functionality

set -e

# Use environment variables if set, otherwise default
FFE="${FFE_BIN:-../src/ffe}"
srcdir="${srcdir:-.}"

# Helper function to compare with expected output
check_output() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local anonymize="$4"
    local output_format="${5:-raw}"

    # Run ffe
    output=$(mktemp)
    if [ -n "$anonymize" ]; then
        "$FFE" -c "$config" -A "$anonymize" "$input" -p"$output_format" > "$output"
    else
        "$FFE" -c "$config" "$input" -p"$output_format" > "$output"
    fi

    # Compare with expected
    if diff -u "$expected" "$output"; then
        echo "PASS: anonymization test $config"
        rm -f "$output"
        return 0
    else
        echo "FAIL: output does not match expected for $config"
        rm -f "$output"
        return 1
    fi
}

# Helper for fixed-width test with random age
check_fixed_random() {
    local config="$1"
    local input="$2"
    local expected="$3"
    local anonymize="$4"

    # Run ffe
    output=$(mktemp)
    "$FFE" -c "$config" -A "$anonymize" "$input" > "$output"

    # Check each line: age field (positions 21-23) should be 3 digits
    # Replace age digits in both expected and output with placeholder for comparison
    normalized_expected=$(mktemp)
    normalized_output=$(mktemp)

    sed 's/\(.\{20\}\)...\(.\{5\}\)/\1XXX\2/' "$expected" > "$normalized_expected"
    sed 's/\(.\{20\}\)...\(.\{5\}\)/\1XXX\2/' "$output" > "$normalized_output"

    if diff -u "$normalized_expected" "$normalized_output"; then
        echo "PASS: fixed-width anonymization test $config (with random age)"
        rm -f "$output" "$normalized_expected" "$normalized_output"
        return 0
    else
        echo "FAIL: output does not match expected for $config"
        rm -f "$output" "$normalized_expected" "$normalized_output"
        return 1
    fi
}

# Test 1: Basic mask anonymization
check_output \
    "$srcdir/anonymize_basic.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_mask.expected" \
    "test1" \
    "raw"

# Test 2: Hash anonymization
check_output \
    "$srcdir/anonymize_hash.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_hash.expected" \
    "test_hash" \
    "raw"

# Test 3: Custom mask characters
check_output \
    "$srcdir/anonymize_mask_char.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_mask_char.expected" \
    "test_mask_char" \
    "raw"

# Test 4: Partial field anonymization
check_output \
    "$srcdir/anonymize_partial.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_partial.expected" \
    "test_partial" \
    "raw"

# Test 5: Fixed-width anonymization with random age
check_fixed_random \
    "$srcdir/anonymize_fixed.fferc" \
    "$srcdir/anonymize_fixed_exact.input" \
    "$srcdir/expected_fixed.expected" \
    "test_fixed"

# Test 6: Random anonymization for text fields
output=$(mktemp)
"$FFE" -c "$srcdir/anonymize_random.fferc" -A test_random "$srcdir/anonymize.input" -praw > "$output"
# Validate characters are in allowed set (0-9, A-Z, a-z, space, comma separator)
if grep -q '[^0-9A-Za-z ,]' "$output"; then
    echo "FAIL: Random text contains invalid characters"
    grep -n '[^0-9A-Za-z ,]' "$output" | head -5
    rm -f "$output"
    exit 1
fi
echo "PASS: random anonymization test"
rm -f "$output"

# Test 7: Binary field anonymization
# Just ensure it runs without error and masked fields are changed
output=$(mktemp)
"$FFE" -c "$srcdir/anonymize_binary.fferc" -s bin_data -A test_binary "$srcdir/../binary/binary.input" -praw > "$output" 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: binary anonymization test failed"
    rm -f "$output"
    exit 1
fi
# Check that text field (first 5 bytes) are masked with '0'
# Original text field: "ABC" + null + 0x08
# Masked should be "000" + null + 0x08
first_five=$(head -c5 "$output" | xxd -p)
if [ "$first_five" != "3030300008" ]; then
    echo "FAIL: binary mask not applied correctly, got $first_five"
    rm -f "$output"
    exit 1
fi
echo "PASS: binary anonymization test"
rm -f "$output"

# Test 8: BCD field anonymization
# Test mask, hash, random - ensure they produce valid BCD values (nibbles 0-9)
for method in mask hash random; do
    output=$(mktemp)
    "$FFE" -c "$srcdir/anonymize_bcd.fferc" -s bcd_test -A "test_bcd_$method" "$srcdir/bcd.input" -praw > "$output" 2>&1
    if [ $? -ne 0 ]; then
        echo "FAIL: BCD $method anonymization test failed"
        rm -f "$output"
        exit 1
    fi
    # Validate each nibble is 0-9 (BCD)
    xxd -p "$output" | tr -d '\n' | awk '{
        if (length($0) != 6) { print "Invalid length"; exit 1 }
        for (i=1; i<=6; i+=2) {
            byte = substr($0, i, 2)
            high = substr(byte,1,1); low = substr(byte,2,1)
            if (high !~ /[0-9]/ || low !~ /[0-9]/) {
                print "Invalid BCD nibble: " byte
                exit 1
            }
        }
    }' > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "FAIL: BCD $method produced invalid BCD values"
        xxd "$output"
        rm -f "$output"
        exit 1
    fi
    echo "PASS: BCD $method anonymization test"
    rm -f "$output"
done

# Test 9: Hash with key (length) parameter
echo "=== Testing hash with key parameter ==="

# Test hash with no key (default 16)
check_output \
    "$srcdir/anonymize_hash_length.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_hash_no_key.expected" \
    "test_hash_no_key" \
    "raw"

# Test hash with key=16 (should match default)
check_output \
    "$srcdir/anonymize_hash_length.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_hash_length_16.expected" \
    "test_hash_length_16" \
    "raw"

# Test hash with key=32
check_output \
    "$srcdir/anonymize_hash_length.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_hash_length_32.expected" \
    "test_hash_length_32" \
    "raw"

# Test hash with key=64
check_output \
    "$srcdir/anonymize_hash_length.fferc" \
    "$srcdir/anonymize.input" \
    "$srcdir/expected_hash_length_64.expected" \
    "test_hash_length_64" \
    "raw"

# Verify that different keys produce different outputs
if cmp -s "$srcdir/expected_hash_length_16.expected" "$srcdir/expected_hash_length_32.expected"; then
    echo "FAIL: hash length 16 and 32 produce same output"
    exit 1
fi
if cmp -s "$srcdir/expected_hash_length_16.expected" "$srcdir/expected_hash_length_64.expected"; then
    echo "FAIL: hash length 16 and 64 produce same output"
    exit 1
fi
if cmp -s "$srcdir/expected_hash_length_32.expected" "$srcdir/expected_hash_length_64.expected"; then
    echo "FAIL: hash length 32 and 64 produce same output"
    exit 1
fi
echo "PASS: hash key parameter tests"

echo "All anonymization tests passed"