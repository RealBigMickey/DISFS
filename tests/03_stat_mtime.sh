#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

init_test

TARGET="$SANDBOX/docs/a.txt"

note "Recording old timestamps"
OLD_MTIME=$(stat -c %Y "$TARGET")
sleep 1

note "Touching file to update mtime"
touch "$TARGET"
sync "$TARGET" || true
sleep 1

note "Checking updated timestamps"
NEW_MTIME=$(stat -c %Y "$TARGET")

if [ "$NEW_MTIME" -le "$OLD_MTIME" ]; then
    die "mtime not updated correctly (old=$OLD_MTIME, new=$NEW_MTIME)"
fi

note "Verifying stat output"
stat "$TARGET" | grep -q "Modify" || die "stat output missing Modify field"

note "Touching new file"
NEWFILE="$SANDBOX/docs/new_touch.txt"
touch "$NEWFILE"
[ -f "$NEWFILE" ] || die "Newly touched file not created"

note "Verifying mtime and size of new file"
stat "$NEWFILE" | grep -q "Size: 0" || die "Touched file not empty"
stat "$NEWFILE" | grep -q "Modify" || die "stat failed on new file"
rm "$NEWFILE"

pass
