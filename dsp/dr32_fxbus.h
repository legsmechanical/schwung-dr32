// dr32_fxbus.h — DR32's effect buses: 2 sends.
//
// The native Drum Rack has ONE send (a single return chain); DR32 doubles it,
// the same way it doubles the pads.
//
// There are deliberately NO kit inserts. DR32 had two, but a Schwung chain slot
// already carries its own insert FX (fx1..fx4) in front of the output, so a
// kit-level insert was a second, worse copy of a facility the host provides —
// worse because it was reachable only from inside DR32 and had to be persisted
// by DR32. Put an insert on the slot instead (Josh, 2026-07-27). The effect
// implementations stay: the sends use them, and Drum Bus is being lifted into a
// standalone audio_fx module.
//
// Signal flow per rendered block:
//
//     pads --(post-fader dB send amounts)--> send bus 1 -> FX --+
//        |                                 -> send bus 2 -> FX --+
//        |                                                       |
//        +------------------ dry ------------------------------> out
//
// C API over C++ DSP: the vendored reverbs are C++ structs, the rest of DR32
// is C11. Implementation lives in dr32_fxbus.cpp.

#ifndef DR32_FXBUS_H
#define DR32_FXBUS_H

#ifdef __cplusplus
extern "C" {
#endif

/** Effect types available to a send. 0 is always "off". */
typedef enum {
    DR32_EFX_NONE = 0,
    DR32_EFX_PLATE,        // Dattorro figure-eight plate tank + input diffusion
    DR32_EFX_SPACES,       // Airwindows Verbity2 — one flexible room-to-hall model
    DR32_EFX_DRUMBUSS,     // drum-bus glue: compress / crunch / attack / sustain
    DR32_EFX_COUNT
} dr32_efx_type;

typedef struct dr32_fxbus dr32_fxbus;

dr32_fxbus *dr32_fxbus_create(float sample_rate);
void        dr32_fxbus_destroy(dr32_fxbus *fx);

/** Send slot indices are 0..1. */
void dr32_fxbus_set_send_type(dr32_fxbus *fx, int slot, dr32_efx_type type);

/** Generic per-slot controls, all 0..1.
 *  Plate:     size / damping / decay / PRE-DELAY (0..200 ms)
 *  Spaces:    size / tone    / decay / PRE-DELAY (0..200 ms)
 *  Drum Bus: compress / crunch / attack / SUSTAIN
 *
 *  ⚠ The fourth slot is pre-delay for a reverb and Sustain for the Drum Bus.
 *  Both Attack and Sustain are bipolar about 0.5, so passing 0.0 for a Drum
 *  Buss is NOT neutral — it pulls the tail down about 8 dB.
 *
 *  A send bus is ALWAYS 100% wet: its return carries only the effect, and its
 *  level is set by the pad send amounts and the return gain. There is
 *  deliberately no wet/dry on a send. */
void dr32_fxbus_set_send_params(dr32_fxbus *fx, int slot,
                                float p1, float p2, float p3, float predelay);

/** Return level of a send bus into the master mix, linear. */
void dr32_fxbus_set_send_return(dr32_fxbus *fx, int slot, float gain);

/** Feed one stereo frame into a send bus at a specific FRAME within the block
 *  (accumulated across pads).
 *
 *  ⚠ The frame index is not optional. An earlier version tracked a write
 *  position internally and never advanced it, so every sample of every pad
 *  landed on frame 0 — the bus then saw one impulse per block, i.e. an impulse
 *  train at the block rate (~344 Hz at 128 frames), heard as a metallic ring
 *  on every send while inserts stayed clean. */
void dr32_fxbus_send(dr32_fxbus *fx, int slot, int frame, float l, float r);

/** Run the send buses over a rendered block, in place.
 *  `out` is interleaved stereo, `n` frames. Call once per block AFTER all pads
 *  have rendered and their send contributions have been accumulated. */
void dr32_fxbus_process(dr32_fxbus *fx, float *out, int n);

/** Drop all tails (kit change, panic). */
void dr32_fxbus_reset(dr32_fxbus *fx);

/** Musical starting point for a type: fills [size,damp,decay,predelay,mix].
 *  The 5th element (mix) is vestigial now that inserts are gone — a send is
 *  always 100% wet — but the array shape is kept so the per-type tables and
 *  their measured values stay untouched.
 *  Selecting an effect should give something usable immediately rather than
 *  whatever the previous effect's knobs happened to be. */
void dr32_efx_defaults(dr32_efx_type type, float *out5);

/** Name for a type, for UI readback. */
const char *dr32_efx_name(dr32_efx_type type);
dr32_efx_type dr32_efx_from_name(const char *name);

#ifdef __cplusplus
}
#endif
#endif
