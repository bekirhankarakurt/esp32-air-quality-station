#!/usr/bin/env bash
# End-to-end check: simulator -> UDP -> receiver, both encodings, with injected I2C faults.
# Usage: scripts/smoke_test.sh [build-dir]
set -euo pipefail

build_dir="${1:-build}"
port=47999
samples=20
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

"$build_dir/aq_receiver" --port "$port" --count $((samples * 2)) --timeout-s 10 --quiet 2>"$log" &
receiver=$!
sleep 0.5

"$build_dir/aq_simulator" --target "127.0.0.1:$port" --format both --speed 1000 \
    --count "$samples" --fault-rate 0.05 --seed 7 --quiet

wait "$receiver"
cat "$log"
grep -q "$samples binary, $samples json valid; rejected: none; lost 0, reordered 0" "$log"
echo "smoke test passed"
