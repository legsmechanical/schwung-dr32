// toy_engine.c — the smallest thing dr32_engine_api.h allows: a decaying
// sine. tests/run.sh builds it as <tmp>/toy/dr32_engine.so and
// tests/test_plugins.c loads it THROUGH DR32, the way the device would.
//
// TOY_BAD builds the same plugin with a ninth knob on one page, which DR32
// must refuse whole.
#include "../../../dsp/dr32_engine_api.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
    float sr, phase, env, hz, decay_ms, tone, tune;
    int   wave, active;
} toy;

enum { P_PITCH, P_DECAY, P_WAVE, P_TONE,
#ifdef TOY_BAD
       P_X1, P_X2, P_X3, P_X4, P_X5, P_X6, P_X7, P_X8,
#endif
       P_COUNT };

static const dr32x_param PARAMS[P_COUNT] = {
    { "pitch", "Pitch", "PITCH", 40, 400, 110, 1, "hz", "Tone", NULL },
    { "decay", "Decay", "DCY",   10, 2000, 200, 1, "ms", "Tone", NULL },
    { "wave",  "Wave",  "WAVE",  0, 1, 0, 1, NULL, "Tone", "Sine|Square" },
    { "tone",  "Tone",  "TONE",  0, 1, 0.5f, 0.01f, NULL, "Color", NULL },
#ifdef TOY_BAD
    { "x1", "X1", "X1", 0, 1, 0, 1, NULL, "Tone", NULL }, { "x2", "X2", "X2", 0, 1, 0, 1, NULL, "Tone", NULL },
    { "x3", "X3", "X3", 0, 1, 0, 1, NULL, "Tone", NULL }, { "x4", "X4", "X4", 0, 1, 0, 1, NULL, "Tone", NULL },
    { "x5", "X5", "X5", 0, 1, 0, 1, NULL, "Tone", NULL }, { "x6", "X6", "X6", 0, 1, 0, 1, NULL, "Tone", NULL },
    { "x7", "X7", "X7", 0, 1, 0, 1, NULL, "Tone", NULL }, { "x8", "X8", "X8", 0, 1, 0, 1, NULL, "Tone", NULL },
#endif
};

static void *toy_create(int sr) {
    toy *t = (toy *)calloc(1, sizeof(*t));
    if (t) t->sr = (float)sr;
    return t;
}
static void toy_destroy(void *e) { free(e); }
static void toy_set(void *e, int idx, float v) {
    toy *t = (toy *)e;
    if (idx == P_PITCH) t->hz = v;
    else if (idx == P_DECAY) t->decay_ms = v;
    else if (idx == P_WAVE) t->wave = (int)(v + 0.5f);
    else if (idx == P_TONE) t->tone = v;
}
static void toy_note_on(void *e, float vel01, float tune_st) {
    toy *t = (toy *)e;
    t->env = vel01;
    t->phase = 0.0f;
    t->tune = powf(2.0f, tune_st / 12.0f);
    t->active = 1;
}
static void toy_choke(void *e) { ((toy *)e)->env = 0.0f; }
static int toy_render(void *e, float *out, int n) {
    toy *t = (toy *)e;
    float k = expf(-1.0f / (0.001f * t->decay_ms * t->sr));
    float inc = t->hz * t->tune / t->sr;
    for (int i = 0; i < n; i++) {
        float s = sinf(6.2831853f * t->phase);
        if (t->wave) s = s >= 0.0f ? 1.0f : -1.0f;
        out[i] = s * t->env;
        t->env *= k;
        t->phase += inc;
        if (t->phase >= 1.0f) t->phase -= 1.0f;
    }
    if (t->env < 1e-5f) t->active = 0;
    return t->active;
}

static const dr32x_engine ENGINES[] = {
    { "sine", "Toy Sine", P_COUNT, PARAMS, toy_create, toy_destroy, toy_set, toy_note_on, toy_choke, toy_render },
};
static const float LOW[P_COUNT]  = { 60, 400, 0, 0.5f };
static const float HIGH[P_COUNT] = { 330, 80, 1, 0.25f };
static const dr32x_model MODELS[] = {
    { "low",  "Toy Low",  0, LOW,  -6.0f, 0.0f },
    { "high", "Toy High", 0, HIGH, -9.0f, 20.0f },
};
static const dr32x_plugin PLUGIN = {
    DR32X_API_VERSION, sizeof(dr32x_plugin), "toy", "Toy", 1, ENGINES, 2, MODELS,
};

__attribute__((visibility("default")))
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host) {
    return host && host->module_dir ? &PLUGIN : NULL;
}
