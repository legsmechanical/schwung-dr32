// dr32_fxbus.h — DR32's effect buses: 2 sends + 2 kit inserts.
//
// The native Drum Rack has ONE send (a single return chain) and ONE rack-wide
// insert. DR32 doubles both, the same way it doubles the pads. That is why kit
// saves keep the extras in a sidecar: the song-model validator allows at most
// one return chain and REJECTS malformed documents, so writing a second one
// would likely make the kit unloadable on native Move.
//
// Signal flow per rendered block:
//
//     pads --(post-fader dB send amounts)--> send bus 1 -> FX --+
//        |                                 -> send bus 2 -> FX --+
//        |                                                       |
//        +------------------ dry ------------------------------> mix
//                                                                 |
//                                          insert 1 -> insert 2 -> out
//
// C API over C++ DSP: the vendored reverbs are C++ structs, the rest of DR32
// is C11. Implementation lives in dr32_fxbus.cpp.

#ifndef DR32_FXBUS_H
#define DR32_FXBUS_H

#ifdef __cplusplus
extern "C" {
#endif

/** Effect types available to a slot. Sends and inserts share one list; the UI
 *  offers the sensible subset per slot. 0 is always "off". */
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

/** Slot indices: sends are 0..1, inserts are 0..1 (separate spaces). */
void dr32_fxbus_set_send_type(dr32_fxbus *fx, int slot, dr32_efx_type type);
void dr32_fxbus_set_insert_type(dr32_fxbus *fx, int slot, dr32_efx_type type);

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
void dr32_fxbus_set_insert_params(dr32_fxbus *fx, int slot,
                                  float p1, float p2, float p3, float predelay, float mix);

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

/** Run the buses and inserts over a rendered block, in place.
 *  `out` is interleaved stereo, `n` frames. Call once per block AFTER all pads
 *  have rendered and their send contributions have been accumulated. */
void dr32_fxbus_process(dr32_fxbus *fx, float *out, int n);

/** Drop all tails (kit change, panic). */
void dr32_fxbus_reset(dr32_fxbus *fx);

/** Musical starting point for a type: fills [size,damp,decay,predelay,mix].
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
