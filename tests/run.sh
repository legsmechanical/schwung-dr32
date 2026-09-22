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
# The synth engines are C++ (Faust output). Compiled ONCE — they do not depend
# on the test — and linked into every test through the C++ driver, because
# they need its runtime. -Wno-comment: the generated headers nest '/*'.
# ⓘ -Wno-missing-field-initializers: engine rows leave optional trailing
# fields (an enum's options, the Media stage flag) to zero, by design.
for src in dsp/engines/*.cpp; do
  c++ -std=c++14 -O2 -Wall -Wextra -Werror -Wno-comment -Wno-unused-parameter -Wno-missing-field-initializers \
      -fno-exceptions -fno-rtti -Idsp -Idsp/engines \
      -c "$src" -o "dist/tests/eng_$(basename "${src%.cpp}").o"
done
# The C engines (9W9) — C11, as the machine is.
for src in dsp/engines/*.c; do
  cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -Idsp/engines \
      -c "$src" -o "dist/tests/eng_$(basename "${src%.c}").o"
done
for src in tests/test_*.c; do
  name=$(basename "$src" .c)
  cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -c "$src" -o "dist/tests/$name.o"
  for c in dsp/*.c; do
    cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -c "$c" -o "dist/tests/$(basename "${c%.c}").o"
  done
  c++ -o "dist/tests/$name" "dist/tests/$name.o" dist/tests/dr32.o dist/tests/dr32_*.o dist/tests/wav.o \
      dist/tests/eng_*.o -lm
  "./dist/tests/$name" "$@" || fail=1
done
# module.json must satisfy the host's constraints (duplicate keys reject the
# whole hierarchy, so this is not cosmetic)
node tools/check_module_json.mjs src/module.json || fail=1

# The synth engines' pages and the picker's model list are GENERATED from the
# engines' own tables; a stale copy is a knob that addresses nothing.
node tools/gen_engine_ui.mjs --check || fail=1

# The engine picker / sample browser, driven over a fake tree. It was only ever
# run by hand; the picker's new top menu made it worth running every time.
node tools/check_browser_nav.mjs >/dev/null || { node tools/check_browser_nav.mjs | grep FAIL; fail=1; }

# The Resample page (src/resample.js), driven the way the host drives an
# entered canvas page: jog/click as CCs, Back, drawPage with its extra_keys.
node tools/check_resample_page.mjs || fail=1

# build.sh's two-pass shape: the outer pass must not fall through past its
# `docker run`, or every line below the guard silently runs twice on the same
# mounted volume. Greps a shell script; no Docker, no toolchain, milliseconds.
node tools/check_build_script.mjs scripts/build.sh || fail=1
node tools/check_build_script.mjs scripts/install.sh || fail=1

# Help lines are DRAWN, never wrapped and never truncated — anything past x=127
# is dropped silently. Measured against the host's own glyph table, not counted
# against 20 characters. Skips (loudly) without SCHWUNG_SRC.
node tools/check_help.mjs src/help.json || fail=1

# The PAD cell's big number (src/canvas.js), drawn for all 32 pads through the
# host's own framebuffer: none blank, none clipped, none alike. Skips (loudly)
# without a Schwung checkout.
node tools/check_pad_cell.mjs || fail=1

# The chain_params dsp.so serves must be the host's own fallback plus the PAD
# cell's viz and NOTHING else: the modulation refresh re-parses it and REPLACES
# the slot's metadata (per-pad send ranges included). Proven with the host's
# own parser, compiled from SCHWUNG_SRC; skips (loudly) without a checkout.
node tools/check_chain_params.mjs || fail=1

# JSON layer
node tests/roundtrip.mjs tests/fixtures || fail=1

# The offline null-test renderer must at least build (running it needs device
# fixtures + a sample mirror; see docs/NULL_TESTING.md). The reverb renderer
# that used to sit beside it went with the FX bus.
cc -std=c11 -O2 -Wall -Wextra -Werror -Idsp -o dist/tests/render_score.o -c tests/render_score.c
c++ -o dist/tests/render_score dist/tests/render_score.o \
   dist/tests/dr32_params.o dist/tests/dr32_kit.o dist/tests/dr32_voice.o \
   dist/tests/dr32_effects.o dist/tests/dr32_preset.o dist/tests/dr32_json.o dist/tests/wav.o \
   dist/tests/dr32_engine.o dist/tests/eng_*.o -lm || fail=1
exit $fail
