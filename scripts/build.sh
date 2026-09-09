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
    #
    # ⚠ If `docker build` here fails with apt "At least one invalid signature
    # was encountered", that is NOT an architecture or GPG problem — it is the
    # Docker VM's disk being FULL. Check with:
    #     docker run --rm ubuntu:22.04 df -h /
    # and reclaim with `docker builder prune -af` (and/or remove unused images).
    # A full VM also makes `docker image inspect` fail intermittently, which
    # looks like the image "disappearing" and silently leaves dist/ stale.
    # ── TOOLCHAIN SELECTION IS A CONVENIENCE; THE VERSION ASSERT IS THE GUARANTEE
    #
    # This used to probe three images and take the first that had a cross g++,
    # and that silently decided which COMPILER built the artifact. The images do
    # not agree: schwung-builder and davebox-builder carry gcc 12.2.0,
    # move-anything-builder carries 11.4.0. Same source, two different binaries
    # (proven: 11.4 -> a49f4fe9, 12.2 -> c191a9c3).
    #
    # ⚠⚠ AND THE FALLTHROUGH WAS SILENT. The documented full-VM symptom —
    # `docker image inspect` failing intermittently — makes the first candidate
    # look absent, so the loop moves to the next one. The only trace is a line of
    # build output nobody reads. Three commits shipped a gcc 11.4 artifact that
    # had been reported as the verified 12.2 build.
    #
    # So the name below is a PREFERENCE, and the assert after the build is what
    # actually holds: gcc records its own version in the .so's .comment section,
    # so the artifact has always been self-identifying — it was simply never
    # checked. A wrong-compiler build now FAILS instead of shipping.
    #
    # (The old comment here said DR32 needs g++ because the FX bus is C++. That
    # stopped being true on 2026-09-08 when the FX bus left; there is no C++ in
    # this module any more, which is why the probe is for gcc.)
    # ⚠⚠ DO NOT GATE ON `docker image inspect`. It fails while the image is
    # present and runnable — observed 5/5 on schwung-builder at a moment when
    # `docker run schwung-builder` worked and reported gcc 12.2.0. That false
    # negative is the ROOT CAUSE of the whole silent-compiler episode: the old
    # loop used inspect as its presence test, it failed, and the build fell
    # through to an image with a different gcc without anything saying so.
    #
    # The run-probe IS the presence test and the capability test at once. If the
    # container starts and has the cross compiler, the image is usable; nothing
    # else needs asking.
    usable() { docker run --rm "$1" sh -c 'command -v aarch64-linux-gnu-gcc' >/dev/null 2>&1; }

    BUILDER=""
    for img in "${DR32_BUILDER:-schwung-builder}" schwung-builder davebox-builder move-anything-builder; do
        [ -n "$img" ] || continue
        if usable "$img"; then BUILDER="$img"; break; fi
    done

    # Nothing usable: distinguish a full VM (the documented cause) from a
    # genuinely missing toolchain, then build our own rather than give up.
    if [ -z "$BUILDER" ]; then
        free_kb=$(docker run --rm ubuntu:22.04 df -k / 2>/dev/null | awk 'NR==2{print $4}')
        if [ -n "$free_kb" ] && [ "$free_kb" -lt 262144 ]; then
            echo "ERROR: Docker VM disk is nearly full (${free_kb} KB free)." >&2
            echo "       Reclaim space first:  docker builder prune -af" >&2
            exit 1
        fi
        echo "==> no usable toolchain image; building one" >&2
        docker build -q -t move-anything-builder -f scripts/Dockerfile scripts >/dev/null || {
            echo "ERROR: could not build a toolchain image" >&2; exit 1; }
        BUILDER=move-anything-builder
        usable "$BUILDER" || { echo "ERROR: built image lacks the cross compiler" >&2; exit 1; }
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
    [ $status -eq 0 ] || exit $status

    # ── THE GUARANTEE: the artifact must name the compiler we expect.
    #
    # gcc writes its version into .comment, so this reads the built .so rather
    # than trusting which image was chosen — it catches a fallthrough, an image
    # rebuilt on a moved base, and a hand-run container, all the same way.
    # DR32_GCC=<version> to move the pin deliberately; there is no way to move it
    # by accident.
    want="${DR32_GCC:-12.2.0}"
    got=$(strings build/dsp.so 2>/dev/null | sed -n 's/^GCC: .*) \([0-9.]*\)$/\1/p' | sort -u | tr '\n' ' ')
    case " $got " in
      *" $want "*) echo "==> built by gcc $want (verified in the artifact)" ;;
      *) echo "ERROR: dsp.so was built by gcc '${got:-<none recorded>}', expected $want." >&2
         echo "       The image used was '$BUILDER'. A different compiler produces a" >&2
         echo "       different binary from identical source, so this artifact is NOT" >&2
         echo "       comparable with the one on the device or in git." >&2
         echo "       Fix the toolchain rather than the expectation; DR32_GCC=$got" >&2
         echo "       overrides it only if the move is deliberate." >&2
         # ⚠ REMOVE WHAT INSTALL.SH ACTUALLY READS. It ships the DIRECTORY
         # (`dist/<id>/`), not the tarball — deleting only the tarball left the
         # bad binary sitting exactly where the installer looks for it, which is
         # the guard I wrote first and did not check. install.sh already refuses
         # a missing dist dir with "run scripts/build.sh first".
         rm -rf "dist/${MODULE_ID}" "dist/${MODULE_ID}-module.tar.gz"
         exit 1 ;;
    esac
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
CC="${CROSS_PREFIX}gcc"
CXX="${CROSS_PREFIX}g++"
ARCH="-march=armv8-a -mtune=cortex-a72"

echo "==> compiling with $CC / $CXX"
# ⚠ WIPE FIRST — the link line below is `build/obj/*.o`, so an object left over
# from a source that has since been DELETED is still globbed and still linked.
# That is not hypothetical: removing the FX bus left a stale dr32_fxbus.o here,
# and it went into dsp.so on the next build. A shared-library link does not have
# to resolve undefined symbols, so it did not even fail — it just quietly
# carried the removed reverbs and their unresolved C++ runtime references into
# the shipped artifact. A stale artifact is a decoy; the same rule as tests/run.sh.
rm -rf build/obj
mkdir -p build/obj

# DR32 is C11 throughout. It used to link with g++ because the FX bus was C++
# (the vendored reverbs were C++ structs); that whole stage moved out with the
# internal send/return framework, so there is no C++ translation unit left and
# nothing here needs the C++ runtime.
# ⚠⚠ GLOB, DO NOT LIST. This was an explicit list of nine files while
# tests/run.sh globbed `dsp/*.c`, so the two builds compiled DIFFERENT SETS of
# sources. Adding dsp/dr32_kits.c passed the whole suite and shipped a dsp.so
# without it in — the link globs build/obj/*.o, so it found only what had been
# compiled, printed "==> done:", passed the compiler assert, and installed. The
# device caught it at dlopen: "undefined symbol: dr32_kits_name".
# One list, derived the same way in both places, is the fix; two lists that must
# be kept in step is the bug.
for src in dsp/*.c; do
    $CC -O2 -fPIC $ARCH -DNDEBUG -std=c11 -Wall -Wextra -Idsp \
        -c "$src" -o "build/obj/$(basename "${src%.c}").o"
done

# ⚠⚠ --no-undefined IS LOAD-BEARING, not tidiness.
#
# A -shared link is ALLOWED to be incomplete: an unresolved symbol is left for
# whoever dlopen's you, so the link cannot tell you a whole source file is
# missing. That is exactly how a dsp.so without dr32_kits.o in it linked, exited
# 0, printed "==> done:", passed the compiler assert and installed — the failure
# surfaced on the DEVICE as `dlopen failed: undefined symbol: dr32_kits_name`.
#
# Proven on the same objects with dr32_kits.o removed: without the flag the link
# exits 0; with it, 12 errors naming the symbols. DR32 resolves everything from
# libc/libm, so this costs nothing here.
$CC -shared -Wl,--no-undefined -o build/dsp.so build/obj/*.o -lm

echo "==> packaging dist/"
rm -rf "dist/${MODULE_ID}"
mkdir -p "dist/${MODULE_ID}"
cp build/dsp.so     "dist/${MODULE_ID}/"
cp build/ui.js      "dist/${MODULE_ID}/"
cp src/module.json  "dist/${MODULE_ID}/"
# On-device help. The host discovers help.json by scanning module directories,
# so shipping it is the whole wiring — and a module without one gets no
# "Module Help" row at all, which is why it is copied rather than optional in
# spirit only.
[ -f src/help.json ] && cp src/help.json "dist/${MODULE_ID}/"

tar -czf "dist/${MODULE_ID}-module.tar.gz" -C dist "${MODULE_ID}"
echo "==> done: dist/${MODULE_ID}-module.tar.gz"
