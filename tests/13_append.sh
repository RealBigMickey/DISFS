#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

TEST_FILE="$SANDBOX/append_test.txt"

note "Removing any existing test file"
rm -f "$TEST_FILE"
sleep 1

note "Creating initial file"
echo "Part 1" > "$TEST_FILE"
assert_file_exists "$TEST_FILE"

note "Appending data"
echo "Part 2" >> "$TEST_FILE"

note "Verifying content integrity"

EXPECTED=$(printf "Part 1\nPart 2")
ACTUAL=$(cat "$TEST_FILE")

if [ "$ACTUAL" != "$EXPECTED" ]; then
    die "Append failed.
Expected:
$EXPECTED
Got:
$ACTUAL"
fi

pass