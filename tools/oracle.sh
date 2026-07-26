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
# ⚠ KNOWN DEFECT IN THIS SCRIPT (2026-07-26, raised by Josh): the STOP_STACK
# teardown below is NOT canonical. It stops the launcher only, which is exactly
# the partial-stop anti-pattern `scripts/restart_move.sh` exists to prevent —
# it leaves shadow_ui / schwung / display-server / schwung-manager running, the
# /dev/shm rings stale, and /dev/ablspi0.0 held, and THEN starts a second
# engine on top of all that.
#
# The restarts here ARE canonical (they call scripts/restart_move.sh).
#
# What the evidence actually says: both lockups happened BEFORE any stop
# existed — captures against a fully live stack. The batch that used the
# partial stop survived. So "capture while the stack is live" is still the
# better-supported theory, but the partial stop should be replaced regardless.
#
# PROPOSED FIX (needs Josh's OK — shared tooling): add `MOVE_ACTION=stop` to
# scripts/restart_move.sh so the same clean teardown can run without starting
# again, and call that here instead of the launcher stop.
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
