#include "dr32_voice.h"

#include <math.h>
#include <string.h>

#define DR32_PI 3.14159265358979323846f
#define CHOKE_SECONDS 0.003f
// Native decays ~84 dB over the decay time, so a -80 dB floor would cut the
// tail early. Sit below the law's own endpoint.
#define ENV_FLOOR 1e-5f

void dr32_pad_defaults(dr32_pad *p) {
    memset(p, 0, sizeof(*p));
    p->play_start = 0.0f;
    p->play_length = 1.0f;
    p->gain = 1.0f;
    p->volume_db = 0.0f;
    p->vel_to_volume = 0.35f;
    p->env_mode = DR32_ENV_AHD;
    p->attack = DR32_ATTACK_MIN;
    p->hold = 0.3f;
    p->decay = 1.0f;
    p->filter_on = 1;
    p->filter_type = DR32_FILT_LP24;      // JSON "Lowpass"
    p->cutoff = DR32_CUTOFF_MAX;
    p->resonance = 0.0f;
    p->peak_gain = 1.0f;                  // 1.0 is flat (band term cancels)
    p->mod_target = DR32_MOD_FILTER;
    p->sending_note = 60;
    p->speaker_on = 1;
}

// ------------------------------------------------------------------ filter
//
// Reconstructed from FUN_01a77df8 (cutoff), FUN_01a36734 (resonance) and the
// kernels in drum-filter-kernels.c. See DRUM_FILTER_RECON.md.

/** The engine's rational approximation of tan(pi*f/fs) — NOT a call to tanf().
 *  Keeping the approximation (and its error) is part of matching the sound. */
float dr32_filter_g(float cutoff_hz) {
    float f = cutoff_hz;
    if (f < DR32_CUTOFF_MIN) f = DR32_CUTOFF_MIN;
    if (f > DR32_CUTOFF_MAX) f = DR32_CUTOFF_MAX;

    float omega = (2.0f * DR32_PI * f) / DR32_SR;
    if (omega > 3.1337388f) omega = 3.1337388f;
    float x = 0.5f * omega;
    float x2 = x * x;
    return x * (0.999999463558197f - 0.09652461111545563f * x2)
             / (1.0f - 0.4298672676086426f * x2 + 0.009981878101825714f * x2 * x2);
}

/** Resonance is distributed ACROSS the two cascade stages, not shared. */
void dr32_filter_k(float resonance, float *k1, float *k2) {
    float r = resonance;
    if (r < 0.0f) r = 0.0f;
    if (r > DR32_RESO_MAX) r = DR32_RESO_MAX;
    *k1 = 2.0f * (1.0f - r) / (1.0f + 3.0f * r);
    *k2 = 2.0f * (1.0f + r) / (1.0f + 3.0f * r);
}

typedef struct { float lp, bp, hp; } svf_out;

/** One TPT SVF stage. den = 1 + g*(g+k) — note the single k, not 2k. */
static inline svf_out svf_tick(dr32_svf *s, float x, float g, float k) {
    float den = 1.0f + g * (g + k);
    float hp = (x - (g + k) * s->s1 - s->s2) / den;
    float bp = s->s1 + g * hp;
    float lp = s->s2 + g * bp;
    s->s1 = 2.0f * bp - s->s1;
    s->s2 = 2.0f * lp - s->s2;
    svf_out o = { lp, bp, hp };
    return o;
}

static inline float run_filter(dr32_voice *v, dr32_svf *a, dr32_svf *b, float x) {
    switch (v->ftype) {
        case DR32_FILT_LP12:
            return svf_tick(a, x, v->g, v->k1).lp;
        case DR32_FILT_LP24: {
            float s1 = svf_tick(a, x, v->g, v->k1).lp;
            return svf_tick(b, s1, v->g, v->k2).lp;
        }
        case DR32_FILT_HP24: {
            float s1 = svf_tick(a, x, v->g, v->k1).hp;
            return svf_tick(b, s1, v->g, v->k2).hp;
        }
        case DR32_FILT_PEAK: {
            // Fixed damping; output is input plus a scaled band term, so
            // peak_gain == k (i.e. normalized 1.0) is exactly flat.
            const float k = 5.0f / 3.0f;
            svf_out o = svf_tick(a, x, v->g, k);
            return x + (v->peak_coef - k) * o.bp;
        }
    }
    return x;
}

// ------------------------------------------------------------------ laws

static inline float db_to_lin(float db) { return powf(10.0f, db / 20.0f); }

/** Velocity -> volume is a dB law centred on velocity 70, NOT a linear blend.
 *  gain = 10 ^ (((vel - 70) * amount * 0.4) / 20), and amount <= 0 is unity. */
float dr32_velocity_gain(int velocity, float amount) {
    if (amount <= 0.0f) return 1.0f;
    if (velocity < 0) velocity = 0;
    if (velocity > 127) velocity = 127;
    return db_to_lin(((float)velocity - 70.0f) * amount * 0.4f);
}

/** Equal-power pan over the SERIALIZED -50..+50 domain, scaled by sqrt(2) so
 *  centre is unity in both channels. Silent endpoint is floored to 1e-5. */
void dr32_pan_gains(float pan, float *l, float *r) {
    if (pan < -50.0f) pan = -50.0f;
    if (pan > 50.0f) pan = 50.0f;
    float theta = (DR32_PI / 4.0f) * (1.0f + pan / 50.0f);
    float sq2 = 1.41421356237f;
    float gl = sq2 * cosf(theta);
    float gr = sq2 * sinf(theta);
    if (gl < 1e-5f) gl = 1e-5f;
    if (gr < 1e-5f) gr = 1e-5f;
    *l = gl;
    *r = gr;
}

// ------------------------------------------------------------------ voice

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void dr32_voice_start(dr32_voice *v, const dr32_pad *p,
                      const float *sample, size_t frames, int channels,
                      int sample_rate, int velocity) {
    memset(v, 0, sizeof(*v));
    v->sample = sample;
    v->sample_frames = frames;
    v->channels = (channels == 2) ? 2 : 1;
    v->sample_rate = sample_rate > 0 ? sample_rate : (int)DR32_SR;
    v->note = p->sending_note;

    // Sample window: start is a fraction of N-1, length is clamped to what
    // remains, and the engine requires at least two frames for interpolation.
    double s = clampf(p->play_start, 0.0f, 1.0f);
    double l = clampf(p->play_length, 0.0f, 1.0f);
    size_t start = (frames > 1) ? (size_t)(s * (double)(frames - 1)) : 0;
    size_t avail = (frames > start) ? (frames - start) : 0;
    size_t len = (size_t)(l * (double)frames);
    if (len > avail) len = avail;
    v->region_start = start;
    v->region_end = start + len;
    v->pos = (double)start;

    // ratio = 2 ^ ((note - 60 + transpose + detune) / 12). The rack remaps each
    // pad's incoming note to its sending note, so the cell's "played note" is
    // the sending note (60 in factory kits).
    float semis = (float)p->sending_note - 60.0f + p->transpose + p->detune / 100.0f;
    // Ratio is pitch AND sample-rate derived: a 96 kHz source must advance
    // 96000/44100 frames per output frame just to play back at its own pitch.
    v->step = pow(2.0, (double)semis / 12.0) * ((double)v->sample_rate / (double)DR32_SR);

    v->amp = p->gain * db_to_lin(p->cell_volume_db) * db_to_lin(p->volume_db)
           * dr32_velocity_gain(velocity, p->vel_to_volume);
    if (!p->speaker_on) v->amp = 0.0f;

    dr32_pan_gains(p->pan, &v->panl, &v->panr);

    // Modulation applies to the targeted stage before the runtime clamps.
    float atk = p->attack, hold = p->hold, dec = p->decay, cutoff = p->cutoff;
    float vel01 = clampf((float)velocity / 127.0f, 0.0f, 1.0f);
    float mod = p->mod_amount * vel01;
    switch (p->mod_target) {
        case DR32_MOD_ATTACK: atk *= powf(4.0f, mod); break;
        case DR32_MOD_HOLD:   hold *= powf(4.0f, mod); break;
        case DR32_MOD_DECAY:  dec *= powf(4.0f, mod); break;
        case DR32_MOD_FILTER: cutoff *= powf(8.0f, mod); break;
        default: break;                                   // FX1/FX2: M2
    }

    if (p->pitch_to_env && v->step > 0.0) {
        float inv = (float)(1.0 / v->step);
        atk *= inv; hold *= inv; dec *= inv;
    }

    atk = clampf(atk, DR32_ATTACK_MIN, DR32_ATTACK_MAX);
    dec = clampf(dec, DR32_DECAY_MIN, DR32_DECAY_MAX);
    // Hold is clamped like the engine, but the "infinite" test uses the value
    // BEFORE clamping so a preset asking for >= 60 s still means "whole sample".
    v->hold_infinite = (hold >= DR32_HOLD_MAX);
    hold = clampf(hold, DR32_HOLD_MIN, DR32_HOLD_MAX);

    v->atk_rate = 1.0f / (atk * DR32_SR);
    // Exponential decay: a per-sample multiplier that loses
    // DR32_DECAY_DB_PER_TIME dB over `dec` seconds.
    v->dec_rate = powf(10.0f, -DR32_DECAY_DB_PER_TIME / (20.0f * dec * DR32_SR));
    v->hold_left = hold;
    v->stage = 0;
    v->env = 0.0f;
    v->gate = 1;
    v->env_mode = p->env_mode;

    // Effective-filter bypass: the engine picks a bypass kernel when the filter
    // is neutral — LP at the 22 kHz ceiling, HP at the 30 Hz floor — provided
    // resonance permits it. Peak has NO extreme-bypass test.
    cutoff = clampf(cutoff, DR32_CUTOFF_MIN, DR32_CUTOFF_MAX);
    int neutral = 0;
    if (p->resonance <= 0.0f) {
        if ((p->filter_type == DR32_FILT_LP12 || p->filter_type == DR32_FILT_LP24)
            && cutoff >= DR32_CUTOFF_MAX) neutral = 1;
        if (p->filter_type == DR32_FILT_HP24 && cutoff <= DR32_CUTOFF_MIN) neutral = 1;
    }
    v->filter_on = (p->filter_on && !neutral);
    v->ftype = p->filter_type;
    v->g = dr32_filter_g(cutoff);
    dr32_filter_k(p->resonance, &v->k1, &v->k2);
    v->peak_coef = p->peak_gain * (5.0f / 3.0f);

    v->active = (sample != NULL && v->region_end > v->region_start + 1);
}

void dr32_voice_release(dr32_voice *v, const dr32_pad *p) {
    v->gate = 0;
    if (p->env_mode == DR32_ENV_ASR && v->stage < 2) v->stage = 2;
}

void dr32_voice_choke(dr32_voice *v) {
    if (!v->active) return;
    v->stage = 2;
    // Same exponential form, forced to a fast time constant.
    float fast = powf(10.0f, -DR32_DECAY_DB_PER_TIME / (20.0f * CHOKE_SECONDS * DR32_SR));
    if (fast < v->dec_rate) v->dec_rate = fast;   // smaller multiplier = faster
}

/** LINEAR interpolation — the native kernel (FUN_01a3cfbc) interpolates
 *  linearly between adjacent frames. A "better" interpolator would be a
 *  fidelity bug: it changes the high-frequency character of every transposed
 *  pad relative to the real engine. */
static inline void read_frame(const dr32_voice *v, double pos, float *l, float *r) {
    size_t i = (size_t)pos;
    float t = (float)(pos - (double)i);
    size_t n = v->sample_frames;
    size_t i2 = (i + 1 < n) ? i + 1 : i;
    if (v->channels == 2) {
        const float *d = v->sample;
        float l0 = d[2 * i], l1 = d[2 * i2];
        float r0 = d[2 * i + 1], r1 = d[2 * i2 + 1];
        *l = l0 + t * (l1 - l0);
        *r = r0 + t * (r1 - r0);
    } else {
        float a = v->sample[i], b = v->sample[i2];
        *l = *r = a + t * (b - a);
    }
}

int dr32_voice_render(dr32_voice *v, float *out, int n) {
    if (!v->active) return 0;

    const float hold_step = 1.0f / DR32_SR;

    for (int i = 0; i < n; i++) {
        // The envelope is USED then ADVANCED: the native engine emits env[i] =
        // i * rate, so a note's very first sample is exactly zero. Advancing
        // first (env[i] = (i+1)*rate) shifts the whole attack a sample early —
        // measured against the stock render, whose onset ratios are 0.495,
        // 0.666, 0.747, 0.907 = i/(i+1) for the first four frames.
        float env_now = v->env;

        if (v->stage == 0) {
            v->env += v->atk_rate;
            if (v->env >= 1.0f) { v->env = 1.0f; v->stage = 1; }
        }
        if (v->stage == 1) {
            if (v->env_mode == DR32_ENV_AHD) {
                if (!v->hold_infinite) {
                    v->hold_left -= hold_step;
                    if (v->hold_left <= 0.0f) v->stage = 2;
                }
            } else if (!v->gate) {
                v->stage = 2;
            }
        }
        if (v->stage == 2) {
            v->env *= v->dec_rate;              // exponential, not linear
            if (v->env <= ENV_FLOOR) { v->env = 0.0f; v->stage = 3; v->active = 0; }
        }

        if (v->pos >= (double)v->region_end) { v->active = 0; break; }

        float l, r;
        read_frame(v, v->pos, &l, &r);
        v->pos += v->step;

        if (v->filter_on) {
            l = run_filter(v, &v->fL1, &v->fL2, l);
            r = run_filter(v, &v->fR1, &v->fR2, r);
        }

        float e = env_now * v->amp;
        out[2 * i]     += l * e * v->panl;
        out[2 * i + 1] += r * e * v->panr;
    }
    return v->active;
}
