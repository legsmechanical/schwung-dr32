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
    echo "==> validating module.json"
    node tools/check_module_json.mjs src/module.json || exit 1

    # ui.js is the play view only and imports nothing but the host's shared
    # base, so there is no longer a bundle step — it ships as written.
    mkdir -p build
    cp src/ui.js build/ui.js

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
    # DR32 needs BOTH gcc and g++ (the FX bus is C++ — vendored reverbs).
    # davebox-builder ships only the C cross compiler, so merely existing is not
    # enough: probe each candidate for aarch64-linux-gnu-g++ before choosing it.
    BUILDER=""
    for img in schwung-builder move-anything-builder davebox-builder; do
        docker image inspect "$img" >/dev/null 2>&1 || continue
        if docker run --rm "$img" sh -c 'command -v aarch64-linux-gnu-g++' >/dev/null 2>&1; then
            BUILDER="$img"; break
        fi
        echo "==> $img has no aarch64 g++, skipping" >&2
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
    # Propagate the container's exit status. This used to `exit 0`
    # unconditionally, so a failed compile inside docker reported success, left
    # dist/ holding the PREVIOUS build, and install.sh shipped a stale binary.
    docker run --rm -v "$HERE":/work -w /work \
        -e CROSS_PREFIX=aarch64-linux-gnu- \
        "$BUILDER" bash scripts/build.sh
    status=$?
    [ $status -eq 0 ] || echo "ERROR: build failed inside docker (status $status); dist/ NOT updated" >&2
    exit $status
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
CC="${CROSS_PREFIX}gcc"
CXX="${CROSS_PREFIX}g++"
ARCH="-march=armv8-a -mtune=cortex-a72"

echo "==> compiling with $CC / $CXX"
mkdir -p build/obj

# The engine is C11; the FX bus is C++ because the vendored reverbs are C++
# structs (dsp/vendor/SOURCES.md). Compile each with its own front end and link
# with g++ so the C++ runtime bits resolve.
for src in dsp/dr32.c dsp/dr32_params.c dsp/dr32_kit.c dsp/dr32_voice.c \
           dsp/dr32_effects.c dsp/dr32_preset.c dsp/dr32_json.c dsp/dr32_state.c dsp/wav.c; do
    $CC -O2 -fPIC $ARCH -DNDEBUG -std=c11 -Wall -Wextra -Idsp \
        -c "$src" -o "build/obj/$(basename "${src%.c}").o"
done

# dsp/dr32_fxbus.cpp is NOT built into the shipped .so any more. It is the
# internal send + Drum Bus container, which the host owns now; nothing in the
# kit references it, and the only remaining consumer is the offline null-test
# renderer, which compiles it itself (tests/run.sh). -Wl,--no-undefined below
# is what proves the kit really has no reference left rather than us hoping so.
$CXX -O2 -fPIC $ARCH -DNDEBUG -std=c++17 -Wall -Wextra -Idsp \
    -c dsp/dr32_efx_names.cpp -o build/obj/dr32_efx_names.o

# -Wl,--no-undefined on BOTH links. A shared library links clean with undefined
# symbols by default, so a call to a function whose translation unit was never
# added succeeds here and fails at dlopen on the device -- which the host shows
# as an insert stuck on "Loading..." with nothing logged as an error. That is
# exactly what shipped once; the link is the only place it can be caught.
$CXX -shared -Wl,--no-undefined -o build/dsp.so build/obj/*.o -lm

# The EFFECTS module: the same Drum Buss stages, as ordinary chain inserts.
# One binary, four effects chosen by `effect` -- audio_fx_api_v2 is
# multi-instance, so a bus holding all four is four create_instance calls on
# this file. It shares dsp/dr32_drumbus.h with the kit rather than copying it.
FX_ID=dr32-fx
$CXX -O2 -fPIC $ARCH -DNDEBUG -std=c++17 -Wall -Wextra -I. -Idsp -Ifx \
    -c fx/dr32_fx.cpp -o build/obj/dr32_fx.o
$CXX -shared -Wl,--no-undefined -o build/dr32-fx.so \
    build/obj/dr32_fx.o build/obj/dr32_efx_names.o -lm

echo "==> packaging dist/"
rm -rf "dist/${MODULE_ID}" "dist/${FX_ID}"
mkdir -p "dist/${MODULE_ID}" "dist/${FX_ID}"
# `cat` and not `cp` for anything BUILT: on an ExtFS volume cp attempts a clone
# and fails with "error deallocating", which under `set -e` aborts the script
# after the .so is half-written -- a truncated dsp.so and no tarball, reported
# as a copy error rather than as a build failure. The fx .so below learned this
# first; the kit's did not, and started failing the day dsp.so grew.
cat build/dsp.so > "dist/${MODULE_ID}/dsp.so"
chmod 755 "dist/${MODULE_ID}/dsp.so"
cat build/ui.js > "dist/${MODULE_ID}/ui.js"
cp src/module.json  "dist/${MODULE_ID}/"

# The host loads an audio FX by the path in its module.json; dsp.so is the name
# every other module uses, so it is dsp.so here too.
# <id>/<id>.so, NOT dsp.so.
#
# A bus insert is loaded by chain_bus.c with a hardcoded
# "%s/../audio_fx/%s/%s.so" -- the freeverb convention (freeverb/freeverb.so),
# not the dsp.so every other module type uses. Named dsp.so the dlopen simply
# fails, bus_fx_ready never goes true, and every read answers null: the editor
# holds on "Loading..." forever with nothing logged as an error.
cat build/dr32-fx.so > "dist/${FX_ID}/${FX_ID}.so"
chmod 755 "dist/${FX_ID}/${FX_ID}.so"
cp fx/module.json   "dist/${FX_ID}/"
[ -f fx/help.json ] && cp fx/help.json "dist/${FX_ID}/"

tar -czf "dist/${MODULE_ID}-module.tar.gz" -C dist "${MODULE_ID}"
tar -czf "dist/${FX_ID}-module.tar.gz"     -C dist "${FX_ID}"
echo "==> done: dist/${MODULE_ID}-module.tar.gz + dist/${FX_ID}-module.tar.gz"
