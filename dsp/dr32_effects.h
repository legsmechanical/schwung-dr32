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

/** Map a JSON `Effect_Type` string to the native slot index. */
dr32_fx_type dr32_fx_from_name(const char *name);

#endif
