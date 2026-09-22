// test_kit_ports.c — the kit-port engines (9W9, 6W6, 8W8, CW-78) against the
// machines they come from.
//
// ⭐ THE CLAIM IS "THE MACHINE'S OWN VOICE", so the test is an A/B against the
// machine itself, not a description of it: for every lane, a DR32 pad at its
// model's values must render SAMPLE FOR SAMPLE what the whole vendored machine
// renders with only that lane hit, until DR32's silence gate stops the pad
// (after which the machine's own tail is below the gate's level).
//
// And, per lane:
//   - +12 st on a pitched lane is an octave; on 9W9's kick (Tune = sweep time)
//     the transpose changes nothing at all
//   - a transpose that returns to 0 returns to the exact same sound
//   - every knob on the lane reaches the machine (moving it changes the audio)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dr32_engine.h"
#include "engines/9w9/er99_engine.h"

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SR 44100
#define LEN (SR * 3)
#define VEL 100

/* The machines' own C entry points (their headers are C++-only). */
typedef struct sd606_engine sd606_engine_t;
sd606_engine_t *sd606_create(float);
void sd606_destroy(sd606_engine_t *);
void sd606_trigger(sd606_engine_t *, int, int);
void sd606_render(sd606_engine_t *, float *, int);
typedef struct sc808_engine sc808_engine_t;
sc808_engine_t *sc808_create(float);
void sc808_destroy(sc808_engine_t *);
void sc808_trigger(sc808_engine_t *, int, int);
void sc808_render(sc808_engine_t *, float *, int);
typedef struct cr78_engine cr78_engine_t;
cr78_engine_t *cr78_create(float);
void cr78_destroy(cr78_engine_t *);
void cr78_trigger(cr78_engine_t *, int, int);
void cr78_render(cr78_engine_t *, float *, int);

static float want[LEN], got[LEN];

static void *make(const dr32_model *m) {
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    void *v = e->create(SR);
    for (int i = 0; i < e->nparams; i++) e->set(v, i, m->values[i]);
    return v;
}

/* Render in host-sized blocks; returns the frame the pad stopped at (LEN if
 * it never did). */
static int render_pad(const dr32_engine_ops *e, void *v, float *out, int n) {
    int stop = n;
    for (int at = 0; at < n; at += 128) {
        int m = n - at < 128 ? n - at : 128;
        if (!e->render(v, out + at, m) && stop == n) stop = at + m;
    }
    return stop;
}

static float peak(const float *x, int n) {
    float p = 0;
    for (int i = 0; i < n; i++) if (fabsf(x[i]) > p) p = fabsf(x[i]);
    return p;
}

static int crossings(const float *x, int n) {
    int c = 0, s = 0;
    for (int i = 0; i < n; i++) {
        int t = x[i] > 1e-3f ? 1 : (x[i] < -1e-3f ? -1 : 0);
        if (t && s && t != s) c++;
        if (t) s = t;
    }
    return c;
}

/* 9W9's picker order is its page order; its trigger enum is not. */
static const int N9_TRIG[] = {ER99_BD, ER99_SD, ER99_LT, ER99_MT, ER99_HT, ER99_RS,
                              ER99_HC, ER99_CHH, ER99_OHH, ER99_RC, ER99_CR};

/* The machine, fresh, with lane `lane` hit once at VEL, rendered in blocks. */
static void machine(int base, int lane, void *pad, float *out, int n) {
    if (base == DR32_ENG_6W6_BASE) {
        sd606_engine_t *m = sd606_create(SR);
        sd606_trigger(m, lane, VEL);
        for (int at = 0; at < n; at += 128) sd606_render(m, out + at, n - at < 128 ? n - at : 128);
        sd606_destroy(m);
    } else if (base == DR32_ENG_8W8_BASE) {
        sc808_engine_t *m = sc808_create(SR);
        sc808_trigger(m, lane, VEL);
        for (int at = 0; at < n; at += 128) sc808_render(m, out + at, n - at < 128 ? n - at : 128);
        sc808_destroy(m);
    } else if (base == DR32_ENG_CW78_BASE) {
        cr78_engine_t *m = cr78_create(SR);
        cr78_trigger(m, lane, VEL);
        for (int at = 0; at < n; at += 128) cr78_render(m, out + at, n - at < 128 ? n - at : 128);
        cr78_destroy(m);
    } else {
        er99_engine_t *m = calloc(1, sizeof(er99_engine_t));
        er99_engine_init(m, SR, NULL);
        /* The machine loads its cymbal WAVs itself; the pad's are the same
         * files, decoded once. Borrow them (the pad's machine is the first
         * member of its state, 9w9_engine.c n9_pad). */
        const er99_engine_t *pm = (const er99_engine_t *)pad;
        for (int i = 0; i < ER99_NUM_SAMPLERS; i++) {
            m->sampler[i].buffer = pm->sampler[i].buffer;
            m->sampler[i].length = pm->sampler[i].length;
        }
        er99_engine_trigger(m, (er99_trigger_t)N9_TRIG[lane], VEL);
        for (int at = 0; at < n; at += 128) er99_engine_render(m, out + at, n - at < 128 ? n - at : 128);
        for (int i = 0; i < ER99_NUM_SAMPLERS; i++) m->sampler[i].buffer = NULL;
        er99_engine_free(m);
        free(m);
    }
}

static int port_base(int engine) {
    if (engine >= DR32_ENG_CW78_BASE) return DR32_ENG_CW78_BASE;
    if (engine >= DR32_ENG_8W8_BASE)  return DR32_ENG_8W8_BASE;
    if (engine >= DR32_ENG_6W6_BASE)  return DR32_ENG_6W6_BASE;
    if (engine >= DR32_ENG_9W9_BASE)  return DR32_ENG_9W9_BASE;
    return -1;
}

static void test_ab(const dr32_model *m) {
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    const int base = port_base(m->engine), lane = m->engine - base;
    void *v = make(m);
    e->note_on(v, VEL / 127.0f, 0.0f);
    int stop = render_pad(e, v, got, LEN);
    machine(base, lane, v, want, LEN);

    int first = -1;
    for (int i = 0; i < stop && first < 0; i++) if (got[i] != want[i]) first = i;
    CHECK(first < 0, "%s: differs from the machine at frame %d (pad %g, machine %g)",
          m->slug, first, first < 0 ? 0.0 : (double)got[first], first < 0 ? 0.0 : (double)want[first]);
    CHECK(peak(want, LEN) > 0.01f, "%s: the machine itself is silent here — the A/B proves nothing", m->slug);
    CHECK(stop < LEN, "%s: the pad never stopped", m->slug);
    /* What the gate cut off, the machine renders below the gate's level
     * (with a little headroom: the gate measures 100 ms of it, the check the
     * whole rest of the tail). */
    if (stop < LEN) CHECK(peak(want + stop, LEN - stop) < 0.004f, "%s: stopped while the machine still rang (%g after frame %d)",
                          m->slug, (double)peak(want + stop, LEN - stop), stop);
    e->destroy(v);
}

/*
 * Semitones per Tune-knob step, from the machines' own pot tables: a LIN pot
 * spans (max-min) semitones, an EXP one 12*log2(max/min). 0 = not a pitch.
 */
static float st_per_step(const char *slug) {
    static const struct { const char *slug; float range_st; } T[] = {
        {"9w9/kick", 0},
        {"9w9/snare", 15.5936f}, {"9w9/low_tom", 10.1760f}, {"9w9/mid_tom", 7.7263f},
        {"9w9/hi_tom", 7.5363f}, {"9w9/rimshot", 12.0f}, {"9w9/clap", 13.2842f},
        {"9w9/hat_closed", 48.0f}, {"9w9/hat_open", 48.0f}, {"9w9/ride", 48.0f}, {"9w9/crash", 48.0f},
        {"8w8/low_tom", 4}, {"8w8/mid_tom", 4}, {"8w8/hi_tom", 4},
        {"8w8/low_conga", 4}, {"8w8/mid_conga", 4}, {"8w8/hi_conga", 4},
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++)
        if (!strcmp(T[i].slug, slug)) return T[i].range_st / 127.0f;
    return 24.0f / 127.0f;       /* every other lane: +-12 st, or a 0.5..2 ratio */
}

static float rel_err(const float *a, const float *b, int n) {
    double e = 0, r = 0;
    for (int i = 0; i < n; i++) { e += (double)(a[i] - b[i]) * (a[i] - b[i]); r += (double)a[i] * a[i]; }
    return r > 0 ? (float)sqrt(e / r) : 1.0f;
}

/*
 * DR32's transpose must BE the lane's Tune: transposing by N knob-steps' worth
 * of semitones sounds like turning Tune up N steps. This pins the kind (a
 * semitone offset vs a ratio) and the direction exactly, which a pitch
 * estimate cannot on a noise voice (a hat's bank moves, its filters do not).
 */
static void test_transpose(const dr32_model *m) {
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    const int it = 0;                               /* Tune is every lane's first knob */
    CHECK(strstr(e->params[it].key, "_tune") != NULL, "%s: first knob is not Tune", m->slug);
    const float step = st_per_step(m->slug);
    const float def = m->values[it];
    const int k = def + 10 <= 127 ? 10 : -10;
    void *a = make(m), *b = make(m);
    /* Both pads' Tune WRITTEN (9W9 holds its factory value in engineering
     * units until the knob moves, which is not on the pot grid). */
    e->set(a, it, def + (k > 0 ? -1 : 1)); e->set(a, it, def);
    e->set(b, it, def + (k > 0 ? -1 : 1)); e->set(b, it, def + k);
    e->note_on(a, 1.0f, step > 0 ? k * step : 12.0f);
    e->note_on(b, 1.0f, 0.0f);
    render_pad(e, a, want, SR / 10);
    render_pad(e, b, got, SR / 10);
    const float err = rel_err(got, want, SR / 10);
    if (step > 0) {
        CHECK(err < 0.02f, "%s: +%g st is not Tune %+d steps (rel err %g)", m->slug, (double)(k * step), k, (double)err);
    } else {
        /* 9W9's kick: Tune is its sweep time, so the transpose must leave the
         * sound exactly as it was — compare against an untransposed hit. */
        void *c = make(m);
        e->set(c, it, def + (k > 0 ? -1 : 1)); e->set(c, it, def);
        e->note_on(c, 1.0f, 0.0f);
        render_pad(e, c, got, SR / 10);
        CHECK(!memcmp(want, got, sizeof(float) * (SR / 10)), "%s: transpose moved a Tune that is not a pitch", m->slug);
        e->destroy(c);
    }
    e->destroy(a); e->destroy(b);

    /* A transpose returns EXACTLY. Two hits with no audio between them, so
     * the machines' free-running state (noise, drift, a metal bank) has
     * advanced identically on both pads: +7 then 0, against 0 then 0. The
     * second trigger re-reads Tune, so if the transpose left anything behind
     * in the value the machine reads, the two differ. */
    void *x = make(m), *y = make(m);
    e->note_on(x, 1.0f, 7.0f);
    e->note_on(x, 1.0f, 0.0f);
    e->note_on(y, 1.0f, 0.0f);
    e->note_on(y, 1.0f, 0.0f);
    render_pad(e, x, want, SR / 2);
    render_pad(e, y, got, SR / 2);
    CHECK(!memcmp(want, got, sizeof(float) * (SR / 2)), "%s: back at 0 st it is not the same voice (rel err %g)",
          m->slug, (double)rel_err(got, want, SR / 2));
    e->destroy(x); e->destroy(y);
}

/* An octave, measured, on a tonal lane of each machine. */
static void test_octave(const char *slug) {
    int mi = dr32_model_find(slug);
    CHECK(mi >= 0, "no model %s", slug);
    if (mi < 0) return;
    const dr32_model *m = dr32_model_at(mi);
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    int c[2];
    for (int k = 0; k < 2; k++) {
        void *v = make(m);
        e->note_on(v, 1.0f, k ? 12.0f : 0.0f);
        render_pad(e, v, got, SR / 4);
        c[k] = crossings(got + SR * 40 / 1000, SR / 10);
        e->destroy(v);
    }
    float ratio = c[0] ? (float)c[1] / (float)c[0] : 0;
    CHECK(ratio > 1.7f && ratio < 2.3f, "%s: +12 st moved pitch by x%.2f (%d -> %d crossings)",
          slug, ratio, c[0], c[1]);
}

/* Every knob on a lane must reach the machine. Two only speak once another
 * knob opens them, on the machines themselves: a distortion TYPE with Drive
 * up, and 9W9's kick Pitch with P. Depth up ("inert until P.Depth leaves
 * zero", schwung-9W9 README). Both sides of the comparison get the opener. */
static int opener(const dr32_engine_ops *e, const dr32_eparam *p) {
    const char *want = p->options ? "_drive" : !strcmp(p->key, "n9_bd_pitch") ? "n9_bd_pdepth" : NULL;
    if (!want) return -1;
    for (int i = 0; i < e->nparams; i++) if (strstr(e->params[i].key, want)) return i;
    return -1;
}

static void test_knobs(const dr32_model *m) {
    const dr32_engine_ops *e = dr32_engine_get(m->engine);
    for (int i = 0; i < e->nparams; i++) {
        const dr32_eparam *p = &e->params[i];
        const int op = opener(e, p);
        void *r = make(m), *v = make(m);
        if (op >= 0) { e->set(r, op, 64.0f); e->set(v, op, 64.0f); }
        const float to = p->options ? (m->values[i] >= 1 ? 0.0f : 3.0f)
                                    : (m->values[i] > 64 ? 10.0f : 120.0f);
        e->set(v, i, to);
        e->note_on(r, VEL / 127.0f, 0.0f);
        e->note_on(v, VEL / 127.0f, 0.0f);
        render_pad(e, r, want, SR);
        render_pad(e, v, got, SR);
        CHECK(memcmp(want, got, sizeof(float) * SR), "%s: %s %g -> %g changed nothing",
              m->slug, p->key, (double)m->values[i], (double)to);
        e->destroy(r); e->destroy(v);
    }
}

int main(void) {
    printf("kit ports\n");
    dr32_engines_set_module_dir("src");
    dr32_engines_init(SR);
    int ports = 0;
    for (int i = 0; i < dr32_model_count(); i++) {
        const dr32_model *m = dr32_model_at(i);
        if (port_base(m->engine) < 0) continue;
        ports++;
        test_ab(m);
        test_transpose(m);
        test_knobs(m);
    }
    CHECK(ports == 11 + 8 + 16 + 14, "%d kit-port models", ports);
    test_octave("9w9/low_tom");
    test_octave("6w6/low_tom");
    test_octave("8w8/low_tom");
    test_octave("cw78/low_conga");
    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
