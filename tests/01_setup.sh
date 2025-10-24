#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

note "Logging in as $USER"
LOGIN_OUTPUT=$(cat "$MNT/.command/ping/$USER" || true)
echo "$LOGIN_OUTPUT"
echo "$LOGIN_OUTPUT" | grep -q "Logged in as \"$USER\"" || die "Login failed (no matching login response)"

note "Preparing test sandbox in $MNT/tests/"
rm -rf "$MNT/tests"
mkdir -p "$MNT/tests/docs" "$MNT/tests/sub"

note "Creating test files"
touch "$MNT/tests/docs/a.txt" "$MNT/tests/docs/b.txt"
touch "$MNT/tests/sub/c.txt"

note "Verifying structure"
ls -R "$MNT/tests" || die "Listing failed"

pass