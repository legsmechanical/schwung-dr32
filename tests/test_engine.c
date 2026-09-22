// test_engine.c — the synth engines on their own, below the kit.
//
// What this pins, per MODEL (so every picker entry is exercised):
//   - it makes a sound, and that sound is a hit (loud early, quiet late)
//   - it is deterministic: two fresh instances render identical audio
//   - the forced trigger edge: a note_on and its render in ONE block sound
//   - a retrigger inside one block still sounds (the fast-roll case)
//   - the silence gate eventually stops the voice
// And per ENGINE: tune moves pitch the right way by the right amount, keys
// are unique and prefixed, every model fills every parameter.
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dr32_engine.h"

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SR 44100
#define LEN (SR * 2)

static float buf[LEN], buf2[LEN];

static void *make(const dr32_model *m) {
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    void *v = e->create(SR);
    for (int i = 0; i < e->nparams; i++) e->set(v, i, m->values[i]);
    return v;
}

/* Render in host-sized blocks, the way the kit does. */
static void render_all(const dr32_engine_ops *e, void *v, float *out, int n) {
    for (int at = 0; at < n; at += 128) {
        int m = n - at < 128 ? n - at : 128;
        e->render(v, out + at, m);
    }
}

static float peak(const float *x, int n) {
    float p = 0;
    for (int i = 0; i < n; i++) if (fabsf(x[i]) > p) p = fabsf(x[i]);
    return p;
}

/* Zero crossings with a little hysteresis: a pitch proxy good enough to tell
 * an octave from a unison on a tonal drum. */
static int crossings(const float *x, int n) {
    int c = 0, s = 0;
    for (int i = 0; i < n; i++) {
        int t = x[i] > 1e-3f ? 1 : (x[i] < -1e-3f ? -1 : 0);
        if (t && s && t != s) c++;
        if (t) s = t;
    }
    return c;
}

static void test_model(const dr32_model *m) {
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    CHECK(e != NULL, "%s: no engine", m->slug);
    if (!e) return;

    void *a = make(m);
    e->note_on(a, 100 / 127.0f, 0.0f);
    render_all(e, a, buf, LEN);
    float p = peak(buf, LEN);
    CHECK(p > 0.01f, "%s: silent (peak %g)", m->slug, p);
    CHECK(p < 8.0f, "%s: runaway (peak %g)", m->slug, p);
    /* A hit: the first 100 ms carries more than the last 100 ms. */
    float early = peak(buf, SR / 10), late = peak(buf + LEN - SR / 10, SR / 10);
    CHECK(early > late, "%s: not a hit (early %g, late %g)", m->slug, early, late);

    /* Deterministic across fresh instances. */
    void *b = make(m);
    e->note_on(b, 100 / 127.0f, 0.0f);
    render_all(e, b, buf2, LEN);
    CHECK(!memcmp(buf, buf2, sizeof(buf)), "%s: two instances differ", m->slug);

    /* The forced edge: note_on then ONE 128-frame block must already sound
     * (URCHIN's Lateness is a model value; every model here starts at 0). */
    void *c = make(m);
    e->note_on(c, 1.0f, 0.0f);
    /* ⭑ One exception, and it is the machine's: CW-78's guiro is a free-running
     * scrape oscillator whose first TOOTH lands one scrape period after the
     * trigger (77-125 Hz, so ~8-13 ms), exactly as in schwung-cw-78 — the lane
     * is silent until then by design. It must still be sounding within 15 ms. */
    const int first = !strcmp(m->slug, "cw78/guiro") ? SR * 15 / 1000 : 128;
    for (int at = 0; at < first; at += 128) e->render(c, buf2 + at, first - at < 128 ? first - at : 128);
    CHECK(peak(buf2, first) > 1e-4f, "%s: hit did not start in its own block", m->slug);

    /* A retrigger must be a NEW hit, measured against the same voice left
     * alone: two instances, one second in, one hit again. The Trigger zone is
     * still up from the first hit, so without the forced edge the second
     * write is no change at all and Faust sees nothing — the tail just goes
     * on. Two note_ons in one block (a roll faster than the host's block)
     * must still land. */
    void *d = make(m), *d2 = make(m);
    e->note_on(d, 1.0f, 0.0f);
    e->note_on(d2, 1.0f, 0.0f);
    render_all(e, d, buf2, SR);
    render_all(e, d2, buf2, SR);
    e->note_on(d, 1.0f, 0.0f);
    e->note_on(d, 1.0f, 0.0f);
    render_all(e, d, buf2, SR / 20);
    float again = peak(buf2, SR / 20);
    render_all(e, d2, buf2, SR / 20);
    float tail = peak(buf2, SR / 20);
    CHECK(again > 2.0f * tail + 1e-3f, "%s: retrigger did not hit (again %g, tail %g)",
          m->slug, again, tail);
    e->destroy(d2);

    /* The silence gate: eventually the voice stops. Two minutes, because some
     * models are MEANT to ring: ChowKick's Wonky Synth sits at 14.5% damping
     * and is still about -52 dBFS 25 s after a hit (its preset's sound). */
    int stopped = 0;
    for (int blk = 0; blk < (SR * 120) / 128 && !stopped; blk++)
        stopped = !e->render(a, buf2, 128);
    CHECK(stopped, "%s: never went quiet", m->slug);

    e->destroy(a); e->destroy(b); e->destroy(c); e->destroy(d);
}

/* +12 st must raise the pitch about an octave. Measured on the tonal part of
 * a tonal model, 60 ms in (past the click and the steepest bend). */
static void test_tune(const char *slug) {
    int mi = dr32_model_find(slug);
    CHECK(mi >= 0, "no model %s", slug);
    if (mi < 0) return;
    const dr32_model *m = dr32_model_at(mi);
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    int c[2];
    for (int k = 0; k < 2; k++) {
        void *v = make(m);
        /* SIMIAN's noise layer would swamp a zero-crossing count; take the
         * oscillator alone. */
        int ix = dr32_engine_param_index(e, "sm_noise");
        if (ix >= 0) e->set(v, ix, 0.0f);
        e->note_on(v, 1.0f, k ? 12.0f : 0.0f);
        render_all(e, v, buf, SR / 4);
        c[k] = crossings(buf + SR * 60 / 1000, SR / 10);
        e->destroy(v);
    }
    float ratio = c[0] ? (float)c[1] / (float)c[0] : 0;
    CHECK(ratio > 1.6f && ratio < 2.5f, "%s: +12 st moved pitch by x%.2f (%d -> %d crossings)",
          slug, ratio, c[0], c[1]);
}

int main(void) {
    printf("synth engines\n");
    /* 9W9's cymbals are WAVs under <module dir>/samples/9w9/; the source tree
     * is laid out the same way under src/. */
    dr32_engines_set_module_dir("src");
    dr32_engines_init(SR);
    dr32_engines_init(SR);      /* idempotent */

    int n = dr32_model_count();
    /* SIMIAN 10, URCHIN 9, then one per lane: 9W9 11, 6W6 8, 8W8 16, CW-78 14;
     * ChowKick's five factory presets; FM's nine. */
    CHECK(n == 87, "model count %d", n);

    /* Engines: keys prefixed, and a key is ONE knob wherever it appears (one
     * hierarchy holds them all, and a repeated key kills the host's metadata
     * load). Two engines may share a key only as lanes of one kit port —
     * same prefix, same family — and then only with identical metadata,
     * because the page shows it once for all of them. */
    const dr32_eparam *seen[512]; const dr32_engine_ops *seen_e[512]; int ns = 0;
    for (int id = 1; id < DR32_ENG_COUNT; id++) {
        const dr32_engine_ops *e = dr32_engine_get(id);
        CHECK(e && e->id == id, "engine %d missing or misnumbered", id);
        if (!e) continue;
        CHECK(e->nparams > 0 && e->nparams <= DR32_ENG_MAX_PARAMS, "%s: %d params", e->slug, e->nparams);
        for (int i = 0; i < e->nparams; i++) {
            const dr32_eparam *p = &e->params[i];
            CHECK(!strncmp(p->key, e->prefix, strlen(e->prefix)), "%s: key %s lacks prefix", e->slug, p->key);
            CHECK(p->min < p->max && p->def >= p->min && p->def <= p->max, "%s: bad range", p->key);
            CHECK(p->page && p->page[0], "%s: no page", p->key);
            CHECK(strlen(p->short_name) <= 5, "%s: short name too long", p->key);
            for (int j = 0; j < i; j++) CHECK(strcmp(e->params[j].key, p->key), "%s: key %s twice", e->slug, p->key);
            int dup = 0;
            for (int j = 0; j < ns && !dup; j++) {
                if (strcmp(seen[j]->key, p->key)) continue;
                dup = 1;
                const dr32_eparam *q = seen[j];
                CHECK(seen_e[j]->family == e->family && e->family >= DR32_FAM_9W9 &&
                      !strcmp(seen_e[j]->prefix, e->prefix),
                      "duplicate key %s (%s and %s are not one kit port's lanes)", p->key, seen_e[j]->slug, e->slug);
                CHECK(!strcmp(q->name, p->name) && !strcmp(q->short_name, p->short_name) &&
                      q->min == p->min && q->max == p->max && !strcmp(q->page, p->page) &&
                      ((!q->options && !p->options) || (q->options && p->options && !strcmp(q->options, p->options))),
                      "%s: shared key differs between %s and %s", p->key, seen_e[j]->slug, e->slug);
            }
            if (!dup && ns < 512) { seen[ns] = p; seen_e[ns] = e; ns++; }
        }
    }
    CHECK(dr32_engine_get(DR32_ENG_SAMPLE) == NULL, "sample is not an engine");
    CHECK(dr32_engine_get(99) == NULL, "unknown engine");

    for (int i = 0; i < n; i++) {
        const dr32_model *m = dr32_model_at(i);
        const dr32_engine_ops *e = dr32_engine_get(m->engine);
        CHECK(dr32_model_find(m->slug) == i, "%s: find", m->slug);
        for (int k = 0; e && k < e->nparams; k++)
            CHECK(m->values[k] >= e->params[k].min && m->values[k] <= e->params[k].max,
                  "%s: %s = %g outside %g..%g", m->slug, e->params[k].key, m->values[k],
                  e->params[k].min, e->params[k].max);
        CHECK(m->pan >= -50 && m->pan <= 50, "%s: pan", m->slug);
        test_model(m);
    }
    CHECK(dr32_model_find("nope/none") == -1 && dr32_model_find("") == -1, "unknown slug");

    test_tune("simian/low_tom");
    test_tune("urchin/low_tom");
    test_tune("fm/tom");
    test_tune("fm/drip");

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
