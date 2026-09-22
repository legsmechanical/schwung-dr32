#!/bin/bash
# Render ChowKick's five factory presets through the REFERENCE (ckref, built
# from ChowKick's own sources — see CMakeLists.txt here) and write the golden
# files tests/test_chowkick.c holds DR32's port to:
#
#   tests/fixtures/chowkick/<slug>.golden  float32 little-endian:
#       [0..4409]   the first 100 ms, exact
#       [4410..4429] the peak of each 100 ms window over 2 s (the envelope)
#       [4430]      zero crossings 100..600 ms at 2%-of-peak hysteresis (pitch)
#
# Velocity 100, 44.1 kHz, 128-frame blocks, the note at frame 0.
# ⚠ Wonky Synth is rendered with Link OFF at its saved 80 Hz: in the plugin its
# filter follows the played note, and a DR32 pad has no played note — the
# model is the unlinked instrument, so that is what it is compared with.
#
#   tools/chowkick_ref/make_golden.sh <ckref binary> <ChowKick clone>
set -euo pipefail
REF="$1"; CK="$2"; OUT="$(dirname "$0")/../../tests/fixtures/chowkick"
mkdir -p "$OUT"
for pr in Default:default Tight:tight Tonal:tonal Bouncy:bouncy Wonky_Synth:wonky; do
  f=${pr%%:*}; slug=${pr##*:}
  args=$(python3 -c 'import sys,re; s=open(sys.argv[1]).read(); print(" ".join(f"{k}={v}" for k,v in re.findall(r"id=\"(\w+)\" value=\"([^\"]+)\"",s) if k!="preset"))' "$CK/res/presets/$f.chowpreset")
  [ "$slug" = wonky ] && args="${args/res_link=1.0/res_link=0.0}"
  # shellcheck disable=SC2086 — one argument per parameter
  "$REF" 2 100 $args | python3 -c '
import sys, array
a = array.array("f", sys.stdin.buffer.read())
env = [max(abs(v) for v in a[i:i + 4410]) for i in range(0, 88200, 4410)]
seg = a[4410:26460]; th = max(abs(v) for v in seg) * 0.02; c = 0; s = 0
for v in seg:
    t = 1 if v > th else (-1 if v < -th else 0)
    if t and s and t != s: c += 1
    if t: s = t
out = array.array("f", list(a[:4410]) + env + [float(c)])
sys.stdout.buffer.write(out.tobytes())' > "$OUT/$slug.golden"
  echo "$slug: $(wc -c < "$OUT/$slug.golden") bytes"
done
