// gate_engine.c — two engines that lean on what DR32 does FOR a plugin
// (tests/test_plugins.c, as dist/tests/plug/zraw/dr32_engine.so):
//
//   raw   no `choke`, and `render` never says it has finished: DR32's own
//         fade and silence gate have to do both jobs.
//   late  silent for 250 ms, then a burst: it returns HOLD so the gate does
//         not take the silence for the end, and DONE when it is.
//
// Written with dr32_engine_kit.h, so the helper header is compiled and used
// by the suite too.
#include "../../../dsp/dr32_engine_kit.h"

#include <stdlib.h>

typedef struct { float sr, hz, env, phase, ratio; int t, late; } gv;

static const dr32x_param PARAMS[] = {
    DR32X_HZ  ("pitch", "Pitch", "PITCH", 40, 400, 110, "Tone"),
    DR32X_TIME("decay", "Decay", "DECAY", 40, "Tone"),
};

static void *create(int late, int sr) { gv *v = (gv *)calloc(1, sizeof(*v)); if (v) { v->sr = (float)sr; v->late = late; } return v; }
static void *create_raw(int sr)  { return create(0, sr); }
static void *create_late(int sr) { return create(1, sr); }
static void destroy(void *e) { free(e); }
static float g_decay_s = 0.05f;
static void set(void *e, int idx, float d) {
    gv *v = (gv *)e;
    if (idx == 0) v->hz = d;
    else g_decay_s = dr32x_exp(d, 0.01f, 1.0f);
}
static void note_on(void *e, float vel01, float tune_st) {
    gv *v = (gv *)e;
    v->env = vel01; v->phase = 0.0f; v->t = 0; v->ratio = dr32x_tune_ratio(tune_st);
}
static int render(void *e, float *out, int n) {
    gv *v = (gv *)e;
    float k = expf(-1.0f / (g_decay_s * v->sr));
    int start = v->late ? (int)(0.25f * v->sr) : 0;
    for (int i = 0; i < n; i++, v->t++) {
        if (v->t < start) { out[i] = 0.0f; continue; }
        out[i] = sinf(6.2831853f * v->phase) * v->env;
        v->env *= k;
        v->phase += v->hz * v->ratio / v->sr;
        if (v->phase >= 1.0f) v->phase -= 1.0f;
    }
    if (!v->late) return DR32X_RENDER_ALIVE;                     /* never DONE */
    return (v->t >= start && v->env < 1e-5f) ? DR32X_RENDER_DONE : DR32X_RENDER_HOLD;
}

static const dr32x_engine ENGINES[] = {
    { "raw",  "Raw",  2, PARAMS, create_raw,  destroy, set, note_on, NULL, render },
    { "late", "Late", 2, PARAMS, create_late, destroy, set, note_on, NULL, render },
};
static const float V[2] = { 220, 40 };
static const dr32x_model MODELS[] = {
    { "raw",  "Raw",  0, V, 0.0f, 0.0f },
    { "late", "Late", 1, V, 0.0f, 0.0f },
};
static const dr32x_plugin PLUGIN = { DR32X_API_VERSION, sizeof(dr32x_plugin), "zraw", "Z Raw", 2, ENGINES, 2, MODELS };

DR32X_EXPORT
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host) { (void)host; return &PLUGIN; }
