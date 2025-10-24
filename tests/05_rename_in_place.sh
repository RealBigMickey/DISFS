#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

SRC_FILE="$SANDBOX/docs/a.txt"
DST_FILE="$SANDBOX/docs/a_renamed.txt"

note "Checking preconditions"
assert_file_exists "$SRC_FILE"

note "Recording original content and metadata"
ORIG_CONTENT=$(cat "$SRC_FILE")
ORIG_SIZE=$(stat -c %s "$SRC_FILE")

note "Renaming file in same directory"
mv "$SRC_FILE" "$DST_FILE"
sync "$DST_FILE" || true
sleep 1

note "Verifying rename success"
assert_file_exists "$DST_FILE"
[ ! -f "$SRC_FILE" ] || die "Old name still exists after rename"

note "Verifying content integrity"
NEW_CONTENT=$(cat "$DST_FILE")
if [[ "$NEW_CONTENT" != "$ORIG_CONTENT" ]]; then
    die "Content changed after rename"
fi

note "Verifying file size unchanged"
NEW_SIZE=$(stat -c %s "$DST_FILE")
if [[ "$NEW_SIZE" -ne "$ORIG_SIZE" ]]; then
    die "File size mismatch after rename (old=$ORIG_SIZE new=$NEW_SIZE)"
fi

note "Cleaning up test rename"
mv "$DST_FILE" "$SRC_FILE" || die "Failed to revert rename"

pass
