#!/usr/bin/env bash
# Off-device DSP tests. No Docker, no Move — first-line check before any deploy.
# Usage: tests/run.sh [sample-dir]   (sample-dir sweeps real .wav files if given)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p dist/tests
fail=0
for src in tests/test_*.c; do
  name=$(basename "$src" .c)
  # dr32_fxbus.cpp is C++ (vendored reverbs); build it separately and link both.
  c++ -std=c++17 -O2 -Wall -Idsp -c dsp/dr32_fxbus.cpp -o dist/tests/dr32_fxbus.o
  cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -c "$src" -o "dist/tests/$name.o"
  for c in dsp/*.c; do
    cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -c "$c" -o "dist/tests/$(basename "${c%.c}").o"
  done
  c++ -o "dist/tests/$name" "dist/tests/$name.o" dist/tests/dr32_*.o dist/tests/wav.o -lm
  "./dist/tests/$name" "$@" || fail=1
done
# JSON layer
node tests/roundtrip.mjs tests/fixtures || fail=1

# The offline null-test renderer must at least build (running it needs device
# fixtures + a sample mirror; see docs/NULL_TESTING.md).
c++ -std=c++17 -O2 -Idsp -c dsp/dr32_fxbus.cpp -o dist/tests/fxbus_rs.o
cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -o dist/tests/render_score.o -c tests/render_score.c
c++ -o dist/tests/render_score dist/tests/render_score.o dist/tests/fxbus_rs.o \
   dist/tests/dr32_params.o dist/tests/dr32_kit.o dist/tests/dr32_voice.o \
   dist/tests/dr32_effects.o dist/tests/dr32_preset.o dist/tests/dr32_json.o dist/tests/wav.o -lm || fail=1
exit $fail
