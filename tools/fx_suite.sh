#!/usr/bin/env bash
# fx_suite.sh — per-effect null test against the stock engine.
#
#   tools/fx_suite.sh capture   # render the native references on the device
#   tools/fx_suite.sh           # render DR32 + report null depth for each
#
# Capture talks to the Move once per effect, SEQUENTIALLY. Do not parallelise:
# hammering sshd with rapid connections has previously knocked the device off
# the network entirely.
set -euo pipefail
cd "$(dirname "$0")/.."

BENCH="../move original reconstruct/capture/opt-move/BenchmarkSongs/16drums.abl"
MIRROR="${DR32_SAMPLES:?set DR32_SAMPLES to the local sample mirror}"
OUT=build/fx
FRAMES=44100
mkdir -p "$OUT"

# name | Effect_Type | per-cell parameter overrides
CASES=(
  "standard|Standard|"
  "stretch|Stretch|Effect_StretchFactor=4,Effect_StretchGrainSize=0.05"
  "loop|Loop|Effect_LoopOffset=0.1,Effect_LoopLength=0.2"
  "pitchenv|Pitch Env|Effect_PitchEnvelopeAmount=0.75,Effect_PitchEnvelopeDecay=0.25"
  "punch|Punch|Effect_PunchAmount=0.8,Effect_PunchTime=0.3"
  "eightbit|8-bit|Effect_EightBitResamplingRate=6000,Effect_EightBitFilterDecay=1.0"
  "fm|FM|Effect_FmAmount=0.5,Effect_FmFrequency=220"
  "ringmod|Ring Mod|Effect_RingModAmount=0.7,Effect_RingModFrequency=300"
  "subosc|Sub Osc|Effect_SubOscAmount=0.8,Effect_SubOscFrequency=60"
  "noise|Noise|Effect_NoiseAmount=0.6,Effect_NoiseFrequency=2000"
)

build_fixture() {
    node tools/make_fixture.mjs "$BENCH" "$OUT/$1.abl" \
        --notes=36 --hold=0.001 --decay=2.0 --effect="$2" >/dev/null 2>&1
    if [ -n "$3" ]; then node tools/set_cell_params.mjs "$OUT/$1.abl" "$3" >/dev/null 2>&1; fi
}

if [ "${1:-run}" = "capture" ]; then
    for c in "${CASES[@]}"; do
        IFS='|' read -r name fx overrides <<< "$c"
        build_fixture "$name" "$fx" "$overrides"
        printf '%-10s ' "$name"
        tools/oracle.sh "$OUT/$name.abl" "$OUT/$name-native.wav" "$FRAMES" 2>/dev/null | tail -1
    done
    exit 0
fi

printf '%-10s %10s   %s\n' EFFECT NULL NOTE
for c in "${CASES[@]}"; do
    IFS='|' read -r name fx overrides <<< "$c"
    if [ ! -f "$OUT/$name-native.wav" ]; then
        printf '%-10s %10s\n' "$name" "(no ref)"
        continue
    fi
    build_fixture "$name" "$fx" "$overrides"
    node tools/score.mjs "$OUT/$name.abl" "$OUT/$name.score" --samples="$MIRROR" >/dev/null 2>&1
    ./build/render_score "$OUT/$name.score" "$OUT/$name-dr32.wav" "$FRAMES" 2>/dev/null
    printf '%-10s ' "$name"
    node tools/nulltest.mjs "$OUT/$name-native.wav" "$OUT/$name-dr32.wav" --json | node -e '
        let s=""; process.stdin.on("data",d=>s+=d).on("end",()=>{
            const r=JSON.parse(s);
            const g=r.best_fit_gain_db;
            console.log(r.null_depth_db.toFixed(1).padStart(8)+" dB" +
                (Math.abs(g)>0.5 ? "   (level off by "+g.toFixed(1)+" dB)" : ""));
        })'
done
