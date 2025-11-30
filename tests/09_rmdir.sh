#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

EMPTY_DIR="$SANDBOX/empty_dir"
NONEMPTY_DIR="$SANDBOX/rmdir_test"
CHILD_FILE="$NONEMPTY_DIR/inner.txt"

note "Preparing test directories"
mkdir -p "$EMPTY_DIR"
mkdir -p "$NONEMPTY_DIR"
echo "delete me" > "$CHILD_FILE"
sync "$SANDBOX" || true
sleep 1

note "Attempting to remove non-empty directory (should fail)"
if rmdir "$NONEMPTY_DIR" 2>rmdir_err.log; then
    die "Non-empty directory removed without unlinking children"
fi
grep -q "File exists" rmdir_err.log || note "Expected 'File exists' or similar error seen"

note "Removing child file then directory"
rm -f "$CHILD_FILE"
sleep 1
rmdir "$NONEMPTY_DIR" || die "Failed to remove now-empty directory"

note "Verifying non-empty directory removal"
if [ -d "$NONEMPTY_DIR" ]; then
    die "rmdir_test directory still exists after removal"
fi

note "Testing removal of empty directory"
rmdir "$EMPTY_DIR" || die "Failed to remove empty directory"
if [ -d "$EMPTY_DIR" ]; then
    die "Empty directory still exists after removal"
fi

note "Recreating for consistency"
mkdir -p "$SANDBOX/docs" "$SANDBOX/sub"
touch "$SANDBOX/docs/a.txt" "$SANDBOX/docs/b.txt" "$SANDBOX/sub/c.txt"

pass
