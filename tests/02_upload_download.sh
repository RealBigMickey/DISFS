#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

init_test

note "Writing to test files"
echo "alpha $(date +%s)" > "$SANDBOX/docs/a.txt"
echo "bravo $(date +%s)" > "$SANDBOX/docs/b.txt"
echo "charlie $(date +%s)" > "$SANDBOX/sub/c.txt"
sleep 1

note "Checking file stats"
[ -f "$SANDBOX/docs/a.txt" ] || die "a.txt missing or empty"
[ -f "$SANDBOX/docs/b.txt" ] || die "b.txt missing or empty"
[ -f "$SANDBOX/sub/c.txt" ] || die "c.txt missing or empty"

note "Verifying contents"
grep -q "alpha" "$SANDBOX/docs/a.txt" || die "a.txt content mismatch"
grep -q "bravo" "$SANDBOX/docs/b.txt" || die "b.txt content mismatch"
grep -q "charlie" "$SANDBOX/sub/c.txt" || die "c.txt content mismatch"

pass