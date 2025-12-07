#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

TEST_FILE="$SANDBOX/seek_read.dat"
CONTENT="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

note "Removing any existing test file"
rm -f "$TEST_FILE"
sleep 1

note "Writing test data"
echo -n "$CONTENT" > "$TEST_FILE"

note "Reading bytes 10-15 (Expect 'ABCDE')"
READ_CHUNK=$(dd if="$TEST_FILE" bs=1 skip=10 count=5 2>/dev/null)

if [ "$READ_CHUNK" != "ABCDE" ]; then
    die "Random read failed. Expected 'ABCDE', got '$READ_CHUNK'"
fi

note "Reading last 3 bytes (Expect 'XYZ')"
FILE_SIZE=${#CONTENT}
SKIP=$((FILE_SIZE - 3))
READ_END=$(dd if="$TEST_FILE" bs=1 skip=$SKIP count=3 2>/dev/null)

if [ "$READ_END" != "XYZ" ]; then
    die "End seek read failed. Expected 'XYZ', got '$READ_END'"
fi

pass