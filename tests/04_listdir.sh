#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

EXPECTED_TOP=("docs" "sub")
EXPECTED_DOCS=("a.txt" "b.txt")
EXPECTED_SUB=("c.txt")

note "Checking top-level order"
mapfile -t TOP_ENTRIES < <(ls -1 "$SANDBOX")
if [[ "${TOP_ENTRIES[*]}" != "${EXPECTED_TOP[*]}" ]]; then
    die "Top-level entries mismatch.
Expected: ${EXPECTED_TOP[*]}
Got:      ${TOP_ENTRIES[*]}"
fi

note "Checking docs/ order"
mapfile -t DOC_ENTRIES < <(ls -1 "$SANDBOX/docs")
if [[ "${DOC_ENTRIES[*]}" != "${EXPECTED_DOCS[*]}" ]]; then
    die "docs/ entries mismatch.
Expected: ${EXPECTED_DOCS[*]}
Got:      ${DOC_ENTRIES[*]}"
fi

note "Checking sub/ order"
mapfile -t SUB_ENTRIES < <(ls -1 "$SANDBOX/sub")
if [[ "${SUB_ENTRIES[*]}" != "${EXPECTED_SUB[*]}" ]]; then
    die "sub/ entries mismatch.
Expected: ${EXPECTED_SUB[*]}
Got:      ${SUB_ENTRIES[*]}"
fi

pass
