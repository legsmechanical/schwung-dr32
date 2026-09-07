// dr32_efx_names.cpp -- the send types' names and their musical defaults.
//
// SEPARATE TRANSLATION UNIT because two binaries need it and only one of them
// wants the bus container: the kit links dr32_fxbus.cpp, `dr32-fx` links only
// its own file plus this. It was inside dr32_fxbus.cpp, and `dr32-fx` called
// dr32_efx_defaults without linking it -- a shared library links with
// undefined symbols allowed, so the build was clean and the DLOPEN would have
// failed on the device, which the host shows as an insert stuck on
// "Loading..." with nothing logged. `-Wl,--no-undefined` on both links is the
// other half of that fix; this file is what makes it satisfiable.

#include "dr32_fxbus.h"

#include <cstring>

void dr32_efx_defaults(dr32_efx_type type, float *o) {
    if (!o) return;
    // Slot table is in dr32_fxbus.h. Reverbs use 0-3; the Delay uses all eight.
    for (int i = 0; i < DR32_SEND_PARAMS; i++) o[i] = 0.0f;
    switch (type) {
        case DR32_EFX_PLATE:
            // Snare/clap plate: medium tank, a little damping so it is not
            // brittle, short-ish tail, ~10 ms pre-delay to keep the hit clear.
            o[0] = 0.45f; o[1] = 0.35f; o[2] = 0.45f; o[3] = 0.05f; break;
        case DR32_EFX_SPACES:
            // Lands as a natural mid-size room rather than at either extreme:
            // Spaces is the one control set for everything from a tight room to
            // a hall, so it should open somewhere you would actually start.
            o[0] = 0.35f; o[1] = 0.40f; o[2] = 0.45f; o[3] = 0.02f; break;
        case DR32_EFX_GATED:
            // ⚠ A TIGHT tank, not a big one — measured, against my own first
            // guess of 0.70. A gate needs something to gate, and the tank's
            // density build dominates the first ~180 ms: at size 0.70 the
            // window still RISES through the whole hold (+6 dB) and the type is
            // indistinguishable from NonLin sitting next to it. At size 0.20 it
            // peaks around 65 ms and then falls cleanly (+6.9 -> -10.4 dB by
            // 240 ms) — quick dense build, audible decay, then the chop, which
            // is the sound this is for.
            //
            // Slot 2 is the gate HOLD (50..500 ms), slot 4 the tank's OWN decay,
            // slot 5 the release. No pre-delay: the gate is the shape.
            o[0] = 0.20f; o[1] = 0.20f; o[2] = 0.45f; o[3] = 0.0f;
            o[4] = 0.30f; o[5] = 0.28f; break;
        case DR32_EFX_DIGITAL:
            // 80s rack: mid-size, fairly bright, a medium tail. Its 12-bit loop
            // grain and chorused modulation are internal and always on — that is
            // the sound, not a fault.
            //
            // ⚠ SpaceExtra's sibling LoFi type (the same FDN at fs/3 and 7 bits)
            // is deliberately NOT offered. Measured, its decay knob is dead:
            // RT60 spans 0.19 s to 0.26 s across the whole control, and raising
            // the bit depth 7 -> 11 only reaches 0.41 s, so the short tail is
            // structural to that voicing rather than a quantisation floor. The
            // effect is usable but the control is not, and a dead knob is the
            // defect this suite exists to catch. Revisit with its own pass.
            o[0] = 0.50f; o[1] = 0.45f; o[2] = 0.50f; o[3] = 0.03f; break;
        case DR32_EFX_HALL:
            // ⚠ Chamber reaches cathedral length at the top of its range and was
            // pulled from DR32 once for exactly that (07ca02c). Decay is scaled
            // to 0.62 in apply() and the default sits mid-knob; the RT60 test
            // holds it under 3 s.
            o[0] = 0.55f; o[1] = 0.45f; o[2] = 0.45f; o[3] = 0.04f; break;
        case DR32_EFX_NONLIN:
            // A dense bright tank — the window is doing the shaping, so the
            // reverb under it wants density rather than character. Slot 2 is the
            // window LENGTH (0.45 -> ~300 ms, the classic non-lin), slot 4 its
            // TILT, defaulting dead flat. No pre-delay: the point is that the
            // hit and the window start together.
            o[0] = 0.35f; o[1] = 0.25f; o[2] = 0.45f; o[3] = 0.0f; o[4] = 0.5f;
            o[5] = 0.07f; break;   // ~1.5 ms, the cliff this always had
        case DR32_EFX_NATIVE:
            // A stock drum-kit room, in the device's own numbers: RoomSize 60
            // (the value the factory kits' return chains carry), a ~1.5 s decay,
            // a little HF damping, and Diffusion at 0.6 — again the factory
            // kits' own value. Pre-delay 0: the native device's own PreDelay is
            // 0 in every kit return chain, and DR32's line stands in for it.
            o[0] = 0.55f; o[1] = 0.59f; o[2] = 0.42f; o[3] = 0.0f;
            o[4] = 0.46f; break;
        case DR32_EFX_DELAY:
            // The factory corpus's dominant configuration, not a guess: L = 1
            // sixteenth and R = 4 is what ten of the twelve native delay returns
            // use, feedback is their median (0.50 against a 0.12-0.73 spread),
            // and tone 0.55 puts the feedback bandpass at ~1.3 kHz, their median
            // centre. Ping-pong starts off — the library is split 7/5 on it, and
            // the L/R asymmetry already gives a stereo pattern without it.
            //
            // Synced by default (11 of the 12 native delay returns are), and the
            // free times are seeded with what the synced pair produces at
            // 120 BPM — so flipping Sync off does not move the delay, it just
            // stops it following the tempo.
            o[0] = 1.0f; o[1] = 4.0f; o[2] = 0.50f; o[3] = 0.55f; o[4] = 0.0f;
            o[5] = 1.0f; o[6] = 125.0f; o[7] = 500.0f; break;
        default:
            o[0] = 0.5f; o[1] = 0.3f; o[2] = 0.5f; o[3] = 0.0f; break;
    }
}

const char *dr32_efx_name(dr32_efx_type type) {
    switch (type) {
        case DR32_EFX_PLATE:  return "Plate";
        case DR32_EFX_SPACES: return "Spaces";
        case DR32_EFX_DELAY:  return "Delay";
        case DR32_EFX_GATED:  return "Gated";
        case DR32_EFX_DIGITAL:return "Digital";
        case DR32_EFX_HALL:   return "Hall";
        case DR32_EFX_NONLIN: return "NonLin";
        case DR32_EFX_NATIVE: return "Native";
        default:              return "Off";
    }
}

dr32_efx_type dr32_efx_from_name(const char *name) {
    if (!name || !*name) return DR32_EFX_NONE;
    if (!std::strcmp(name, "Plate")) return DR32_EFX_PLATE;
    if (!std::strcmp(name, "Spaces")) return DR32_EFX_SPACES;
    if (!std::strcmp(name, "Delay")) return DR32_EFX_DELAY;
    if (!std::strcmp(name, "Gated")) return DR32_EFX_GATED;
    if (!std::strcmp(name, "Digital")) return DR32_EFX_DIGITAL;
    if (!std::strcmp(name, "Hall")) return DR32_EFX_HALL;
    if (!std::strcmp(name, "NonLin")) return DR32_EFX_NONLIN;
    if (!std::strcmp(name, "Native")) return DR32_EFX_NATIVE;
    // Anything else, including a saved state naming the old "Drum Bus" send
    // type, falls through to Off.
    return DR32_EFX_NONE;
}
