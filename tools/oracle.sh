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
# STOP_STACK=1 tears the Move stack down for the capture and brings it back
# afterwards, both via the CANONICAL scripts/restart_move.sh (MOVE_ACTION=stop
# / default restart). Do not hand-roll a launcher stop here: stopping the
# launcher alone leaves shadow_ui / schwung / display-server / schwung-manager
# running, the /dev/shm rings stale and /dev/ablspi0.0 held — the partial-stop
# trap that script exists to avoid. This script did exactly that until
# 2026-07-26; Josh caught it.
#
# What the evidence says about the two full device lockups: both happened
# BEFORE any stop existed, i.e. captures against a fully live stack. So
# "capture while the stack is live" is the better-supported theory, and the
# partial stop was a separate (real) defect rather than the proven cause.
#
# ⚠ A FLAT BATTERY LOOKS EXACTLY LIKE THAT LOCKUP. Both present as: no ping on
# WiFi AND none on the USB tether, a stale ARP entry, and the host's own network
# fine. On 2026-07-28 a batch was followed by exactly that picture and written up
# as a third lockup — it was the battery. Check that the Move is plugged in and
# powered before concluding anything about captures.
#
# Usage: STOP_STACK=1 tools/oracle.sh <song.abl> <out.wav> [frames]
set -euo pipefail

SONG="$1"
OUT="$2"
FRAMES="${3:-88200}"
HOST="${MOVE_HOST:-move.local}"
REMOTE=/tmp/dr32-oracle

SCRIPTS="$(cd "$(dirname "$0")/../.." && pwd)/scripts"

if [ "${STOP_STACK:-0}" = "1" ]; then
    MOVE_ACTION=stop MOVE_HOST="root@${HOST}" "$SCRIPTS/restart_move.sh" >&2
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
    MOVE_HOST="root@${HOST}" "$SCRIPTS/restart_move.sh" >&2
fi

echo "$OUT: $(ls -l "$OUT" | awk '{print $5}') bytes"
