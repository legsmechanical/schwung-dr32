#include "dr32_voice.h"

#include <math.h>
#include <string.h>

// M_PI is not in ISO C11; the cross toolchain builds with -std=c11 and would
// not see glibc's extension.
#define DR32_PI 3.14159265358979323846f

// A choke is a fast fade, not a hard stop — a hard stop clicks.
#define CHOKE_SECONDS 0.003f
// Envelope floor below which the voice is considered finished.
#define ENV_FLOOR 1e-4f

void dr32_pad_defaults(dr32_pad *p) {
    memset(p, 0, sizeof(*p));
    p->play_start = 0.0f;
    p->play_length = 1.0f;
    p->gain = 1.0f;
    p->volume_db = 0.0f;
    p->vel_to_volume = 0.35f;          // the corpus default
    p->env_mode = DR32_ENV_AHD;
    p->attack = 0.0001f;
    p->hold = 0.3f;
    p->decay = 1.0f;
    p->filter_on = 1;
    p->filter_type = DR32_FILT_LP24;
    p->cutoff = 22000.0f;
    p->resonance = 0.0f;
    p->peak_gain = 1.0f;
    p->mod_target = DR32_MOD_FILTER;
}

// ------------------------------------------------------------------ filter

static void svf_reset(dr32_svf *s) { s->ic1 = s->ic2 = 0.0f; }

/** TPT/ZDF state-variable filter coefficients. ZDF form chosen deliberately:
 *  the echidna ladder work showed a naive digital filter loses top end and
 *  misbehaves at high resonance, and this one stays stable up to Nyquist. */
static void filter_coeffs(dr32_voice *v, float cutoff, float reso, float peak_gain) {
    float fc = cutoff;
    if (fc < 20.0f) fc = 20.0f;
    if (fc > DR32_SR * 0.49f) fc = DR32_SR * 0.49f;

    v->g = tanf(DR32_PI * fc / DR32_SR);
    // resonance 0..1 -> damping. k = 2 is fully damped, k -> 0 self-oscillates.
    float r = reso;
    if (r < 0.0f) r = 0.0f;
    if (r > 0.98f) r = 0.98f;
    v->k = 2.0f - 1.98f * r;
    v->a_peak = peak_gain;
}

/** One 2-pole SVF section. `mode`: 0 = LP, 1 = HP, 2 = peak. */
static inline float svf_tick(dr32_svf *s, float x, float g, float k, float a_peak, int mode) {
    // Zavalishin's TPT SVF, one sample.
    float hp = (x - (2.0f * k + g) * s->ic1 - s->ic2) / (1.0f + g * (2.0f * k + g));
    float bp = g * hp + s->ic1;
    float lp = g * bp + s->ic2;
    s->ic1 = g * hp + bp;
    s->ic2 = g * bp + lp;
    if (mode == 0) return lp;
    if (mode == 1) return hp;
    return lp + a_peak * bp + hp;      // peaking: band gain around cutoff
}

static inline float run_filter(dr32_voice *v, float x) {
    switch (v->ftype) {
        case DR32_FILT_LP12: return svf_tick(&v->f1, x, v->g, v->k, v->a_peak, 0);
        case DR32_FILT_LP24: {
            float y = svf_tick(&v->f1, x, v->g, v->k, v->a_peak, 0);
            return svf_tick(&v->f2, y, v->g, v->k, v->a_peak, 0);
        }
        case DR32_FILT_HP24: {
            float y = svf_tick(&v->f1, x, v->g, v->k, v->a_peak, 1);
            return svf_tick(&v->f2, y, v->g, v->k, v->a_peak, 1);
        }
        case DR32_FILT_PEAK: return svf_tick(&v->f1, x, v->g, v->k, v->a_peak, 2);
    }
    return x;
}

// ------------------------------------------------------------------ voice

static inline float db_to_lin(float db) { return powf(10.0f, db / 20.0f); }

void dr32_voice_start(dr32_voice *v, const dr32_pad *p,
                      const float *sample, size_t frames, int velocity) {
    memset(v, 0, sizeof(*v));
    v->sample = sample;
    v->sample_frames = frames;

    // Playback region. PlaybackStart/Length are fractions of the whole sample.
    double s = p->play_start;
    if (s < 0.0) s = 0.0;
    if (s > 1.0) s = 1.0;
    double l = p->play_length;
    if (l < 0.0) l = 0.0;
    if (l > 1.0) l = 1.0;
    size_t start = (size_t)(s * (double)frames);
    size_t end = start + (size_t)(l * (double)frames);
    if (end > frames) end = frames;
    v->region_start = start;
    v->region_end = end;
    v->pos = (double)start;

    // Pitch. The sample is at unity when the cell is played at its sendingNote
    // (60); transpose/detune shift from there. The drum rack always feeds the
    // cell its sendingNote, so in practice transpose/detune ARE the pitch.
    float semis = p->transpose + p->detune / 100.0f;
    v->step = pow(2.0, semis / 12.0);

    // Velocity -> volume. vel_to_volume is the depth of velocity's control:
    // at 0 the pad is velocity-independent, at 1 it tracks velocity fully.
    float vel = (float)velocity / 127.0f;
    if (vel < 0.0f) vel = 0.0f;
    if (vel > 1.0f) vel = 1.0f;
    float vscale = 1.0f - p->vel_to_volume + p->vel_to_volume * vel;

    v->amp = p->gain * db_to_lin(p->volume_db) * vscale;

    // Pan. LINEAR law, not equal-power: centre must be unity in BOTH channels
    // (that's what Live/Move do — a centred mono source is not attenuated).
    // Equal-power would put every centred pad 3 dB below the native rack.
    // ⚠ The exact taper away from centre is assumed linear and unverified;
    // worth a device null-test when the FX phase adds a comparison rig.
    float pan = p->pan;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    v->panl = (pan > 0.0f) ? (1.0f - pan) : 1.0f;
    v->panr = (pan < 0.0f) ? (1.0f + pan) : 1.0f;

    // Envelope. Modulation applies to whichever stage the pad targets.
    float atk = p->attack, hold = p->hold, dec = p->decay, cutoff = p->cutoff;
    float mod = p->mod_amount * vel;                  // source: Velocity (Slide = M2/MPE)
    switch (p->mod_target) {
        case DR32_MOD_ATTACK: atk *= powf(4.0f, mod); break;
        case DR32_MOD_HOLD:   hold *= powf(4.0f, mod); break;
        case DR32_MOD_DECAY:  dec *= powf(4.0f, mod); break;
        case DR32_MOD_FILTER: cutoff *= powf(8.0f, mod); break;
        default: break;                                // FX1/FX2: M2
    }

    // "Envelope follows pitch": times scale with playback rate, so a transposed
    // pad keeps the same portion of the sample under the envelope.
    if (p->pitch_to_env && v->step > 0.0) {
        float inv = (float)(1.0 / v->step);
        atk *= inv; hold *= inv; dec *= inv;
    }

    if (atk < 1e-5f) atk = 1e-5f;
    if (dec < 1e-5f) dec = 1e-5f;
    v->atk_rate = 1.0f / (atk * DR32_SR);
    v->dec_rate = 1.0f / (dec * DR32_SR);
    v->hold_infinite = (hold >= DR32_HOLD_INFINITE);
    v->hold_left = hold;
    v->stage = 0;
    v->env = 0.0f;
    v->gate = 1;
    v->env_mode = p->env_mode;

    v->filter_on = p->filter_on;
    v->ftype = p->filter_type;
    filter_coeffs(v, cutoff, p->resonance, p->peak_gain);
    svf_reset(&v->f1);
    svf_reset(&v->f2);

    v->active = (sample != NULL && end > start);
}

void dr32_voice_release(dr32_voice *v, const dr32_pad *p) {
    v->gate = 0;
    // A-H-D ignores note-off entirely — that's what makes it "Trigger" mode.
    if (p->env_mode == DR32_ENV_ASR && v->stage < 2) v->stage = 2;
}

void dr32_voice_choke(dr32_voice *v) {
    if (!v->active) return;
    v->stage = 2;
    float fast = 1.0f / (CHOKE_SECONDS * DR32_SR);
    if (fast > v->dec_rate) v->dec_rate = fast;
}

/** Catmull-Rom interpolation — smoother than linear on transposed one-shots,
 *  which is where linear's brightness loss is audible. */
static inline float interp(const float *d, size_t n, double pos) {
    long i = (long)pos;
    float t = (float)(pos - (double)i);
    float x0 = (i > 0) ? d[i - 1] : d[0];
    float x1 = d[i];
    float x2 = ((size_t)(i + 1) < n) ? d[i + 1] : d[n - 1];
    float x3 = ((size_t)(i + 2) < n) ? d[i + 2] : d[n - 1];
    float a = -0.5f * x0 + 1.5f * x1 - 1.5f * x2 + 0.5f * x3;
    float b = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    float c = -0.5f * x0 + 0.5f * x2;
    return ((a * t + b) * t + c) * t + x1;
}

int dr32_voice_render(dr32_voice *v, float *out, int n) {
    if (!v->active) return 0;

    const float hold_step = 1.0f / DR32_SR;

    for (int i = 0; i < n; i++) {
        // --- envelope
        if (v->stage == 0) {
            v->env += v->atk_rate;
            if (v->env >= 1.0f) { v->env = 1.0f; v->stage = 1; }
        }
        if (v->stage == 1) {
            if (v->env_mode == DR32_ENV_AHD) {
                // Trigger: runs on its own schedule and IGNORES note-off. Hold
                // at 60 s ("inf") means play the whole sample.
                if (!v->hold_infinite) {
                    v->hold_left -= hold_step;
                    if (v->hold_left <= 0.0f) v->stage = 2;
                }
            } else if (!v->gate) {
                v->stage = 2;              // Gate: sustain until released
            }
        }
        if (v->stage == 2) {
            v->env -= v->dec_rate;
            if (v->env <= ENV_FLOOR) { v->env = 0.0f; v->stage = 3; v->active = 0; break; }
        }

        // --- sample read
        float s = 0.0f;
        if (v->pos < (double)v->region_end) {
            s = interp(v->sample, v->sample_frames, v->pos);
            v->pos += v->step;
        } else {
            // Ran off the end of the region: no more signal, so finish.
            v->active = 0;
            break;
        }

        if (v->filter_on) s = run_filter(v, s);
        s *= v->env * v->amp;

        out[2 * i]     += s * v->panl;
        out[2 * i + 1] += s * v->panr;
    }
    return v->active;
}
