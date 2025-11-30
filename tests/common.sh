#!/usr/bin/env bash
# common.sh — shared helpers for DISFS tests

set -euo pipefail

# Default values
USER="William"
MNT="${MNT:-mnt}"
SANDBOX="$MNT/tests"

# --- Utility functions ---
note() { echo "[NOTE] $*"; }
die() {
    local script
    script=$(basename "$0")
    echo -e "\033[1;31mFAIL:\033[0m $script $*\n" >&2
    exit 1
}



# --- Environment sanity checks ---
check_mount() {
    [ -d "$MNT/.command" ] || die "Mount not found. Run 'make mount' first."
}

check_login() {
    local out
    out=$(cat "$MNT/.command/ping/$USER" 2>/dev/null || true)
    echo "$out" | grep -q "Logged in as \"$USER\"" || die "Login failed (no matching login response)"
}

check_sandbox() {
    [ -d "$SANDBOX" ] || die "Run 01_setup.sh first."
}

# --- Common verification helpers ---
assert_file_exists() {
    [ -f "$1" ] || die "Missing file: $1"
}

assert_contains() {
    local file="$1"
    local pattern="$2"
    grep -q "$pattern" "$file" || die "Pattern '$pattern' not found in $file"
}

assert_mtime_newer() {
    local old="$1" new="$2" path="$3"
    if [ "$new" -le "$old" ]; then
        die "mtime not updated for $path (old=$old, new=$new)"
    fi
}

# --- Common start procedure ---
init_test() {
    check_mount
    check_login
    check_sandbox
}

# --- Common end marker ---
pass() {
    local script
    script=$(basename "$0")
    echo -e "\033[1;32mPASS:\033[0m $script\n"
}
