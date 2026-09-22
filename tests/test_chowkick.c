// test_chowkick.c — DR32's ChowKick port against ChowKick itself.
//
// The reference is ChowKick's OWN DSP classes (its src/dsp/, unmodified)
// built against the JUCE and chowdsp_utils it pins — tools/chowkick_ref/ —
// rendered for each factory preset by tools/chowkick_ref/make_golden.sh into
// tests/fixtures/chowkick/<slug>.golden: the first 100 ms exactly, the peak of
// each 100 ms over 2 s, and a pitch count.
//
// ⚠ NOT bit-identical, and not meant to be: the plugin runs four voices in
// SIMD lanes with xsimd's sin/cos/tanh/pow, the port is scalar std::. Those
// differ in the last bit, which on a lightly damped resonator becomes a slow
// PHASE drift over seconds — the sample-by-sample error grows while the
// envelope and pitch stay the same. So the test pins what the ear hears:
//   - the attack (first 50 ms) to within -40 dB of the plugin's own samples,
//     and 50-100 ms to within -30 dB
//   - the envelope, every 100 ms for 2 s, within 0.5 dB (while the plugin is
//     above -60 dBFS; below that DR32's silence gate may already have stopped)
//   - the pitch, by zero crossings 100-600 ms, within one crossing
// Measured when written (2026-09-22): attack -43.3 dB worst (Tonal), envelope
// within 0.2 dB everywhere, pitch identical on all five.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dr32_engine.h"

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SR 44100
#define N_EXACT 4410
#define N_ENV 20
#define GOLDEN_LEN (N_EXACT + N_ENV + 1)

static float port[SR * 2];

/* 1 = compute ChowKick's pulse circuit every sample (chowkick_engine.cpp). */
extern int dr32_chowkick_no_hold;

static float err_db(const float *a, const float *b, int i0, int i1) {
    double e = 0, r = 0;
    for (int i = i0; i < i1; i++) { e += (double) (a[i] - b[i]) * (a[i] - b[i]); r += (double) a[i] * a[i]; }
    return (e > 0 && r > 0) ? (float) (10.0 * log10(e / r)) : -999.0f;
}

static float peak(const float *x, int n) {
    float p = 0;
    for (int i = 0; i < n; i++) if (fabsf(x[i]) > p) p = fabsf(x[i]);
    return p;
}

static int crossings(const float *x, int n) {
    const float th = peak(x, n) * 0.02f;
    int c = 0, s = 0;
    for (int i = 0; i < n; i++) {
        int t = x[i] > th ? 1 : (x[i] < -th ? -1 : 0);
        if (t && s && t != s) c++;
        if (t) s = t;
    }
    return c;
}

static void test_preset(const char *slug) {
    char path[256];
    snprintf(path, sizeof path, "tests/fixtures/chowkick/%s.golden", slug + strlen("chowkick/"));
    static float g[GOLDEN_LEN];
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "%s: no golden at %s", slug, path);
    if (!f) return;
    size_t got = fread(g, sizeof(float), GOLDEN_LEN, f);
    fclose(f);
    CHECK(got == GOLDEN_LEN, "%s: golden is %zu floats, want %d", slug, got, GOLDEN_LEN);
    if (got != GOLDEN_LEN) return;

    int mi = dr32_model_find(slug);
    CHECK(mi >= 0, "no model %s", slug);
    if (mi < 0) return;
    const dr32_model *m = dr32_model_at(mi);
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    void *v = e->create(SR);
    for (int k = 0; k < e->nparams; k++) e->set(v, k, m->values[k]);
    e->note_on(v, 100 / 127.0f, 0.0f);
    for (int at = 0; at < SR * 2; at += 128) e->render(v, port + at, 128);
    e->destroy(v);

    const float a = err_db(g, port, 0, 2205), b = err_db(g, port, 2205, 4410);
    CHECK(a < -40.0f, "%s: attack (0-50 ms) is %.1f dB from ChowKick's own", slug, (double) a);
    CHECK(b < -30.0f, "%s: 50-100 ms is %.1f dB from ChowKick's own", slug, (double) b);

    for (int w = 0; w < N_ENV; w++) {
        const float want = g[N_EXACT + w];
        if (want < 0.001f) continue;                 /* below -60 dBFS: the gate's business */
        const float have = peak(port + w * 4410, 4410);
        const float d = 20.0f * log10f(have / want);
        CHECK(fabsf(d) < 0.5f, "%s: envelope at %d ms is %+.2f dB off ChowKick's", slug, w * 100, (double) d);
    }
    const int zw = (int) g[N_EXACT + N_ENV], zh = crossings(port + 4410, 22050);
    CHECK(abs(zw - zh) <= 1, "%s: pitch %d crossings vs ChowKick's %d", slug, zh, zw);
}

/* ⭑ The one optimisation must change NOTHING: the pulse circuit is held at its
 * fixed point between hits instead of recomputed. 20 s of every preset,
 * including a second hit 5 s in (the circuit must leave the hold on a pulse),
 * with the hold and without it, bit for bit. */
static void test_hold_is_exact(const char *slug) {
    static float with[SR * 20], without[SR * 20];
    const dr32_model *m = dr32_model_at(dr32_model_find(slug));
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    for (int pass = 0; pass < 2; pass++) {
        dr32_chowkick_no_hold = pass;
        float *out = pass ? without : with;
        void *v = e->create(SR);
        for (int k = 0; k < e->nparams; k++) e->set(v, k, m->values[k]);
        e->note_on(v, 100 / 127.0f, 0.0f);
        for (int at = 0; at < SR * 20; at += 128) {
            if (at == SR * 5 - (SR * 5) % 128) e->note_on(v, 60 / 127.0f, 0.0f);
            e->render(v, out + at, 128);
        }
        e->destroy(v);
    }
    dr32_chowkick_no_hold = 0;
    CHECK(!memcmp(with, without, sizeof with), "%s: holding the pulse circuit changed the sound", slug);
}

int main(void) {
    printf("chowkick\n");
    test_hold_is_exact("chowkick/default");
    test_hold_is_exact("chowkick/tonal");
    test_hold_is_exact("chowkick/wonky");
    test_preset("chowkick/default");
    test_preset("chowkick/tight");
    test_preset("chowkick/tonal");
    test_preset("chowkick/bouncy");
    test_preset("chowkick/wonky");
    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
