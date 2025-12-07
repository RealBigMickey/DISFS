#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

DEEP_DIR="$SANDBOX/level1/level2/level3"
DEEP_FILE="$DEEP_DIR/deep.txt"

note "Removing any existing directories"
rm -rf "$SANDBOX/level1"
sleep 1

note "Creating recursive directories (mkdir -p)"
mkdir -p "$DEEP_DIR"

note "Creating file in deep directory"
echo "Deep Content" > "$DEEP_FILE"
sleep 1

note "Verifying file existence"
assert_file_exists "$DEEP_FILE"

note "Verifying content"
assert_contains "$DEEP_FILE" "Deep Content"

note "Cleaning up"
rm -r "$SANDBOX/level1"

if [ -d "$SANDBOX/level1" ]; then
    die "Directory removal failed"
fi

pass