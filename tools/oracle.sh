#!/usr/bin/env bash
# oracle.sh — render a .abl through the STOCK engine on the Move and fetch the
# float WAV. This is the ground truth the DR32 renderer is nulled against.
#
# EnginePerfTool drives the real engine graph through its offline/fake driver,
# so this is not an acoustic recording — it is the engine's own output, and
# repeat runs are byte-identical.
#
# ⚠ SUSPECTED DEVICE DESTABILISATION (2026-07-25). The Move has twice dropped
# off the network entirely (no ping, no ssh on WiFi/tether/static, needing a
# power cycle) shortly after capture runs. The first time was blamed on rapid
# ssh connections; the second happened after only an install plus ONE capture,
# which does not fit that explanation. The better hypothesis is that running
# EnginePerfTool while the normal Move stack is live has both fighting over the
# audio device.
#
# Until that is understood, prefer STOP_STACK=1, which stops the launcher for
# the duration of the capture and restarts it afterwards.
#
# Usage: STOP_STACK=1 tools/oracle.sh <song.abl> <out.wav> [frames]
set -euo pipefail

SONG="$1"
OUT="$2"
FRAMES="${3:-88200}"
HOST="${MOVE_HOST:-move.local}"
REMOTE=/tmp/dr32-oracle

if [ "${STOP_STACK:-0}" = "1" ]; then
    echo "==> stopping the Move stack for the capture" >&2
    ssh "root@${HOST}" "systemctl stop move-launcher.service 2>/dev/null || /etc/init.d/move stop 2>/dev/null; true" || true
fi

ssh "ableton@${HOST}" "mkdir -p ${REMOTE}"
scp -q "$SONG" "ableton@${HOST}:${REMOTE}/song.abl"

ssh "ableton@${HOST}" "/opt/move/EnginePerfTool \
    --fake-driver --silent --render-audio \
    --duration=${FRAMES} --num-threads=1 \
    --audio-output=${REMOTE}/out.wav \
    --output=${REMOTE}/out.json \
    ${REMOTE}/song.abl" >/dev/null

scp -q "ableton@${HOST}:${REMOTE}/out.wav" "$OUT"

if [ "${STOP_STACK:-0}" = "1" ]; then
    echo "==> restarting the Move stack" >&2
    ( cd "$(dirname "$0")/../.." && MOVE_HOST="${HOST}" ./scripts/restart_move.sh >/dev/null 2>&1 ) || true
fi

echo "$OUT: $(ls -l "$OUT" | awk '{print $5}') bytes"
