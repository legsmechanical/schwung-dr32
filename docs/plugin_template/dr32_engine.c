/* dr32_engine.c — a starting point for a DR32 engine plugin.
 *
 * SPDX-License-Identifier: MIT-0
 * This template is yours to copy, change and ship under any licence, with no
 * notice required.
 *
 * Copy this file, dr32_engine_api.h and dr32_engine_kit.h into your module,
 * replace the voice with your own, and build it as dr32_engine.so (see
 * Makefile.snippet beside this file, and docs/ENGINE_PLUGINS.md in DR32).
 *
 * As it stands it is a working plugin: one engine, a pitched drum with a
 * pitch sweep and a noise layer, on two pages, with three models. DR32's test
 * suite builds it and runs the checker over it, so it cannot go stale.
 *
 * The parts to change are marked CHANGE.
 */
#include "dr32_engine_kit.h"

#include <stdlib.h>

/* ---- CHANGE: your voice ------------------------------------------------- */
typedef struct {
    float sr;
    /* what the knobs set */
    float hz, decay_s, sweep_st, sweep_s, noise, noise_s, drive;
    int   wave;
    /* what a hit sets going */
    float phase, amp, sweep, namp, vel, ratio;
    unsigned rng;
} voice;

/* ---- CHANGE: the knobs --------------------------------------------------
 * One row per knob, in the order they appear. `page` groups them: at most 8
 * to a page, 32 to an engine. The enum gives each row its index, which is
 * what `set` receives. */
enum { P_PITCH, P_DECAY, P_SWEEP, P_SWEEP_T, P_WAVE, P_DRIVE, P_NOISE, P_NOISE_T, P_COUNT };

static const dr32x_param PARAMS[P_COUNT] = {
    [P_PITCH]   = DR32X_HZ   ("pitch",   "Pitch",       "PITCH", 30, 400, 55, "Tone"),
    [P_DECAY]   = DR32X_TIME ("decay",   "Decay",       "DECAY", 60, "Tone"),
    [P_SWEEP]   = DR32X_KNOB ("sweep",   "Sweep",       "SWEEP", 0, 48, 24, 1, "st", "Tone"),
    [P_SWEEP_T] = DR32X_TIME ("sweep_t", "Sweep Time",  "SWP T", 35, "Tone"),
    [P_WAVE]    = DR32X_ENUM ("wave",    "Wave",        "WAVE",  0, "Tone", "Sine|Triangle", 2),
    [P_DRIVE]   = DR32X_PCT  ("drive",   "Drive",       "DRIVE", 20, "Tone"),
    [P_NOISE]   = DR32X_PCT  ("noise",   "Noise",       "NOISE", 0, "Noise"),
    [P_NOISE_T] = DR32X_TIME ("noise_t", "Noise Decay", "N DEC", 40, "Noise"),
};

static void *eng_create(int sample_rate) {
    voice *v = (voice *)calloc(1, sizeof(*v));      /* may allocate: not the audio thread */
    if (!v) return NULL;
    v->sr = (float)sample_rate;
    v->rng = 0x2545F491u;
    return v;
}

static void eng_destroy(void *e) { free(e); }

/* A knob moved, or a model is being loaded (then this is called once per
 * knob). `display` is in the units of the table above. Audio thread. */
static void eng_set(void *e, int idx, float display) {
    voice *v = (voice *)e;
    switch (idx) {
        case P_PITCH:   v->hz = display; break;
        case P_DECAY:   v->decay_s = dr32x_exp(display, 0.01f, 4.0f); break;
        case P_SWEEP:   v->sweep_st = display; break;
        case P_SWEEP_T: v->sweep_s = dr32x_exp(display, 0.002f, 0.5f); break;
        case P_WAVE:    v->wave = dr32x_index(display); break;
        case P_DRIVE:   v->drive = dr32x_unit(display); break;
        case P_NOISE:   v->noise = dr32x_unit(display); break;
        case P_NOISE_T: v->noise_s = dr32x_exp(display, 0.005f, 1.0f); break;
    }
}

/* A hit. Restart everything: the same instance is retriggered, there is no
 * second voice. Velocity and pitch are yours to apply. */
static void eng_note_on(void *e, float vel01, float tune_st) {
    voice *v = (voice *)e;
    v->vel = vel01;
    v->ratio = dr32x_tune_ratio(tune_st);
    v->phase = 0.0f;
    v->amp = 1.0f;
    v->sweep = 1.0f;
    v->namp = 1.0f;
}

/* n mono frames. No need to detect silence: return ALIVE and DR32 stops
 * calling once the output has stayed under -80 dB for 100 ms. */
static int eng_render(void *e, float *out, int n) {
    voice *v = (voice *)e;
    float k_amp = expf(-1.0f / (v->decay_s * v->sr));
    float k_swp = expf(-1.0f / (v->sweep_s * v->sr));
    float k_nse = expf(-1.0f / (v->noise_s * v->sr));
    float gain = 1.0f + v->drive * 6.0f;
    for (int i = 0; i < n; i++) {
        float hz = v->hz * v->ratio * dr32x_tune_ratio(v->sweep_st * v->sweep);
        v->phase += hz / v->sr;
        if (v->phase >= 1.0f) v->phase -= 1.0f;
        float osc = v->wave ? 4.0f * fabsf(v->phase - 0.5f) - 1.0f : sinf(6.2831853f * v->phase);
        v->rng = v->rng * 1664525u + 1013904223u;
        float nse = ((float)(v->rng >> 8) / 8388608.0f - 1.0f) * v->noise * v->namp;
        out[i] = tanhf((osc * v->amp + nse) * gain) / tanhf(gain) * v->vel * 0.8f;
        v->amp *= k_amp;
        v->sweep *= k_swp;
        v->namp *= k_nse;
    }
    return DR32X_RENDER_ALIVE;
}

/* ---- CHANGE: what you offer --------------------------------------------- */
static const dr32x_engine ENGINES[] = {
    /* slug    name       params            create      destroy      set      note_on      choke render */
    { "drum", "My Drum", P_COUNT, PARAMS, eng_create, eng_destroy, eng_set, eng_note_on, NULL, eng_render },
};

/* A model is a named set of knob values: one per row of the engine's table,
 * in display units, inside each row's range. */
static const float KICK[P_COUNT] = { 55,  60, 24, 35, 0, 20,  0, 40 };
static const float TOM[P_COUNT]  = { 110, 52, 12, 45, 0, 10,  0, 40 };
static const float ZAP[P_COUNT]  = { 220, 30, 48, 55, 1, 40, 25, 30 };
static const dr32x_model MODELS[] = {
    /* slug    name   engine values volume_db pan */
    { "kick", "Kick", 0,     KICK,  0.0f,     0.0f },
    { "tom",  "Tom",  0,     TOM,   0.0f,     0.0f },
    { "zap",  "Zap",  0,     ZAP,  -3.0f,     0.0f },
};

static const dr32x_plugin PLUGIN = {
    DR32X_API_VERSION, sizeof(dr32x_plugin),
    "mysynth",      /* CHANGE: your id, [a-z0-9], permanent */
    "My Synth",     /* CHANGE: the picker section's name    */
    sizeof(ENGINES) / sizeof(ENGINES[0]), ENGINES,
    sizeof(MODELS) / sizeof(MODELS[0]), MODELS,
};

DR32X_EXPORT
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host) {
    /* Refuse only a DR32 older than this was built for; a newer one still
     * reads this version of the contract. */
    if (!host || host->api_version < DR32X_API_VERSION) return NULL;
    return &PLUGIN;
}
