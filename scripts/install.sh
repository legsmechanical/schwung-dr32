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
for f in module.json ui.js; do
    scp "$HERE/dist/${MODULE_ID}/$f" "ableton@${HOST}:${DEST}/"
done
# The canvas Pad Editor is gone (0.2.0); a stale canvas.js left on the device
# is harmless to the host but misleading to anyone reading the module dir.
ssh "ableton@${HOST}" "rm -f '${DEST}/canvas.js'"
ssh "ableton@${HOST}" "chmod -R a+rw '${DEST}'"
echo "==> installed"

if [ "${SKIP_RESTART:-0}" = "1" ]; then
    echo "==> SKIP_RESTART set — remember the module will NOT reload until you restart"
    exit 0
fi

MOVE_HOST="root@${HOST}" "$HERE/../scripts/restart_move.sh"
