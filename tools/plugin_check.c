// plugin_check.c — check a DR32 engine plugin on your own machine.
//
//   cc -O2 -Idsp -o dr32-plugin-check tools/plugin_check.c dsp/dr32_plugin_validate.c -lm -ldl
//   ./dr32-plugin-check path/to/dr32_engine.so
//
// Build your plugin NATIVELY for this (not for the Move) and point this at it.
// It loads the plugin the way DR32 does, applies DR32's own rules (the same
// code: dsp/dr32_plugin_validate.c), then plays it:
//
//   - the pages DR32 will build, and the keys a saved kit will hold
//   - every model: does it sound, how loud, how long, does it end
//   - every knob: does turning it change anything
//   - transpose, velocity, choke and retrigger
//
// Exit status 0 = DR32 would load it and nothing is wrong; 1 = an ERROR.
// A WARN is something to look at, not a refusal.
//
// It cannot tell you how the plugin SOUNDS, what it costs on the Move's CPU,
// or whether its id collides with another plugin installed there.

#define _GNU_SOURCE

#include "dr32_engine_api.h"
#include "dr32_plugin_validate.h"

#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR          44100
#define BLOCK       128
#define GATE        1.0e-4f              /* DR32's gate: -80 dB ...        */
#define GATE_FRAMES (SR / 10)            /* ... for 100 ms                 */
#define MAX_SECONDS 20                   /* DR32's Resample gives up here  */

static int errors, warnings;
static void error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf("  ERROR  "); vprintf(fmt, ap); printf("\n");
    va_end(ap); errors++;
}
static void warn(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf("  WARN   "); vprintf(fmt, ap); printf("\n");
    va_end(ap); warnings++;
}

/* One hit, rendered as DR32 would: until the engine says DONE or the gate
 * closes. What came out, summarised. */
typedef struct {
    float    peak;
    int      frames;        /* until it ended                          */
    int      ended;         /* 1 = DONE or gated; 0 = ran to MAX_SECONDS */
    int      held;          /* it returned HOLD at some point            */
    int      bad;           /* a NaN or an infinity                      */
    int      crossings;     /* zero crossings in the first 200 ms        */
    uint64_t hash;          /* of the first second                       */
    double   us_per_block;  /* this machine, while sounding              */
} hit;

static hit play(const dr32x_engine *e, void *inst, float vel, float tune, int choke_after) {
    hit h = { 0 };
    h.hash = 1469598103934665603ull;
    float out[BLOCK], prev = 0.0f;
    int quiet = 0;
    struct timespec t0, t1;
    e->note_on(inst, vel, tune);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int blocks = 0;
    for (; h.frames < MAX_SECONDS * SR; h.frames += BLOCK) {
        if (choke_after >= 0 && h.frames >= choke_after && e->choke) { e->choke(inst); choke_after = -1; }
        int r = e->render(inst, out, BLOCK);
        blocks++;
        float bp = 0.0f;
        for (int i = 0; i < BLOCK; i++) {
            float s = out[i];
            if (!isfinite(s)) { h.bad = 1; s = 0.0f; }
            float a = fabsf(s);
            if (a > bp) bp = a;
            if (h.frames + i < SR / 5 && (prev < 0.0f) != (s < 0.0f)) h.crossings++;
            if (h.frames + i < SR) { uint32_t u; memcpy(&u, &s, 4); h.hash = (h.hash ^ u) * 1099511628211ull; }
            prev = s;
        }
        if (bp > h.peak) h.peak = bp;
        if (r == DR32X_RENDER_HOLD) h.held = 1;
        if (r == DR32X_RENDER_DONE) { h.ended = 1; h.frames += BLOCK; break; }
        if (r == DR32X_RENDER_ALIVE) {
            quiet = bp < GATE ? quiet + BLOCK : 0;
            if (quiet >= GATE_FRAMES) { h.ended = 1; h.frames += BLOCK; break; }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    h.us_per_block = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / (blocks ? blocks : 1);
    return h;
}

/* A fresh voice holding a model's values. */
static void *voice_for(const dr32x_engine *e, const float *values) {
    void *inst = e->create(SR);
    for (int i = 0; inst && i < e->nparams; i++) e->set(inst, i, values[i]);
    return inst;
}

static double db(float peak) { return 20.0 * log10((double)peak + 1e-12); }

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s path/to/dr32_engine.so   (a NATIVE build, for this machine)\n", argv[0]);
        return 2;
    }
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("  ERROR  did not load: %s\n", dlerror()); return 1; }
    const dr32x_plugin *(*entry)(const dr32x_host *) =
        (const dr32x_plugin *(*)(const dr32x_host *))dlsym(h, DR32X_ENTRY);
    if (!entry) { printf("  ERROR  exports no " DR32X_ENTRY " (is it marked visible?)\n"); return 1; }

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", argv[1]);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0'; else snprintf(dir, sizeof(dir), ".");
    dr32x_host host = { DR32X_API_VERSION, SR, dir };
    const dr32x_plugin *pl = entry(&host);
    if (!pl) { printf("  ERROR  " DR32X_ENTRY " returned NULL: it offers nothing\n"); return 1; }

    const char *why = dr32_plugin_validate(pl);
    if (why) { printf("  ERROR  DR32 would refuse this plugin: %s\n", why); return 1; }

    printf("%s (\"%s\"): %d engine%s, %d model%s\n", pl->id, pl->name,
           pl->nengines, pl->nengines == 1 ? "" : "s", pl->nmodels, pl->nmodels == 1 ? "" : "s");

    /* ---- the pages, as DR32 will lay them out ---------------------------- */
    int total_pages = 0;
    for (int e = 0; e < pl->nengines; e++) {
        const dr32x_engine *x = &pl->engines[e];
        printf("\nengine %s (\"%s\"), %d knobs%s\n", x->slug, x->name, x->nparams,
               x->choke ? "" : ", no choke (DR32 fades it)");
        for (int i = 0; i < x->nparams; i++) {
            int first = 1;
            for (int j = 0; j < i; j++) if (!strcmp(x->params[j].page, x->params[i].page)) first = 0;
            if (!first) continue;
            total_pages++;
            printf("  page %-10s", x->params[i].page);
            for (int j = i; j < x->nparams; j++) {
                const dr32x_param *p = &x->params[j];
                if (strcmp(p->page, x->params[i].page)) continue;
                if (p->options) printf("  %s[%d]", p->short_name, dr32x_option_count(p->options));
                else printf("  %s[%g..%g%s]", p->short_name, (double)p->min, (double)p->max, p->unit ? p->unit : "");
            }
            printf("\n");
        }
        printf("  saved as  x_%s_%s_<key>, model slugs %s/<model>\n", pl->id, x->slug, pl->id);
        int used = 0;
        for (int m = 0; m < pl->nmodels; m++) if (pl->models[m].engine == e) used++;
        if (!used) warn("engine %s has no model: nothing in the picker reaches it", x->slug);
    }
    printf("\n%d pages in all. A kit has room for about 30 plugin pages across the engines on its pads.\n", total_pages);

    /* ---- every model, played --------------------------------------------- */
    printf("\nmodels (velocity 100, no transpose):\n");
    float loudest = 0.0f, quietest = 1e9f;
    for (int m = 0; m < pl->nmodels; m++) {
        const dr32x_model *md = &pl->models[m];
        const dr32x_engine *x = &pl->engines[md->engine];
        void *inst = voice_for(x, md->values);
        if (!inst) { error("model %s: create returned NULL", md->slug); continue; }
        hit a = play(x, inst, 100.0f / 127.0f, 0.0f, -1);
        printf("  %-24s %-8s peak %6.1f dBFS  %5d ms  %5.1f us/block%s\n", md->slug, x->slug, db(a.peak),
               (int)(a.frames * 1000.0 / SR), a.us_per_block, a.held ? "  (HOLD)" : "");
        if (a.bad) error("model %s: output has a NaN or an infinity", md->slug);
        if (a.peak <= 0.0f) error("model %s: makes no sound", md->slug);
        else if (a.peak < 0.01f) warn("model %s: very quiet (%.1f dBFS)", md->slug, db(a.peak));
        if (a.peak > 1.0f) warn("model %s: peaks at %.1f dBFS, over full scale: it will clip at pad Volume 0 dB", md->slug, db(a.peak));
        if (!a.ended && a.held)
            error("model %s: returns HOLD and never DONE: DR32 would render it forever", md->slug);
        else if (!a.ended)
            warn("model %s: still above -80 dB after %d s (a drone? DR32's Resample stops there)", md->slug, MAX_SECONDS);
        if (a.peak > 0.0f) { if (a.peak > loudest) loudest = a.peak; if (a.peak < quietest) quietest = a.peak; }

        /* A second hit on the same voice, after the first has ended and again
         * while it is sounding: neither may break it. */
        hit b = play(x, inst, 100.0f / 127.0f, 0.0f, -1);
        if (b.bad || b.peak <= 0.0f) error("model %s: a second hit on the same voice %s", md->slug, b.bad ? "is not finite" : "is silent");
        float out[BLOCK];
        x->note_on(inst, 1.0f, 0.0f);
        x->render(inst, out, BLOCK);
        x->note_on(inst, 1.0f, 0.0f);
        int ok = 1;
        for (int k = 0; k < 8; k++) { x->render(inst, out, BLOCK); for (int i = 0; i < BLOCK; i++) if (!isfinite(out[i])) ok = 0; }
        if (!ok) error("model %s: a retrigger while sounding gives output that is not finite", md->slug);
        x->destroy(inst);
    }
    if (loudest > 0.0f && db(loudest) - db(quietest) > 12.0)
        warn("models differ by %.0f dB in peak level: use volume_db to bring them together", db(loudest) - db(quietest));

    /* ---- every engine: knobs, transpose, velocity, choke ----------------- */
    for (int e = 0; e < pl->nengines; e++) {
        const dr32x_engine *x = &pl->engines[e];
        /* Up to three of its models: a knob that does nothing on one (a noise
         * decay with the noise at 0) is only worth a warning if it does nothing
         * on all of them. */
        const dr32x_model *tried[3];
        int ntried = 0;
        for (int m = 0; m < pl->nmodels && ntried < 3; m++) if (pl->models[m].engine == e) tried[ntried++] = &pl->models[m];
        if (!ntried) continue;
        const dr32x_model *md = tried[0];
        printf("\nengine %s, tried on model %s%s:\n", x->slug, md->slug, ntried > 1 ? " and others" : "");

        /* Each knob at its two ends, everything else as the model has it. */
        float vals[DR32X_MAX_PARAMS];
        int dead = 0;
        for (int i = 0; i < x->nparams; i++) {
            int moved = 0;
            for (int t = 0; t < ntried && !moved; t++) {
                uint64_t ends[2];
                for (int end = 0; end < 2; end++) {
                    memcpy(vals, tried[t]->values, sizeof(float) * (size_t)x->nparams);
                    vals[i] = end ? x->params[i].max : x->params[i].min;
                    void *inst = voice_for(x, vals);
                    if (!inst) { ends[end] = (uint64_t)end; continue; }
                    hit r = play(x, inst, 100.0f / 127.0f, 0.0f, -1);
                    if (r.bad) error("knob %s at its %s gives output that is not finite", x->params[i].key, end ? "max" : "min");
                    ends[end] = r.hash;
                    x->destroy(inst);
                }
                moved = ends[0] != ends[1];
            }
            if (!moved) {
                warn("knob %s (\"%s\"): min and max sound identical on %s", x->params[i].key, x->params[i].name,
                     ntried > 1 ? "every model tried" : "this model");
                dead++;
            }
        }
        if (!dead) printf("  every knob changes the sound\n");

        void *inst = voice_for(x, md->values);
        if (!inst) continue;
        hit base = play(x, inst, 100.0f / 127.0f, 0.0f, -1);
        hit up   = play(x, inst, 100.0f / 127.0f, 12.0f, -1);
        if (base.crossings > 4) {
            double ratio = (double)up.crossings / base.crossings;
            printf("  transpose +12 st: zero crossings x%.2f\n", ratio);
            if (ratio < 1.1) warn("transpose does not seem to move the pitch (fine for a noise voice; otherwise use tune_st)");
        }
        hit soft = play(x, inst, 30.0f / 127.0f, 0.0f, -1);
        printf("  velocity 30 vs 100: %.1f dB\n", db(soft.peak) - db(base.peak));
        if (fabs(db(soft.peak) - db(base.peak)) < 0.5)
            warn("velocity does not change the level: DR32 leaves velocity to the engine (vel01)");
        if (x->choke) {
            hit ch = play(x, inst, 1.0f, 0.0f, SR / 20);     /* choked 50 ms in */
            int ms = (int)((ch.frames - SR / 20) * 1000.0 / SR);
            printf("  choke: silent %d ms after it\n", ms);
            if (!ch.ended || ms > 600) warn("choke does not end the voice: it keeps costing CPU, unheard");
        }
        x->destroy(inst);
    }

    printf("\n%s: %d error%s, %d warning%s\n", errors ? "FAILED" : "OK",
           errors, errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
    return errors ? 1 : 0;
}
