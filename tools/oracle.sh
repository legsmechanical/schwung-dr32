#!/usr/bin/env bash
# oracle.sh — render a .abl through the STOCK engine on the Move and fetch the
# float WAV. This is the ground truth the DR32 renderer is nulled against.
#
# EnginePerfTool drives the real engine graph through its offline/fake driver,
# so this is not an acoustic recording — it is the engine's own output, and
# repeat runs are byte-identical.
#
# Usage: tools/oracle.sh <song.abl> <out.wav> [frames]
set -euo pipefail

SONG="$1"
OUT="$2"
FRAMES="${3:-88200}"
HOST="${MOVE_HOST:-move.local}"
REMOTE=/tmp/dr32-oracle

ssh "ableton@${HOST}" "mkdir -p ${REMOTE}"
scp -q "$SONG" "ableton@${HOST}:${REMOTE}/song.abl"

ssh "ableton@${HOST}" "/opt/move/EnginePerfTool \
    --fake-driver --silent --render-audio \
    --duration=${FRAMES} --num-threads=1 \
    --audio-output=${REMOTE}/out.wav \
    --output=${REMOTE}/out.json \
    ${REMOTE}/song.abl" >/dev/null

scp -q "ableton@${HOST}:${REMOTE}/out.wav" "$OUT"
echo "$OUT: $(ls -l "$OUT" | awk '{print $5}') bytes"
