#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

note "Testing atomic swap with RENAME_EXCHANGE"

# Setup: Create two files with distinct content
# Use subshells to force file closure before swap
(echo "Content of file A" > "$SANDBOX/swap_a.txt")
(echo "Content of file B" > "$SANDBOX/swap_b.txt")

# Give backend time to process uploads from do_release()
sleep 2

# Save original content for verification
cp "$SANDBOX/swap_a.txt" /tmp/original_a.txt
cp "$SANDBOX/swap_b.txt" /tmp/original_b.txt

note "Performing atomic swap using renameat2 with RENAME_EXCHANGE"
python3 - <<'EOF'
import os
import ctypes
from ctypes.util import find_library

RENAME_EXCHANGE = 2
libc_path = find_library('c')
libc = ctypes.CDLL(libc_path, use_errno=True)

renameat2 = libc.renameat2
renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
renameat2.restype = ctypes.c_int

AT_FDCWD = -100
old_path = b"mnt/tests/swap_a.txt"
new_path = b"mnt/tests/swap_b.txt"

result = renameat2(AT_FDCWD, old_path, AT_FDCWD, new_path, RENAME_EXCHANGE)

if result != 0:
    errno = ctypes.get_errno()
    import errno as errno_module
    error_name = errno_module.errorcode.get(errno, f"UNKNOWN({errno})")
    print(f"renameat2 failed with errno={errno} ({error_name})")
    exit(1)
else:
    print("Swap successful")
EOF

note "Verifying swap occurred correctly"
diff -u /tmp/original_b.txt "$SANDBOX/swap_a.txt" || die "swap_a.txt does not contain original B content"
diff -u /tmp/original_a.txt "$SANDBOX/swap_b.txt" || die "swap_b.txt does not contain original A content"

note "Testing swap with non-existent file (should fail)"
(echo "Test file" > "$SANDBOX/real_file.txt")
sleep 2
python3 - <<'EOF' && die "Expected swap with non-existent file to fail" || note "Correctly rejected swap with non-existent file"
import ctypes
from ctypes.util import find_library

RENAME_EXCHANGE = 2
libc = ctypes.CDLL(find_library('c'), use_errno=True)
renameat2 = libc.renameat2
renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
renameat2.restype = ctypes.c_int
AT_FDCWD = -100

result = renameat2(AT_FDCWD, b"mnt/tests/real_file.txt", AT_FDCWD, b"mnt/tests/nonexistent.txt", RENAME_EXCHANGE)
exit(0 if result == 0 else 1)
EOF

# Cleanup
rm -f /tmp/original_a.txt /tmp/original_b.txt
rm -f "$SANDBOX/swap_a.txt" "$SANDBOX/swap_b.txt" "$SANDBOX/real_file.txt"

pass