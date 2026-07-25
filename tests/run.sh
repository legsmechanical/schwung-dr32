#!/usr/bin/env bash
# Off-device DSP tests. No Docker, no Move — first-line check before any deploy.
# Usage: tests/run.sh [sample-dir]   (sample-dir sweeps real .wav files if given)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p dist/tests
fail=0
for src in tests/test_*.c; do
  name=$(basename "$src" .c)
  cc -std=c11 -O2 -Wall -Wextra -Werror -o "dist/tests/$name" "$src" dsp/*.c -lm
  "./dist/tests/$name" "$@" || fail=1
done
# JSON layer
node tests/roundtrip.mjs tests/fixtures || fail=1
exit $fail
