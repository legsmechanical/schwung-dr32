// dr32_effects.h — the ten Drum Sampler player/effect paths.
//
// ⚠ Reconstructed from the native engine; see
// `move original reconstruct/analysis/native-instruments/DRUM_EFFECTS_RECON.md`.
// Every law here is either quoted from that document or measured against the
// null-test oracle. Do not substitute a "nicer" algorithm.
//
// The native engine preconstructs ten players and a 320-entry table of render
// callables (effect x envelope-mode x filter-active x filter-type x mono). We
// keep one voice and branch, which is the same arithmetic with a different
// dispatch strategy — the specialization exists for CPU, not for sound.

#ifndef DR32_EFFECTS_H
#define DR32_EFFECTS_H

#include <stddef.h>

/** Effect slot indices — the NATIVE ordering (DRUM_EFFECTS_RECON.md "effect
 *  bank map"), which is also the order the JSON's Effect_Type strings map to. */
typedef enum {
    DR32_FX_STANDARD = 0,
    DR32_FX_STRETCH  = 1,
    DR32_FX_LOOP     = 2,
    DR32_FX_PITCHENV = 3,
    DR32_FX_PUNCH    = 4,
    DR32_FX_EIGHTBIT = 5,
    DR32_FX_FM       = 6,
    DR32_FX_RINGMOD  = 7,
    DR32_FX_SUBOSC   = 8,
    DR32_FX_NOISE    = 9,
} dr32_fx_type;

/** Per-voice effect state. Reset at note start. */
typedef struct {
    dr32_fx_type type;
    float p1, p2;          // the slot's two exposed controls, model domain

    // Pitch Envelope
    float pitch_env;       // decays toward 0
    float pitch_decay_mul; // per-sample multiplier
    float pitch_depth;     // octaves

    // Ring Mod / FM shared oscillator
    float osc_phase;
    float osc_inc;
    float ring_depth, ring_trim;

    // Sub Osc / Noise gains (dB-mapped)
    float sub_gain, noise_gain;

    // Noise PRNG (xorshift family, per DRUM_EFFECTS_RECON)
    unsigned rng_a, rng_b;

    // Loop
    double loop_start, loop_end;

    // 8-bit
    float hold_phase, hold_step;   // sample/hold at the resampling rate
    float hold_l, hold_r;          // held sample
    float quant_step;              // quantizer step
    float lp_l, lp_r, lp_coeff;    // post filter
    int   eightbit_neutral;        // filter considered neutral

    // Punch
    float punch_gain;      // 1 + amount^3
    float punch_samples;   // time * sample_rate
    float punch_pos;       // samples since note start
    float punch_smoothed;  // the engine smooths the target gain
} dr32_fx;

/** Model-domain clamps, exactly as the engine's parameter combiner applies
 *  them (DRUM_EFFECTS_RECON.md "Exposed parameters and exact model ranges"). */
void dr32_fx_clamp(dr32_fx_type type, float *p1, float *p2);

/** Configure at note start. `base_step` is the pitch+rate playback step. */
void dr32_fx_start(dr32_fx *fx, dr32_fx_type type, float p1, float p2,
                   double base_step, float sample_rate);

/** Per-sample step modulation (Pitch Envelope, FM, Stretch). Returns the step
 *  to use for THIS sample; advances any per-sample effect state. */
double dr32_fx_step(dr32_fx *fx, double base_step);

/** Per-sample output stage (Ring Mod, Sub Osc, Noise, Punch, 8-bit).
 *  Operates in place on one stereo frame. */
void dr32_fx_output(dr32_fx *fx, float *l, float *r);

/** Loop wrap: called with the current read position, returns the position to
 *  use. Zero-length/disabled loops return `pos` unchanged. */
double dr32_fx_wrap(dr32_fx *fx, double pos);

/** Configure the Loop effect's frame bounds once the sample window is known. */
void dr32_fx_set_window(dr32_fx *fx, size_t region_start, size_t region_frames,
                        float sample_rate, float source_rate);

/** 1 when the reader must use NEAREST-frame addressing (floor(pos+0.5))
 *  instead of linear interpolation — the 8-bit path does. */
int dr32_fx_nearest(const dr32_fx *fx);

/** Is this effect's model good enough to USE?
 *
 *  Policy: an effect is enabled only once it measurably beats the fallback
 *  (playing the pad dry) in the null suite. Implementing from prose without a
 *  numeric target made three effects WORSE than not implementing them —
 *  8-bit went -29.7 dB to -0.0 dB, Punch -2.4 to +1.1, FM -6.0 to -2.1 — so a
 *  half-finished model is not a step forward, it is a regression the user
 *  would hear. Unmodelled effects fall back to the plain reader, which is the
 *  closest available approximation.
 *
 *  Enable an effect here only with a scoreboard number that justifies it. */
int dr32_fx_modelled(dr32_fx_type type);

/** Map a JSON `Effect_Type` string to the native slot index. */
dr32_fx_type dr32_fx_from_name(const char *name);

#endif
