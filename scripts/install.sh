#!/usr/bin/env bash
# install.sh — deploy dist/dr32/ to the Move.
# Usage: scripts/install.sh                          (WiFi / ssh-config alias)
#        MOVE_HOST=172.16.254.1 scripts/install.sh   (USB tether)
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
ssh "ableton@${HOST}" "chmod -R a+rw '${DEST}'"
echo "==> installed. Swap the synth out/in (or restart) to reload a live .so."
