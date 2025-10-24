#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

TARGET="$SANDBOX/overwrite_test.txt"

note "Creating file with initial content"
echo "AAAA" > "$TARGET"
sync "$TARGET" || true
sleep 1
assert_file_exists "$TARGET"
ORIG_SIZE=$(stat -c %s "$TARGET")

note "Overwriting with different content (same size)"
echo "BBBB" > "$TARGET"
sync "$TARGET" || true
sleep 1
CONTENT=$(cat "$TARGET")
if [[ "$CONTENT" != "BBBB" ]]; then
    die "Overwrite (same size) failed: got '$CONTENT'"
fi

note "Overwriting with larger content"
echo "CCCCCCCC" > "$TARGET"
sync "$TARGET" || true
sleep 1
NEW_SIZE=$(stat -c %s "$TARGET")
if [ "$NEW_SIZE" -le "$ORIG_SIZE" ]; then
    die "Expected larger size after larger overwrite (old=$ORIG_SIZE new=$NEW_SIZE)"
fi
CONTENT=$(cat "$TARGET")
if [[ "$CONTENT" != "CCCCCCCC" ]]; then
    die "Overwrite (larger) content mismatch: got '$CONTENT'"
fi

note "Overwriting with smaller content"
echo "DD" > "$TARGET"
sync "$TARGET" || true
sleep 1
FINAL_SIZE=$(stat -c %s "$TARGET")
if [ "$FINAL_SIZE" -ge "$NEW_SIZE" ]; then
    die "Expected smaller size after smaller overwrite (old=$NEW_SIZE new=$FINAL_SIZE)"
fi
CONTENT=$(cat "$TARGET")
if [[ "$CONTENT" != "DD" ]]; then
    die "Overwrite (smaller) content mismatch: got '$CONTENT'"
fi

pass
