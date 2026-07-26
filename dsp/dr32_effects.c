#include "dr32_effects.h"

#include <math.h>
#include <string.h>

#define DR32_PI 3.14159265358979323846f

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float db_to_lin(float db) { return powf(10.0f, db / 20.0f); }

dr32_fx_type dr32_fx_from_name(const char *name) {
    if (!name || !*name) return DR32_FX_STANDARD;
    // The JSON spellings, exactly as they appear in stock kits. Note the
    // lowercase b in "8-bit": "8-Bit" silently falls back in the stock model
    // (validation/drum-rack/README.md) and must not be emitted.
    if (!strcmp(name, "Stretch"))   return DR32_FX_STRETCH;
    if (!strcmp(name, "Loop"))      return DR32_FX_LOOP;
    if (!strcmp(name, "Pitch Env")) return DR32_FX_PITCHENV;
    if (!strcmp(name, "Punch"))     return DR32_FX_PUNCH;
    if (!strcmp(name, "8-bit"))     return DR32_FX_EIGHTBIT;
    if (!strcmp(name, "FM"))        return DR32_FX_FM;
    if (!strcmp(name, "Ring Mod"))  return DR32_FX_RINGMOD;
    if (!strcmp(name, "Sub Osc"))   return DR32_FX_SUBOSC;
    if (!strcmp(name, "Noise"))     return DR32_FX_NOISE;
    return DR32_FX_STANDARD;
}

void dr32_fx_clamp(dr32_fx_type type, float *p1, float *p2) {
    switch (type) {
        case DR32_FX_STRETCH:  *p1 = clampf(*p1, 1.0f, 20.0f);
                               *p2 = clampf(*p2, 0.005f, 0.3f); break;
        case DR32_FX_LOOP:     *p1 = clampf(*p1, 0.0f, 1.0f);
                               *p2 = clampf(*p2, 0.01f, 0.5f); break;
        case DR32_FX_PITCHENV: *p1 = clampf(*p1, -1.0f, 1.0f);
                               *p2 = clampf(*p2, 0.005f, 2.0f); break;
        case DR32_FX_PUNCH:    *p1 = clampf(*p1, 0.0f, 1.0f);
                               *p2 = clampf(*p2, 0.06f, 1.0f); break;
        case DR32_FX_EIGHTBIT: *p1 = clampf(*p1, 1000.0f, 30000.0f);
                               *p2 = clampf(*p2, 0.01f, 5.0f); break;
        case DR32_FX_FM:       *p1 = clampf(*p1, 0.0f, 1.0f);
                               *p2 = clampf(*p2, 10.0f, 4500.0f); break;
        case DR32_FX_RINGMOD:  *p1 = clampf(*p1, 0.0f, 1.0f);
                               *p2 = clampf(*p2, 1.0f, 5000.0f); break;
        case DR32_FX_SUBOSC:   *p1 = clampf(*p1, 0.0f, 1.0f);
                               *p2 = clampf(*p2, 30.0f, 120.0f); break;
        case DR32_FX_NOISE:    *p1 = clampf(*p1, 0.0f, 1.0f);
                               *p2 = clampf(*p2, 180.0f, 15000.0f); break;
        default: break;
    }
}

void dr32_fx_start(dr32_fx *fx, dr32_fx_type type, float p1, float p2,
                   double base_step, float sample_rate) {
    memset(fx, 0, sizeof(*fx));
    fx->type = type;
    dr32_fx_clamp(type, &p1, &p2);
    fx->p1 = p1;
    fx->p2 = p2;
    (void)base_step;

    switch (type) {
        case DR32_FX_PITCHENV: {
            // EXACT (DRUM_EFFECTS_RECON.md "Pitch Envelope"):
            //   pitch_depth_octaves = amount * 4
            //   samples   = max(sample_rate * decay_seconds, 1e-6)
            //   decay_mul = exp(-ln(1000) / samples)      // reaches 1/1000
            // Both constants CONFIRMED by oracle sweep (2026-07-25): depth 4.0
            // nulls at -39.3 dB while 3.8 or 4.2 null at ~0 dB, and ln(1000)
            // likewise. The null test is extremely sensitive to pitch, so these
            // are not merely plausible — they are pinned.
            fx->pitch_depth = p1 * 4.0f;
            float samples = sample_rate * p2;
            if (samples < 1e-6f) samples = 1e-6f;
            fx->pitch_decay_mul = expf(-logf(1000.0f) / samples);
            fx->pitch_env = 1.0f;
            break;
        }
        case DR32_FX_RINGMOD: {
            // EXACT: ring_depth = amount^2, output_trim = 1 + (amount*0.5)^2
            fx->ring_depth = p1 * p1;
            float half = p1 * 0.5f;
            fx->ring_trim = 1.0f + half * half;
            fx->osc_inc = p2 / sample_rate;
            break;
        }
        case DR32_FX_SUBOSC:
            // EXACT: sub_gain = db_to_linear(lerp(-71 dB, 0 dB, amount))
            fx->sub_gain = db_to_lin(-71.0f + 71.0f * p1);
            fx->osc_inc = p2 / sample_rate;
            break;
        case DR32_FX_NOISE: {
            // EXACT piecewise-dB amount curve.
            float db = (p1 <= 0.3f) ? (-70.0f + 40.0f * (p1 / 0.3f))
                                    : (-30.0f + 30.0f * ((p1 - 0.3f) / 0.7f));
            fx->noise_gain = db_to_lin(db);
            fx->rng_a = 0x9E3779B9u;      // deterministic seed; the engine's
            fx->rng_b = 0x243F6A88u;      // own seeding is not yet attributed
            break;
        }
        case DR32_FX_FM:
            fx->osc_inc = p2 / sample_rate;
            break;
        default:
            break;
    }
}

double dr32_fx_step(dr32_fx *fx, double base_step) {
    switch (fx->type) {
        case DR32_FX_PITCHENV: {
            // Decay FIRST, then convert to a ratio. Measured: use-then-advance
            // (the amplitude envelope's convention) nulls 9 dB worse here, so
            // the two envelopes genuinely differ in phase.
            fx->pitch_env *= fx->pitch_decay_mul;
            float ratio = exp2f(fx->pitch_env * fx->pitch_depth);
            return base_step * (double)ratio;
        }
        case DR32_FX_STRETCH:
            // Factor 1 is the plain reader (and is what stock kits overwhelmingly
            // use). Grain crossfading is not modelled yet — see the header.
            if (fx->p1 <= 1.0f) return base_step;
            return base_step / (double)fx->p1;
        default:
            return base_step;
    }
}

void dr32_fx_output(dr32_fx *fx, float *l, float *r) {
    switch (fx->type) {
        case DR32_FX_RINGMOD: {
            // Signed parabolic phase, the same family FM uses:
            //   ring = trim * (1 - osc^2 * depth * |parabolic|)
            fx->osc_phase += fx->osc_inc;
            if (fx->osc_phase >= 1.0f) fx->osc_phase -= 1.0f;
            float s = 0.5f - fx->osc_phase;
            float par = fabsf(s) * (0.5f - fabsf(s)) * 16.0f;
            float ring = fx->ring_trim * (1.0f - fx->ring_depth * fabsf(par));
            *l *= ring;
            *r *= ring;
            break;
        }
        case DR32_FX_SUBOSC: {
            fx->osc_phase += fx->osc_inc;
            if (fx->osc_phase >= 1.0f) fx->osc_phase -= 1.0f;
            float sub = sinf(2.0f * DR32_PI * fx->osc_phase) * fx->sub_gain;
            *l += sub;
            *r += sub;
            break;
        }
        case DR32_FX_NOISE: {
            // xorshift family; added equally to both channels.
            unsigned x = fx->rng_a, y = fx->rng_b;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            fx->rng_a = y;
            fx->rng_b = x;
            float n = ((float)(int)x / 2147483648.0f) * fx->noise_gain;
            *l += n;
            *r += n;
            break;
        }
        default:
            break;
    }
}
