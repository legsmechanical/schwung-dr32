#!/usr/bin/env bash
# Regenerate the synth engines' C++ from their Faust sources.
#
# Each engine under dsp/engines/<name>/faust/ is a vendored copy of one
# OneTrick port's DSP (schwung-simian, schwung-urchin). Everything in
# dsp/engines/<name>/generated/ is machine output: never hand-edit it; edit
# the .dsp/.lib and re-run this. Requires `faust` (brew install faust) on the
# workstation — the build container has none, which is why the output is
# committed.
#
# ⚠⚠ THE CLASS NAMES ARE THE POINT. Both ports generate `DSP_Drum` with the
# include guard `__DSP_Drum_H__` and the same file-scope table names. In one
# .so that is not a compile error: in one TU the second header is silently
# skipped, and across TUs the inline methods and vtables are merged by the
# linker, so SIMIAN voices can end up running URCHIN's compute. Every class
# here gets an engine-unique name, and each engine is its own TU.
#
# ⚠ Each engine keeps ITS OWN .lib files and resolves them through its own
# --import-dir. The two shared.lib files are different libraries with the same
# name; never point one engine at the other's folder.
#
# The flags are the ports' own: --check-table 0 and --timeout 0 are
# load-bearing for SIMIAN's cymbal wavetable.
set -euo pipefail
cd "$(dirname "$0")/../dsp/engines"

command -v faust >/dev/null 2>&1 || { echo "error: faust not found (brew install faust)" >&2; exit 1; }
echo "Faust: $(faust --version | head -1)"

gen() { # <engine dir> <dsp file> <class name> <output header>
    mkdir -p "$1/generated"
    echo "  $1/$2 -> $1/generated/$4 ($3)"
    faust --check-table 0 --timeout 0 --process-name process -lang cpp \
        --import-dir "$1/faust" -cn "$3" -dlt 65536 \
        -o "$1/generated/$4" "$1/faust/$2"
}

gen simian drum.dsp   SimianVoice  simian_voice.hpp
gen urchin drum.dsp   UrchinDrum   urchin_drum.hpp
gen urchin snare.dsp  UrchinSnare  urchin_snare.hpp
gen urchin cymbal.dsp UrchinCymbal urchin_cymbal.hpp
gen urchin media.dsp  UrchinMedia  urchin_media.hpp    # DR32's per-pad Media stage
echo "Done."
