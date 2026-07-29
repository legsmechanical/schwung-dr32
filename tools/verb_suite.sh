#!/usr/bin/env bash
# verb_suite.sh — null test for the Native (SuperEco) reverb.
#
#   tools/verb_suite.sh            # score + render + report against build/ir
#   tools/verb_suite.sh <a.abl>    # one preset, ad hoc
#
# This is the reverb's counterpart to fx_suite.sh, and it is deliberately much
# simpler. The pad-effect suite has to drive the whole kit engine through a
# song; a reverb reference is a single-sample impulse through a return chain, so
# the tail is already isolated and the only thing to reproduce is the reverb.
#
# The references in build/ir were captured earlier and are checked in — this
# script needs NO device time. Re-capture with tools/oracle.sh only if the
# fixtures change.
#
# ⚠ READ THIS BEFORE BELIEVING THE NULL COLUMN. Four parts of the device are
# still unmodelled (see docs/specs/2026-07-29-supereco-port.md), and one of them
# is the FINAL MIXER, which sets the entire output structure. Until that lands,
# the null number is not a measure of the model — the RT60 columns are, because
# they read the late loop's behaviour rather than the output mix.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=build/fx
FRAMES=1323008          # 30 s at 44.1k — the longest DecayTime needs it
mkdir -p "$OUT"

if [ ! -x dist/tests/render_verb ]; then
    echo "dist/tests/render_verb missing — run tests/run.sh first" >&2
    exit 1
fi

run_one() {
    local abl="$1" name="$2" ref="$3"
    node tools/verb_score.mjs "$abl" "$OUT/$name.verb" >/dev/null
    ./dist/tests/render_verb "$OUT/$name.verb" "$OUT/$name-dr32.wav" "$FRAMES" \
        2>"$OUT/$name.log" || { sed 's/^/    /' "$OUT/$name.log"; return 1; }
    printf '%-12s ' "$name"
    if [ -n "$ref" ] && [ -f "$ref" ]; then
        python3 - "$ref" "$OUT/$name-dr32.wav" <<'PY'
import sys; sys.path.insert(0, 'tools')
from irtools import mono, rt60
n, sr = mono(sys.argv[1]); o, _ = mono(sys.argv[2])
f = lambda v: f"{v:5.2f}" if v else "    -"
bands = ((20, 671), (671, 1470), (1470, 16000))
print(f"{f(rt60(n,sr))} -> {f(rt60(o,sr))} s   bands "
      + "  ".join(f"{f(rt60(n,sr,band=b))}->{f(rt60(o,sr,band=b))}" for b in bands))
PY
    else
        echo "(rendered, no reference)"
    fi
}

if [ $# -ge 1 ]; then
    run_one "$1" "$(basename "${1%.abl}")" "${2:-}"
    exit 0
fi

echo "Native reverb vs the device's own render. RT60 broadband, then per band."
echo "⚠ Not a null number — see the header. Unmodelled parameters are in $OUT/*.log."
printf '%-12s %s\n' CASE 'device -> ours'
for abl in build/ir/*.abl; do
    name=$(basename "${abl%.abl}")
    run_one "$abl" "$name" "build/ir/$name.wav" || true
done
