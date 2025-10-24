#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

TARGET_FILE="$SANDBOX/docs/a.txt"
TMP_CONTENT="Temporary content written at $(date)"
note "Checking preconditions"
assert_file_exists "$TARGET_FILE"

note "Recording original file size"
ORIG_SIZE=$(stat -c %s "$TARGET_FILE")

note "Writing new content"
echo "$TMP_CONTENT" > "$TARGET_FILE"
sync "$TARGET_FILE" || true
sleep 1
NEW_SIZE=$(stat -c %s "$TARGET_FILE")

if [ "$NEW_SIZE" -le "$ORIG_SIZE" ]; then
    die "File size did not increase after write (old=$ORIG_SIZE new=$NEW_SIZE)"
fi

note "Truncating file to zero bytes"
: > "$TARGET_FILE"
sync "$TARGET_FILE" || true
sleep 1
TRUNC_SIZE=$(stat -c %s "$TARGET_FILE")

if [ "$TRUNC_SIZE" -ne 0 ]; then
    die "Truncate failed (expected 0 bytes, got $TRUNC_SIZE)"
fi

note "Verifying content is cleared"
if [ -s "$TARGET_FILE" ]; then
    die "File still has content after truncate"
fi

note "Unlinking file"
rm -f "$TARGET_FILE"
sleep 1

if [ -f "$TARGET_FILE" ]; then
    die "Unlink failed — file still exists"
fi

note "Recreating file for next tests"
echo "recreated after unlink $(date)" > "$TARGET_FILE"
sync "$TARGET_FILE" || true
assert_file_exists "$TARGET_FILE"

pass
