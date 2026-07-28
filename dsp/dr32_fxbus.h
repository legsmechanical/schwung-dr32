// dr32_fxbus.h — DR32's effect buses: 2 sends + one always-on Drum Bus.
//
// The native Drum Rack has ONE send (a single return chain); DR32 doubles it,
// the same way it doubles the pads.
//
// There are deliberately NO kit inserts. DR32 had two, but a Schwung chain slot
// already carries its own insert FX (fx1..fx4) in front of the output, so a
// kit-level insert was a second, worse copy of a facility the host provides —
// worse because it was reachable only from inside DR32 and had to be persisted
// by DR32. Put an insert on the slot instead (Josh, 2026-07-27).
//
// The Drum Bus is the ONE exception, and it is not user-selectable: it is a
// fixed stage at the end of the kit's own chain (Josh, 2026-07-28). It used to
// be a selectable send type, which never made sense — a send return is 100% wet,
// so "compress the reverb and nothing else" was the only thing you could ask it
// for. On the summed mix it does what a drum bus is for.
//
// Signal flow per rendered block:
//
//     pads --(post-fader dB send amounts)--> send bus 1 -> FX --+
//        |                                 -> send bus 2 -> FX --+
//        |                                                       |
//        +------------------ dry ------------------------------> sum
//                                                                 |
//                                              DRUM BUS (always on)
//                                                                 |
//                                       master gain (in dr32_kit) -> out
//
// So the send returns pass through the Drum Bus too — a tail is glued to the
// kit rather than sitting on top of it, which is the point of a bus.
//
// C API over C++ DSP: the vendored reverbs are C++ structs, the rest of DR32
// is C11. Implementation lives in dr32_fxbus.cpp.

#ifndef DR32_FXBUS_H
#define DR32_FXBUS_H

#ifdef __cplusplus
extern "C" {
#endif

/** Effect types available to a send. 0 is always "off".
 *
 *  There is deliberately no Drum Bus here any more: it is the fixed master
 *  stage below, not something a send can be set to. An older saved state naming
 *  it resolves to NONE through dr32_efx_from_name's unknown-name fallback. */
typedef enum {
    DR32_EFX_NONE = 0,
    DR32_EFX_PLATE,        // Dattorro figure-eight plate tank + input diffusion
    DR32_EFX_SPACES,       // Airwindows Verbity2 — one flexible room-to-hall model
    DR32_EFX_DELAY,        // tempo-synced stereo delay, filtered feedback
    DR32_EFX_COUNT
} dr32_efx_type;

typedef struct dr32_fxbus dr32_fxbus;

dr32_fxbus *dr32_fxbus_create(float sample_rate);
void        dr32_fxbus_destroy(dr32_fxbus *fx);

/** Send slot indices are 0..1. */
void dr32_fxbus_set_send_type(dr32_fxbus *fx, int slot, dr32_efx_type type);

/** How many generic control slots a send carries. Passed as an ARRAY rather
 *  than as p1..pN: this list has grown twice already, and every growth rewrote
 *  every call site and silently changed the meaning of a positional argument. */
#define DR32_SEND_PARAMS 8

/** Generic per-slot controls, `p[0..n-1]` (n may be short; the rest keep their
 *  current values).
 *
 *    idx  Plate / Spaces          Delay
 *    ---  ----------------------  ---------------------------------------
 *     0   size                    TIME L, in SIXTEENTHS (1..16)
 *     1   damping / tone          TIME R, in SIXTEENTHS (1..16)
 *     2   decay                   feedback
 *     3   PRE-DELAY (0..200 ms)   tone (feedback bandpass centre)
 *     4   —                       ping-pong
 *     5   —                       sync: 0 = free, 1 = tempo-synced
 *     6   —                       FREE TIME L, in MILLISECONDS
 *     7   —                       FREE TIME R, in MILLISECONDS
 *
 *  ⚠ Everything here is 0..1 EXCEPT the Delay's four times. The synced pair is
 *  a count of SIXTEENTH NOTES, the unit Move's own device syncs in
 *  (DelayLine_SyncedSixteenth); the free pair is milliseconds. Treating either
 *  as normalised gives a 1-sample delay, which reads as a comb filter rather
 *  than as a broken control.
 *
 *  ⚠ The synced and free times are stored SEPARATELY and both survive a flip of
 *  the sync flag — which is what the native device does (its Chicago Kit sits at
 *  SyncL=False while still carrying SyncedSixteenth 3/4).
 *
 *  A send bus is ALWAYS 100% wet: its return carries only the effect, and its
 *  level is set by the pad send amounts and the return gain. There is
 *  deliberately no wet/dry on a send. */
void dr32_fxbus_set_send_params(dr32_fxbus *fx, int slot, const float *p, int n);

/** Tempo, for the synced Delay. Cheap to call every block — the delay lines are
 *  only recomputed when the value actually changes. */
void dr32_fxbus_set_bpm(dr32_fxbus *fx, float bpm);

/** The always-on Drum Bus, over the summed mix.
 *
 *  compress / crunch / mix are 0..1. **attack and sustain are BIPOLAR, -1..+1,
 *  neutral at 0** — down softens/shortens, up sharpens/lengthens. They read as
 *  centred controls everywhere above the DrumBuss struct itself, which still
 *  works in 0..1 about 0.5 internally; the conversion happens here, at the one
 *  boundary, rather than leaving every caller to remember it.
 *
 *  `mix` is a dry/wet BLEND, i.e. parallel compression — 1.0 is fully processed.
 *  It was on the Drum Bus when it was a selectable insert and got dropped when
 *  the stage was lifted onto the master mix (spotted by Josh, 2026-07-28).
 *
 *  At neutral ({ 0, 0, 0, 0 }, any mix) the whole stage is bypassed and
 *  bit-transparent, which is what makes an always-on bus free for anyone who
 *  never opens the page. */
void dr32_fxbus_set_bus_params(dr32_fxbus *fx, float compress, float crunch,
                               float attack, float sustain, float mix);

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

/** Drop all tails and bus state (kit change, panic). */
void dr32_fxbus_reset(dr32_fxbus *fx);

/** Musical starting point for a type: fills all DR32_SEND_PARAMS slots.
 *  Selecting an effect should give something usable immediately rather than
 *  whatever the previous effect's knobs happened to be. */
void dr32_efx_defaults(dr32_efx_type type, float *out);

/** Name for a type, for UI readback. */
const char *dr32_efx_name(dr32_efx_type type);
dr32_efx_type dr32_efx_from_name(const char *name);

#ifdef __cplusplus
}
#endif
#endif
