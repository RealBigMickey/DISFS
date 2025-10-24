#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

TARGET="$SANDBOX/empty_check.txt"

note "Creating empty file"
touch "$TARGET"
sync "$TARGET" || true
sleep 1
assert_file_exists "$TARGET"

note "Verifying size is 0"
SIZE=$(stat -c %s "$TARGET")
if [ "$SIZE" -ne 0 ]; then
    die "Expected empty file size 0, got $SIZE"
fi

note "Reading from empty file (should return nothing)"
CONTENT=$(cat "$TARGET")
if [ -n "$CONTENT" ]; then
    die "Empty file read returned unexpected data"
fi

note "Overwriting empty file with new content"
echo "temporary data $(date +%s)" > "$TARGET"
sync "$TARGET" || true
sleep 1
NEW_SIZE=$(stat -c %s "$TARGET")
if [ "$NEW_SIZE" -eq 0 ]; then
    die "File size unchanged after writing new content"
fi

note "Truncating file back to 0"
: > "$TARGET"
sync "$TARGET" || true
sleep 1
FINAL_SIZE=$(stat -c %s "$TARGET")
if [ "$FINAL_SIZE" -ne 0 ]; then
    die "Truncate failed (expected 0 bytes, got $FINAL_SIZE)"
fi

note "Verifying truncated file reads empty again"
CONTENT=$(cat "$TARGET")
if [ -n "$CONTENT" ]; then
    die "Truncated file returned data when read"
fi

pass
