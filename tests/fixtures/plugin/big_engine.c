// big_engine.c — twelve engines of thirty-two knobs each: more pages than one
// kit can hold in the host's 128 KB value channel. tests/test_plugins.c puts
// them on pads one by one to find where DR32 refuses the next
// (dist/tests/plug/zzbig/dr32_engine.so; the name sorts it last, so the other
// fixtures keep their ids).
#include "../../../dsp/dr32_engine_kit.h"

#include <stdio.h>
#include <stdlib.h>

#define N_ENG 12
#define N_PAR DR32X_MAX_PARAMS

typedef struct { float env; } bv;
static void *create(int sr) { (void)sr; return calloc(1, sizeof(bv)); }
static void destroy(void *e) { free(e); }
static void set(void *e, int i, float v) { (void)e; (void)i; (void)v; }
static void note_on(void *e, float vel, float tune) { (void)tune; ((bv *)e)->env = vel; }
static int render(void *e, float *out, int n) {
    bv *v = (bv *)e;
    for (int i = 0; i < n; i++) { out[i] = v->env * ((i & 32) ? 0.5f : -0.5f); v->env *= 0.999f; }
    return DR32X_RENDER_ALIVE;
}

static char         g_text[N_PAR][3][16];       /* key, name, short: shared by every engine */
static dr32x_param  g_params[N_PAR];
static char         g_slug[N_ENG][8], g_name[N_ENG][16];
static dr32x_engine g_engines[N_ENG];
static dr32x_model  g_models[N_ENG];
static float        g_values[N_PAR];
static dr32x_plugin g_plugin;

DR32X_EXPORT
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host) {
    (void)host;
    static const char *const PAGES[4] = { "Alpha", "Beta", "Gamma", "Delta" };
    for (int i = 0; i < N_PAR; i++) {
        snprintf(g_text[i][0], 16, "knob_number_%02d", i);
        snprintf(g_text[i][1], 16, "Knob Number %02d", i);
        snprintf(g_text[i][2], 16, "KN%02d", i);
        g_params[i] = (dr32x_param)DR32X_PCT(g_text[i][0], g_text[i][1], g_text[i][2], 50, PAGES[i / 8]);
        g_values[i] = 50;
    }
    for (int e = 0; e < N_ENG; e++) {
        snprintf(g_slug[e], sizeof(g_slug[e]), "e%d", e);
        snprintf(g_name[e], sizeof(g_name[e]), "Big %d", e);
        g_engines[e] = (dr32x_engine){ g_slug[e], g_name[e], N_PAR, g_params, create, destroy, set, note_on, NULL, render };
        g_models[e] = (dr32x_model){ g_slug[e], g_name[e], e, g_values, 0.0f, 0.0f };
    }
    g_plugin = (dr32x_plugin){ DR32X_API_VERSION, sizeof(dr32x_plugin), "zzbig", "Big", N_ENG, g_engines, N_ENG, g_models };
    return &g_plugin;
}
