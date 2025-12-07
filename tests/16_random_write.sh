#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

TEST_FILE="$SANDBOX/patch_test.txt"

note "Creating base file (AAAAA)"
printf "AAAAA" > "$TEST_FILE"

note "Patching middle byte (3rd byte) with 'B'"
# conv=notrunc is crucial here, otherwise it might truncate the file to just "B"
printf "B" | dd of="$TEST_FILE" bs=1 seek=2 count=1 conv=notrunc 2>/dev/null

note "Verifying content (Expect AABAA)"
EXPECTED="AABAA"
ACTUAL=$(cat "$TEST_FILE")

if [ "$ACTUAL" != "$EXPECTED" ]; then
    die "Random write failed.
Expected: $EXPECTED
Got:      $ACTUAL"
fi

pass