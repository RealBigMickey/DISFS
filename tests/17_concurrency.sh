#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

init_test

DIR_A="$SANDBOX/conc_a"
DIR_B="$SANDBOX/conc_b"
mkdir -p "$DIR_A" "$DIR_B"

note "Starting concurrent writes"

# Function to write 5 files
write_batch() {
    local dir=$1
    local name=$2
    for i in {1..5}; do
        echo "data $i" > "$dir/file_$i.txt"
    done
}

# Run two batches in background simultaneously
write_batch "$DIR_A" "A" &
PID_A=$!

write_batch "$DIR_B" "B" &
PID_B=$!

note "Waiting for background processes"
wait $PID_A
wait $PID_B

note "Verifying file counts"
COUNT_A=$(ls -1 "$DIR_A" | wc -l)
COUNT_B=$(ls -1 "$DIR_B" | wc -l)

if [ "$COUNT_A" -ne 5 ]; then
    die "Concurrency A failed. Expected 5 files, found $COUNT_A"
fi

if [ "$COUNT_B" -ne 5 ]; then
    die "Concurrency B failed. Expected 5 files, found $COUNT_B"
fi

pass