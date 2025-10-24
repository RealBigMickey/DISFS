#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

TARGET="$SANDBOX/largefile.bin"
TRUNC_SIZE=$((5 * 1024 * 1024))   # 5 MB
TMP_LOCAL="/tmp/disfs_large_local.bin"
TMP_READBACK="/tmp/disfs_large_readback.bin"

note "Generating >20 MB random file"
head -c 20971520 /dev/urandom > "$TMP_LOCAL"   # 20 MB
ORIG_HASH=$(sha256sum "$TMP_LOCAL" | awk '{print $1}')
note "Original SHA256: $ORIG_HASH"

note "Writing large file into DISFS"
cp "$TMP_LOCAL" "$TARGET"
sync "$TARGET" || true
sleep 2

note "Reading large file back from DISFS"
cp "$TARGET" "$TMP_READBACK"
READ_HASH=$(sha256sum "$TMP_READBACK" | awk '{print $1}')
note "Readback SHA256: $READ_HASH"

if [[ "$READ_HASH" != "$ORIG_HASH" ]]; then
    die "Checksum mismatch — file corrupted during upload/download"
fi

note "Truncating large file to $((TRUNC_SIZE / 1024 / 1024)) MB"
truncate -s "$TRUNC_SIZE" "$TARGET"
sync "$TARGET" || true
sleep 1

NEW_SIZE=$(stat -c %s "$TARGET")
if [ "$NEW_SIZE" -ne "$TRUNC_SIZE" ]; then
    die "Truncate size incorrect (expected $TRUNC_SIZE, got $NEW_SIZE)"
fi

note "Verifying read after truncate"
head -c 1024 "$TARGET" >/dev/null || die "Read failed after truncate"

note "Cleaning up temp files"
rm -f "$TMP_LOCAL" "$TMP_READBACK"

pass
