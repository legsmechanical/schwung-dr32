#!/usr/bin/env bash
# build.sh — cross-compile dsp.so for the Move (aarch64), bundle ui.js, and
# package dist/dr32/ + dist/dr32-module.tar.gz.
#
# Auto-Dockerizes: if CROSS_PREFIX is unset and we're not in a container,
# build the toolchain image and re-run inside it.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
MODULE_ID=dr32

# --- UI bundle (host side: needs node/esbuild, so do it before Docker)
if [ -z "${CROSS_PREFIX:-}" ] && [ ! -f /.dockerenv ]; then
    echo "==> bundling ui.js"
    # lib/ablpreset.mjs is the CANONICAL parser; src/ gets a build-time copy so
    # the bundle has a single source of truth to import.
    cp lib/ablpreset.mjs src/ablpreset.mjs
    ESBUILD="$(command -v esbuild || echo ../schwung-davebox/node_modules/.bin/esbuild)"
    "$ESBUILD" src/ui.js --bundle --format=esm --outfile=build/ui.js \
        --external:'/data/UserData/schwung/*' --log-level=warning
    rm -f src/ablpreset.mjs

    echo "==> building dsp in Docker (move-anything-builder)"
    # Reuse the existing toolchain image if present. Rebuilding it on an arm64
    # Mac fails (the gcc-aarch64-linux-gnu package resolves differently for an
    # arm64 host), and the image we already have is the one that built the other
    # modules — so only build it when it is genuinely missing.
    if ! docker image inspect move-anything-builder >/dev/null 2>&1; then
        docker build -q -t move-anything-builder -f scripts/Dockerfile scripts >/dev/null
    fi
    docker run --rm -v "$HERE":/work -w /work \
        -e CROSS_PREFIX=aarch64-linux-gnu- \
        move-anything-builder bash scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
CC="${CROSS_PREFIX}gcc"

echo "==> compiling with $CC"
mkdir -p build
$CC -O2 -shared -fPIC -march=armv8-a -mtune=cortex-a72 \
    -DNDEBUG -std=c11 -Wall -Wextra \
    dsp/dr32.c dsp/dr32_kit.c dsp/dr32_voice.c dsp/wav.c \
    -Idsp \
    -o build/dsp.so -lm

echo "==> packaging dist/"
rm -rf "dist/${MODULE_ID}"
mkdir -p "dist/${MODULE_ID}"
cp build/dsp.so     "dist/${MODULE_ID}/"
cp build/ui.js      "dist/${MODULE_ID}/"
cp src/module.json  "dist/${MODULE_ID}/"

tar -czf "dist/${MODULE_ID}-module.tar.gz" -C dist "${MODULE_ID}"
echo "==> done: dist/${MODULE_ID}-module.tar.gz"
