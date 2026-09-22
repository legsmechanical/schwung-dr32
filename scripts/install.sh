#!/usr/bin/env bash
# install.sh — deploy dist/dr32/ to the Move AND restart the stack.
#
# ⚠ A RESTART IS REQUIRED, not optional. Swapping the synth out and back in does
# NOT pick up new module code — the old dsp.so/module.json stay live and the
# deploy silently appears to do nothing. So this script always restarts, via the
# canonical scripts/restart_move.sh.
#
# Usage: scripts/install.sh                          (WiFi / ssh-config alias)
#        MOVE_HOST=172.16.254.1 scripts/install.sh   (USB tether)
#        SKIP_RESTART=1 scripts/install.sh           (deploy only — you restart)
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
MODULE_ID=dr32
HOST="${MOVE_HOST:-move.local}"
DEST="/data/UserData/schwung/modules/sound_generators/${MODULE_ID}"

[ -d "$HERE/dist/${MODULE_ID}" ] || { echo "run scripts/build.sh first" >&2; exit 1; }

echo "==> installing to ableton@${HOST}:${DEST}"
ssh "ableton@${HOST}" "mkdir -p '${DEST}'"
# temp-name + mv dodges ETXTBSY if the .so is currently loaded
scp "$HERE/dist/${MODULE_ID}/dsp.so" "ableton@${HOST}:${DEST}/.dsp.so.new"
ssh "ableton@${HOST}" "mv -f '${DEST}/.dsp.so.new' '${DEST}/dsp.so'"
# ⚠⚠ SHIP WHAT build.sh PACKAGED, do not name files here. This was
# `for f in module.json ui.js`, a second list that had to be kept in step with
# the one in build.sh — and it was not: help.json was added to the package and
# silently never reached the device, because the installer did not know the word.
# Nothing failed; the file was simply absent, and the host shows no Module Help
# row for a module with no help.json, so the symptom was a missing FEATURE
# rather than an error. dsp.so is excluded only because it needs the
# temp-name + mv dance above to dodge ETXTBSY.
# Directories too (-r): 9W9's cymbal WAVs are dist/<id>/samples/9w9/, and a
# files-only loop shipped the rest of the package without them — silently.
for f in "$HERE/dist/${MODULE_ID}"/*; do
    [ -f "$f" ] || [ -d "$f" ] || continue
    [ "$(basename "$f")" = "dsp.so" ] && continue
    scp -r "$f" "ableton@${HOST}:${DEST}/"
done
# ⚠ canvas.js is SHIPPED again (the PAD cell's big number, 2026-09-22). This
# line used to delete it, left over from the Pad Editor removed in 0.2.0: kept,
# it would have removed the new widget right after copying it, and the cell
# would silently fall back to the small number.
ssh "ableton@${HOST}" "chmod -R a+rw '${DEST}'"
echo "==> installed"

if [ "${SKIP_RESTART:-0}" = "1" ]; then
    echo "==> SKIP_RESTART set — remember the module will NOT reload until you restart"
    exit 0
fi

MOVE_HOST="root@${HOST}" "$HERE/../scripts/restart_move.sh"
