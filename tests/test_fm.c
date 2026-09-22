// test_fm.c — DR32's own FM drum voice (dsp/engines/fm_engine.cpp).
//
// There is no original to A/B against: the engine is DR32's. So this pins
// what its knobs PROMISE, each measured on the rendered audio:
//   - Pitch is the pitch: a bare sine (no sweep, no FM, no noise) crosses zero
//     at 2 x Pitch per second, within 1%
//   - Decay is the time to fall 60 dB, within 2 dB
//   - Sweep starts the hit that many semitones up, and falls back to Pitch
//   - Claps fires that many noise bursts, Clap Gap apart
//   - Velocity: at 100% a half-velocity hit is 6 dB down; at 0% it is not
//   - a knob moved while the voice rings takes effect on the ringing voice
//   - once the envelopes are gone the voice writes exact zeros and stops
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dr32_engine.h"

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SR 44100
static float buf[SR * 4];
static const dr32_engine_ops *E;

static void set(void *v, const char *key, float val) {
    int i = dr32_engine_param_index(E, key);
    CHECK(i >= 0, "no param %s", key);
    if (i >= 0) E->set(v, i, val);
}

static void *bare(float pitch, float decay_ms) {
    void *v = E->create(SR);
    set(v, "fm_pitch", pitch); set(v, "fm_decay", decay_ms);
    set(v, "fm_sweep", 0); set(v, "fm_mod", 0); set(v, "fm_noise", 0);
    set(v, "fm_drive", 0); set(v, "fm_lowcut", 20);
    return v;
}

static void run(void *v, float *out, int n) {
    for (int at = 0; at < n; at += 128) E->render(v, out + at, n - at < 128 ? n - at : 128);
}

static float peak(const float *x, int n) {
    float p = 0;
    for (int i = 0; i < n; i++) if (fabsf(x[i]) > p) p = fabsf(x[i]);
    return p;
}

/* Frequency from the first and last rising zero crossing, interpolated: fine
 * enough for a few cycles, where counting crossings is not. */
static float freq(const float *x, int n) {
    double first = -1, last = -1; int cyc = -1;
    for (int i = 1; i < n; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0) first = t;
            last = t; cyc++;
        }
    return cyc > 0 ? (float) (cyc * SR / (last - first)) : 0.0f;
}

static void test_pitch(void) {
    const float hz[] = {50, 220, 1000};
    for (int k = 0; k < 3; k++) {
        void *v = bare(hz[k], 4000);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR);
        const float got = freq(buf, SR);
        CHECK(fabsf(got / hz[k] - 1.0f) < 0.01f, "Pitch %g Hz sounds at %g Hz", hz[k], got);
        E->destroy(v);
    }
}

static void test_decay(void) {
    const float ms[] = {100, 500, 2000};
    for (int k = 0; k < 3; k++) {
        void *v = bare(200, ms[k]);
        E->note_on(v, 1.0f, 0.0f);
        const int n = (int) (ms[k] * 0.001f * SR) + SR / 50;
        run(v, buf, n);
        const float p0 = peak(buf, SR / 200);          /* first 5 ms  */
        const int at = (int) (ms[k] * 0.001f * SR);
        const float p1 = peak(buf + at - SR / 400, SR / 200);   /* 5 ms around Decay */
        const float db = 20.0f * log10f(p1 / p0);
        CHECK(fabsf(db + 60.0f) < 2.0f, "Decay %g ms: %.1f dB at %g ms, want -60", ms[k], db, ms[k]);
        E->destroy(v);
    }
}

static void test_sweep(void) {
    void *v = bare(100, 4000);
    set(v, "fm_sweep", 12); set(v, "fm_swdec", 1000);
    E->note_on(v, 1.0f, 0.0f);
    run(v, buf, SR * 2);
    /* First 20 ms: about an octave up (the offset has fallen ~7% by the end). */
    const float start = freq(buf, SR / 50);
    CHECK(start > 185.0f && start < 200.0f, "Sweep 12 st starts at %g Hz, want ~195", start);
    /* 1.5 s in, past its -60 dB point: back at Pitch. */
    const float end = freq(buf + SR * 3 / 2, SR / 2);
    CHECK(fabsf(end - 100.0f) < 2.0f, "Sweep ends at %g Hz, want 100", end);
    E->destroy(v);
}

static void test_claps(void) {
    for (int claps = 1; claps <= 6; claps += 5) {
        void *v = E->create(SR);
        set(v, "fm_tone", 0); set(v, "fm_noise", 100); set(v, "fm_nmode", 2);
        set(v, "fm_nfreq", 100); set(v, "fm_ndec", 200); set(v, "fm_lowcut", 20);
        set(v, "fm_claps", (float) claps); set(v, "fm_gap", 10);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR / 5);
        /* An onset: a 1 ms window more than 4x louder than the one before. */
        int onsets = 1;
        const int w = SR / 1000;
        for (int i = 2 * w; i < SR / 10; i += w)
            if (peak(buf + i, w) > 4.0f * peak(buf + i - w, w)) onsets++;
        CHECK(onsets == claps, "Claps %d fired %d bursts", claps, onsets);
        E->destroy(v);
    }
}

static void test_velocity(void) {
    for (int depth = 0; depth <= 100; depth += 100) {
        float p[2];
        for (int k = 0; k < 2; k++) {
            void *v = bare(200, 500);
            set(v, "fm_vel", (float) depth);
            E->note_on(v, k ? 0.5f : 1.0f, 0.0f);
            run(v, buf, SR / 10);
            p[k] = peak(buf, SR / 10);
            E->destroy(v);
        }
        const float db = 20.0f * log10f(p[1] / p[0]);
        const float want = depth ? -6.02f : 0.0f;
        CHECK(fabsf(db - want) < 0.2f, "Velocity %d%%: half velocity is %.2f dB, want %.2f", depth, db, want);
    }
}

static void test_live_knob(void) {
    /* Decay shortened mid-ring must shorten THIS ring. */
    void *v = bare(200, 4000);
    E->note_on(v, 1.0f, 0.0f);
    run(v, buf, SR / 10);
    const float before = peak(buf + SR / 10 - 256, 256);
    set(v, "fm_decay", 50);
    run(v, buf, SR / 10);
    const float after = peak(buf + SR / 10 - 256, 256);
    CHECK(after < before * 0.001f, "Decay moved mid-ring did not shorten it (%g -> %g)", before, after);
    E->destroy(v);
}

static void test_stops(void) {
    void *v = bare(200, 50);
    E->note_on(v, 1.0f, 0.0f);
    int blocks = 0, active = 1;
    while (active && blocks < SR / 128) { active = E->render(v, buf, 128); blocks++; }
    CHECK(!active, "a 50 ms hit was still active after 1 s");
    CHECK(blocks * 128 < SR * 3 / 10, "a 50 ms hit ran %d ms", blocks * 128 * 1000 / SR);
    /* Silent once stopped, and a new hit starts it again. */
    E->render(v, buf, 128);
    CHECK(peak(buf, 128) == 0.0f, "a stopped voice is not silent");
    E->note_on(v, 1.0f, 0.0f);
    CHECK(E->render(v, buf, 128) && peak(buf, 128) > 0.01f, "a stopped voice did not re-hit");
    E->destroy(v);
}

/* Not a check: what a sounding pad costs, per 128-frame block, per model. */
static void bench(void) {
    for (int i = 0; i < dr32_model_count(); i++) {
        const dr32_model *m = dr32_model_at(i);
        if (m->engine != DR32_ENG_FM) continue;
        void *v = E->create(SR);
        for (int k = 0; k < E->nparams; k++) E->set(v, k, m->values[k]);
        const int blocks = 2000;
        clock_t t0 = clock();
        for (int r = 0; r < 20; r++) {
            E->note_on(v, 1.0f, 0.0f);
            for (int b = 0; b < blocks / 20; b++) E->render(v, buf, 128);
        }
        const double us = (double) (clock() - t0) / CLOCKS_PER_SEC * 1e6 / blocks;
        printf("  %-12s %.2f us/block\n", m->name, us);
        E->destroy(v);
    }
}

int main(void) {
    printf("fm drum\n");
    E = dr32_engine_get(DR32_ENG_FM);
    CHECK(E != NULL, "no FM engine");
    if (!E) return 1;
    test_pitch();
    test_decay();
    test_sweep();
    test_claps();
    test_velocity();
    test_live_knob();
    test_stops();
    bench();
    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
