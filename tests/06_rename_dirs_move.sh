#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

SRC_DIR="$SANDBOX/docs"
DST_DIR="$SANDBOX/docs_renamed"
MOVE_TARGET="$SANDBOX/moved_subdir"

note "Checking preconditions"
[ -d "$SRC_DIR" ] || die "Missing $SRC_DIR"
[ -d "$SANDBOX/sub" ] || die "Missing $SANDBOX/sub"

note "Recording initial directory listing"
ORIG_LIST=$(ls -1 "$SRC_DIR" | tr '\n' ' ')

note "Renaming directory"
mv "$SRC_DIR" "$DST_DIR"
sync "$SANDBOX" || true
sleep 1

note "Verifying rename success"
[ -d "$DST_DIR" ] || die "Renamed directory not found"
[ ! -d "$SRC_DIR" ] || die "Old directory name still exists"

note "Verifying contents unchanged"
NEW_LIST=$(ls -1 "$DST_DIR" | tr '\n' ' ')
if [[ "$NEW_LIST" != "$ORIG_LIST" ]]; then
    die "Directory contents mismatch after rename"
fi

note "Moving directory to new location"
mv "$DST_DIR" "$MOVE_TARGET"
sync "$SANDBOX" || true
sleep 1

note "Verifying move success"
[ -d "$MOVE_TARGET" ] || die "Moved directory not found"
[ ! -d "$DST_DIR" ] || die "Old path still exists after move"

note "Verifying nested files are intact"
assert_file_exists "$MOVE_TARGET/a.txt"
assert_file_exists "$MOVE_TARGET/b.txt"

note "Cleaning up — moving directory back"
mv "$MOVE_TARGET" "$SRC_DIR" || die "Failed to restore directory position"

pass
