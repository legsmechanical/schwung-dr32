#!/usr/bin/env bash
# Off-device DSP tests. No Docker, no Move — first-line check before any deploy.
# Usage: tests/run.sh [sample-dir]   (sample-dir sweeps real .wav files if given)
set -euo pipefail
cd "$(dirname "$0")/.."
# ⚠ WIPE FIRST. The link lines glob `dist/tests/dr32_*.o`, so an object left
# behind by an earlier run — from a source that has since been DELETED — is
# still picked up and still linked. That is exactly what happened when the FX
# bus was removed: a stale dr32_fxbus.o dragged the C++ runtime into a build
# that no longer had a single C++ file in it. A stale artifact is a decoy.
rm -rf dist/tests
mkdir -p dist/tests
fail=0
for src in tests/test_*.c; do
  name=$(basename "$src" .c)
  cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -c "$src" -o "dist/tests/$name.o"
  for c in dsp/*.c; do
    cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -c "$c" -o "dist/tests/$(basename "${c%.c}").o"
  done
  cc -o "dist/tests/$name" "dist/tests/$name.o" dist/tests/dr32.o dist/tests/dr32_*.o dist/tests/wav.o -lm
  "./dist/tests/$name" "$@" || fail=1
done
# module.json must satisfy the host's constraints (duplicate keys reject the
# whole hierarchy, so this is not cosmetic)
node tools/check_module_json.mjs src/module.json || fail=1

# JSON layer
node tests/roundtrip.mjs tests/fixtures || fail=1

# The offline null-test renderer must at least build (running it needs device
# fixtures + a sample mirror; see docs/NULL_TESTING.md). The reverb renderer
# that used to sit beside it went with the FX bus.
cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -o dist/tests/render_score.o -c tests/render_score.c
cc -o dist/tests/render_score dist/tests/render_score.o \
   dist/tests/dr32_params.o dist/tests/dr32_kit.o dist/tests/dr32_voice.o \
   dist/tests/dr32_effects.o dist/tests/dr32_preset.o dist/tests/dr32_json.o dist/tests/wav.o -lm || fail=1
exit $fail
