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

    echo "==> building dsp in Docker"
    # Reuse the existing toolchain image if present. Rebuilding it on an arm64
    # Mac fails (the gcc-aarch64-linux-gnu package resolves differently for an
    # arm64 host), and the image we already have is the one that built the other
    # modules — so only build it when it is genuinely missing.
    # Toolchain image. `davebox-builder` is a native arm64 Debian image that
    # already carries aarch64-linux-gnu-gcc; prefer it, and only build our own
    # if none of the known images exist.
    #
    # ⚠ If `docker build` here fails with apt "At least one invalid signature
    # was encountered", that is NOT an architecture or GPG problem — it is the
    # Docker VM's disk being FULL. Check with:
    #     docker run --rm ubuntu:22.04 df -h /
    # and reclaim with `docker builder prune -af` (and/or remove unused images).
    # A full VM also makes `docker image inspect` fail intermittently, which
    # looks like the image "disappearing" and silently leaves dist/ stale.
    BUILDER=""
    for img in davebox-builder schwung-builder move-anything-builder; do
        if docker image inspect "$img" >/dev/null 2>&1; then BUILDER="$img"; break; fi
    done

    # Fail loudly on a full VM rather than limping on with a stale dist/.
    if [ -z "$BUILDER" ]; then
        free_kb=$(docker run --rm ubuntu:22.04 df -k / 2>/dev/null | awk 'NR==2{print $4}')
        if [ -n "$free_kb" ] && [ "$free_kb" -lt 262144 ]; then
            echo "ERROR: Docker VM disk is nearly full (${free_kb} KB free)." >&2
            echo "       Reclaim space first:  docker builder prune -af" >&2
            exit 1
        fi
    fi
    if [ -z "$BUILDER" ]; then
        echo "==> no toolchain image found; building one" >&2
        docker build -q -t move-anything-builder -f scripts/Dockerfile scripts >/dev/null
        BUILDER=move-anything-builder
    fi
    echo "==> using $BUILDER" 
    docker run --rm -v "$HERE":/work -w /work \
        -e CROSS_PREFIX=aarch64-linux-gnu- \
        "$BUILDER" bash scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
CC="${CROSS_PREFIX}gcc"

echo "==> compiling with $CC"
mkdir -p build
$CC -O2 -shared -fPIC -march=armv8-a -mtune=cortex-a72 \
    -DNDEBUG -std=c11 -Wall -Wextra \
    dsp/dr32.c dsp/dr32_params.c dsp/dr32_kit.c dsp/dr32_voice.c \
    dsp/dr32_effects.c dsp/dr32_preset.c dsp/dr32_json.c dsp/wav.c \
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
