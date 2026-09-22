// test_fm.c — DR32's own FM drum voices (dsp/engines/fm_engine.cpp): four
// engines (Kick, Snare, Metal, Perc) on one core.
//
// There is no original to A/B against: the engines are DR32's. So this pins
// what their knobs PROMISE, each measured on the rendered audio, on EVERY
// engine that has the knob (the key suffix after the prefix is the knob):
//   - Pitch is the pitch: a bare sine (no sweep, no FM, no noise; Metal at
//     Spread 0) crosses zero at 2 x Pitch per second, within 1%
//   - Decay is the time to fall 60 dB, within 2 dB
//   - Sweep starts the hit that many semitones up — or DOWN, it is bipolar —
//     and falls back to Pitch
//   - Bursts fires that many noise bursts, Burst Gap apart
//   - Mix: full left is the tone alone, full right the noise alone
//   - High Cut darkens; at its top it is off (bit-identical)
//   - Velocity: at 100% a half-velocity hit is 6 dB down; at 0% it is not
//   - the Velocity page's law: a FULL-velocity hit is the knobs as set, bit
//     for bit, whatever the velocity amounts; softer hits move each target
//     by the promised amount (pitch, decay both ways, sweep, noise level,
//     noise freq)
//   - Perc's Mangle: Crush holds samples, Bits quantises, Noise FM and Ring
//     change the tone
//   - Metal is not a bell: its hats' spectrum above 4 kHz is DENSE (no few
//     partials standing out), unlike one FM pair
//   - the page layout Josh asked for: Tone Level on knob 1, an Output page (Mix, Drive, Low/High Cut)
//   - a knob moved while the voice rings takes effect on the ringing voice
//   - once the envelopes are gone the voice writes exact zeros and stops
#define _GNU_SOURCE   /* M_PI on glibc under -std=c11 (CLAUDE.md, Testing) */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dr32_engine.h"

static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL [%s]: ", E ? E->slug : "-"); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SR 44100
static float buf[SR * 5];
static const dr32_engine_ops *E;

static const int ENGINES[] = {DR32_ENG_FM_KICK, DR32_ENG_FM_SNARE, DR32_ENG_FM_METAL, DR32_ENG_FM_PERC};

/* The engine's index for a knob by its SUFFIX ("pitch" -> "fk_pitch"). */
static int ix(const char *suffix) {
    char k[64];
    snprintf(k, sizeof k, "%s%s", E->prefix, suffix);
    return dr32_engine_param_index(E, k);
}
static int has(const char *suffix) { return ix(suffix) >= 0; }

static void set(void *v, const char *suffix, float val) {
    int i = ix(suffix);
    CHECK(i >= 0, "no param %s%s", E->prefix, suffix);
    if (i >= 0) E->set(v, i, val);
}
/* Only where the engine has it (Metal has no sweep, Kick no bursts). */
static void set_if(void *v, const char *suffix, float val) {
    if (has(suffix)) set(v, suffix, val);
}
static float pmin(const char *s) { return E->params[ix(s)].min; }
static float pmax(const char *s) { return E->params[ix(s)].max; }

static void *bare(float pitch, float decay_ms) {
    void *v = E->create(SR);
    set(v, "pitch", pitch); set(v, "decay", decay_ms);
    set_if(v, "sweep", 0); set(v, "mod", 0); set(v, "noise", 0); set(v, "tone", 100);
    set_if(v, "spread", 0); set(v, "mix", 0);
    set(v, "drive", 0); set(v, "lowcut", 20); set(v, "hicut", 20000);
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

static int crossings(const float *x, int n) {
    int z = 0;
    for (int i = 1; i < n; i++) if ((x[i - 1] < 0) != (x[i] < 0)) z++;
    return z;
}

static void test_layout(void) {
    CHECK(!strcmp(E->params[0].key + strlen(E->prefix), "tone"), "knob 1 is %s, want Tone Level", E->params[0].key);
    CHECK(!strcmp(E->params[0].page, "Tone"), "Tone Level is not on the Tone page");
    const char *mixk[] = {"mix", "drive", "lowcut", "hicut"};
    for (int k = 0; k < 4; k++)
        CHECK(has(mixk[k]) && !strcmp(E->params[ix(mixk[k])].page, "Output"), "%s is not on the Output page", mixk[k]);
    CHECK(has("noise") && !strcmp(E->params[ix("noise")].page, "Noise") &&
          strcmp(E->params[ix("noise") - 1].page, "Noise"),
          "Noise Level is not knob 1 of the Noise page");
    if (has("sweep")) CHECK(pmin("sweep") < 0 && pmax("sweep") > 0, "Sweep is not bipolar");
    CHECK(pmin("mix") == -pmax("mix"), "Mix is not centred");
    for (int i = 0; i < E->nparams; i++)
        CHECK(!strstr(E->params[i].name, "Clap"), "%s is still called %s", E->params[i].key, E->params[i].name);
}

static void test_pitch(void) {
    const float lo = pmin("pitch"), hi = pmax("pitch");
    const float hz[] = {lo * 1.5f, sqrtf(lo * hi), hi * 0.9f};
    for (int k = 0; k < 3; k++) {
        void *v = bare(hz[k], 1000);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR / 2);
        const float got = freq(buf, SR / 2);
        CHECK(fabsf(got / hz[k] - 1.0f) < 0.01f, "Pitch %g Hz sounds at %g Hz", hz[k], got);
        E->destroy(v);
    }
}

static void test_decay(void) {
    const float lo = pmin("decay"), hi = pmax("decay");
    /* Not past 2 s: a longer tail meets DR32's silence gate (100 ms under
     * 0.001, kit_port.h) before its own -60 dB point. */
    const float ms[] = {lo < 100 ? 100 : lo * 2, sqrtf(lo * hi) < 100 ? 150 : sqrtf(lo * hi), hi > 2000 ? 2000 : hi};
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
    if (!has("sweep")) return;
    /* The offset decays over Sweep Decay, so the pitch heard between two
     * zero crossings is PREDICTED from the law, not assumed:
     * phase(t) = P t + off (1 - e^(-a t)) / a, over the crossings' own span. */
    const float st[] = {12, -12};
    const float P = pmax("pitch") * 0.8f < 400 ? pmax("pitch") * 0.8f : 400;
    const float swdec = pmax("swdec") < 200 ? pmax("swdec") : 200;
    const float a = 6.907755f / (swdec * 0.001f);
    for (int k = 0; k < 2; k++) {
        void *v = bare(P, 1000);
        set(v, "sweep", st[k]); set(v, "swdec", swdec);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR);
        const float off = P * (powf(2.0f, st[k] / 12.0f) - 1.0f);
        double t0 = -1, t1 = -1; int cyc = -1;
        for (int i = 1; i < SR / 25; i++)                    /* the first 40 ms */
            if (buf[i - 1] < 0 && buf[i] >= 0) {
                const double t = (i - 1 + buf[i - 1] / (buf[i - 1] - buf[i])) / SR;
                if (t0 < 0) t0 = t;
                t1 = t; cyc++;
            }
        double ph0 = P * t0 + off * (1 - exp(-a * t0)) / a, ph1 = P * t1 + off * (1 - exp(-a * t1)) / a;
        const float want = (float) ((ph1 - ph0) / (t1 - t0)), got = cyc > 0 ? (float) (cyc / (t1 - t0)) : 0;
        CHECK(cyc > 2 && fabsf(got / want - 1.0f) < 0.02f, "Sweep %g st: first 40 ms at %g Hz, want %g", st[k], got, want);
        CHECK(st[k] > 0 ? got > P * 1.2f : got < P * 0.85f, "Sweep %g st does not start %s Pitch (%g vs %g)",
              st[k], st[k] > 0 ? "above" : "below", got, P);
        /* Past its -60 dB point: back at Pitch. */
        const int at = (int) (swdec * 0.001f * SR) + SR / 20;
        const float end = freq(buf + at, SR / 5);
        CHECK(fabsf(end / P - 1.0f) < 0.01f, "Sweep %g st ends at %g Hz, want %g", st[k], end, P);
        E->destroy(v);
    }
}

static void *bare_noise(void) {
    void *v = E->create(SR);
    set(v, "tone", 0); set(v, "noise", 100); set(v, "ndec", pmax("ndec")); set(v, "mix", 0);
    set(v, "lowcut", 20); set(v, "hicut", 20000); set(v, "drive", 0); set(v, "vel", 0);
    return v;
}

static void test_bursts(void) {
    if (!has("bursts")) return;
    for (int n = 1; n <= 6; n += 5) {
        void *v = bare_noise();
        set(v, "nmode", 2); set(v, "nfreq", pmin("nfreq")); set(v, "ndec", 200);
        set(v, "bursts", (float) n); set(v, "gap", 10);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR / 5);
        /* An onset: a 1 ms window more than 4x louder than the one before. */
        int onsets = 1;
        const int w = SR / 1000;
        for (int i = 2 * w; i < SR / 10; i += w)
            if (peak(buf + i, w) > 4.0f * peak(buf + i - w, w)) onsets++;
        CHECK(onsets == n, "Bursts %d fired %d", n, onsets);
        E->destroy(v);
    }
}

static void test_mix(void) {
    /* Both layers up; each end of Mix leaves one. Measured as energy, since
     * the tone is a 200 Hz sine and the noise high-passed at 2+ kHz. */
    float t[3], nz[3];
    const float at[] = {-100, 0, 100};
    for (int k = 0; k < 3; k++) {
        void *v = bare(200, 1000);
        set(v, "noise", 100); set(v, "ndec", pmax("ndec")); set(v, "nmode", 2);
        set(v, "nfreq", pmin("nfreq") > 2000 ? pmin("nfreq") : 2000);
        set(v, "mix", at[k]);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR / 10);
        /* Tone: the sample-average magnitude of the slow part; noise: the
         * first difference's (the noise is all edges, the sine none). */
        double s = 0, d = 0;
        for (int i = 1; i < SR / 10; i++) { s += fabsf(buf[i]); d += fabsf(buf[i] - buf[i - 1]); }
        t[k] = (float) s; nz[k] = (float) d;
        E->destroy(v);
    }
    /* Left: no noise, so the edges are only the sine's (tiny). */
    CHECK(nz[0] < 0.1f * nz[1], "Mix full left still has noise (%g vs %g)", nz[0], nz[1]);
    /* Right: no tone — what is left is all edge. */
    void *v = bare_noise();
    set(v, "nmode", 2); set(v, "nfreq", pmin("nfreq") > 2000 ? pmin("nfreq") : 2000);
    E->note_on(v, 1.0f, 0.0f);
    static float only[SR / 10];
    set(v, "vel", 60);
    run(v, only, SR / 10);
    E->destroy(v);
    double s = 0;
    for (int i = 0; i < SR / 10; i++) s += fabsf(only[i]);
    CHECK(fabsf(t[2] / (float) s - 1.0f) < 0.02f, "Mix full right is not the noise alone (%g vs %g)", t[2], s);
    (void) t[0];
}

static void test_hicut(void) {
    int z[2];
    for (int k = 0; k < 2; k++) {
        void *v = bare_noise();
        set(v, "nmode", 2); set(v, "nfreq", pmin("nfreq"));
        set(v, "hicut", k ? pmin("hicut") : 20000);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR / 4);
        z[k] = crossings(buf, SR / 4);
        E->destroy(v);
    }
    CHECK(z[1] < z[0] / 2, "High Cut %g Hz did not darken (%d -> %d crossings)", pmin("hicut"), z[0], z[1]);
}

static void test_velocity(void) {
    for (int depth = 0; depth <= 100; depth += 100) {
        float p[2];
        for (int k = 0; k < 2; k++) {
            void *v = bare(200, 500);
            set(v, "vel", (float) depth);
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

static const char *const VKEYS[] = {"vel", "vmod", "vsweep", "vpitch", "vdecay", "vnoise", "vnfreq"};

static void test_vel_hard_hit_is_the_knobs(void) {
    static float a[SR], b[SR];
    for (int i = 0; i < dr32_model_count(); i++) {
        const dr32_model *m = dr32_model_at(i);
        if (m->engine != E->id) continue;
        for (int pass = 0; pass < 2; pass++) {
            void *v = E->create(SR);
            for (int k = 0; k < E->nparams; k++) E->set(v, k, m->values[k]);
            for (int k = 0; k < 7; k++) {
                const int j = ix(VKEYS[k]);
                if (j >= 0) E->set(v, j, pass ? E->params[j].max : 0.0f);
            }
            E->note_on(v, 1.0f, 0.0f);
            run(v, pass ? b : a, SR);
            E->destroy(v);
        }
        CHECK(!memcmp(a, b, sizeof a), "%s: velocity amounts changed a full-velocity hit", m->slug);
    }
}

/* -60 dB time of a bare tone, in ms: the first 5 ms window 60 dB below the start. */
static float t60(void *v) {
    run(v, buf, SR * 4);
    const float p0 = peak(buf, SR / 200);
    for (int i = 0; i + SR / 200 <= SR * 4; i += SR / 1000)
        if (peak(buf + i, SR / 200) < p0 * 0.001f) return (i + SR / 400) * 1000.0f / SR;
    return 1e9f;
}

static void test_vel_targets(void) {
    /* Pitch: 12 st at zero velocity is an octave down; half velocity half that. */
    for (int k = 0; k < 2; k++) {
        void *v = bare(200, 1000);
        set(v, "vpitch", 12);
        E->note_on(v, k ? 0.5f : 0.0f, 0.0f);
        run(v, buf, SR / 2);
        const float want = k ? 200.0f / sqrtf(2.0f) : 100.0f, got = freq(buf, SR / 2);
        CHECK(fabsf(got / want - 1.0f) < 0.01f, "Vel>Pitch 12 st at vel %g: %g Hz, want %g", k ? 0.5 : 0.0, got, want);
        E->destroy(v);
    }
    /* Decay: +100% quarters a zero-velocity hit, -100% quadruples it. */
    const float amt[] = {100, -100}, mul[] = {0.25f, 4.0f};
    for (int k = 0; k < 2; k++) {
        void *v = bare(200, 400);
        /* Level off velocity: a quieter hit would meet DR32's silence gate
         * (-60 dBFS absolute) before its own -60 dB point. */
        set(v, "vel", 0);
        set(v, "vdecay", amt[k]);
        E->note_on(v, 0.0f, 0.0f);
        const float got = t60(v), want = 400 * mul[k];
        CHECK(fabsf(got / want - 1.0f) < 0.05f, "Vel>Decay %g%%: %g ms, want %g", amt[k], got, want);
        E->destroy(v);
    }
    /* Sweep: 100% removes the sweep from a zero-velocity hit. */
    if (has("vsweep")) {
        void *v = bare(100, 1500);
        set(v, "sweep", 24); set(v, "swdec", 200); set(v, "vsweep", 100);
        E->note_on(v, 0.0f, 0.0f);
        run(v, buf, SR / 10);
        const float got = freq(buf, SR / 10);
        CHECK(fabsf(got - 100.0f) < 1.0f, "Vel>Sweep 100%%: a soft hit starts at %g Hz, want 100", got);
        E->destroy(v);
    }
    /* Noise: 100% makes a half-velocity hit's noise 6 dB down. */
    {
        float pk[2];
        for (int k = 0; k < 2; k++) {
            void *v = bare_noise();
            set(v, "vnoise", 100);
            E->note_on(v, k ? 0.5f : 1.0f, 0.0f);
            run(v, buf, SR / 20);
            pk[k] = peak(buf, SR / 20);
            E->destroy(v);
        }
        const float db = 20.0f * log10f(pk[1] / pk[0]);
        CHECK(fabsf(db + 6.02f) < 0.5f, "Vel>Noise 100%%: half velocity is %.2f dB, want -6", db);
    }
    /* Noise Freq: 100% puts a zero-velocity lowpass 4 octaves down. Lowpassed
     * white noise crosses zero in proportion to its cutoff. */
    {
        int zc[2];
        for (int k = 0; k < 2; k++) {
            void *v = bare_noise();
            set(v, "nmode", 0); set(v, "nfreq", 3200 > pmin("nfreq") ? 3200 : 8000); set(v, "vnfreq", 100);
            E->note_on(v, k ? 0.0f : 1.0f, 0.0f);
            run(v, buf, SR / 2);
            zc[k] = crossings(buf, SR / 2);
            E->destroy(v);
        }
        const float r = (float) zc[0] / (float) zc[1];
        CHECK(r > 11.0f && r < 20.0f, "Vel>NFreq 100%%: cutoff ratio ~%.1f, want ~16", r);
    }
}

static void test_mangle(void) {
    if (!has("crush")) return;
    /* Crush 100%: every sample held for 64. */
    {
        void *v = bare(300, 1000);
        set(v, "crush", 100);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, 64 * 20);
        int held = 1;
        for (int i = 0; i < 64 * 20; i++) if (buf[i] != buf[i - i % 64]) held = 0;
        CHECK(held, "Crush 100%% does not hold 64 samples");
        E->destroy(v);
    }
    /* Bits 2: at most 4 levels before the output gain. */
    {
        void *v = bare(300, 1000);
        set(v, "bits", 2); set(v, "vel", 0);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR / 10);
        float lv[16]; int n = 0;
        for (int i = 0; i < SR / 10 && n < 16; i++) {
            int seen = 0;
            for (int k = 0; k < n; k++) if (lv[k] == buf[i]) seen = 1;
            if (!seen) lv[n++] = buf[i];
        }
        CHECK(n <= 5, "Bits 2 left %d levels", n);
        E->destroy(v);
    }
    /* Noise FM and Ring each change a tone whose noise layer is silent. */
    const char *ks[] = {"nzfm", "ring"};
    for (int k = 0; k < 2; k++) {
        static float a[SR / 10];
        void *v = bare(300, 1000);
        E->note_on(v, 1.0f, 0.0f);
        run(v, a, SR / 10);
        E->destroy(v);
        v = bare(300, 1000);
        set(v, ks[k], 100);
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR / 10);
        E->destroy(v);
        double diff = 0, ref = 0;
        for (int i = 0; i < SR / 10; i++) { diff += fabs(buf[i] - a[i]); ref += fabs(a[i]); }
        CHECK(diff > 0.2 * ref, "%s 100%% barely changes the tone (%.3f)", ks[k], diff / ref);
    }
}

/* Spectral density above 4 kHz: the fraction of 43 Hz bins within 20 dB of
 * the strongest. A bell's few partials give a small fraction; a hat's wash a
 * large one. A plain DFT over 1024 samples from 5 ms in. */
static float density(const float *x) {
    enum { N = 1024 };
    static float mag[N / 2];
    float mx = 0;
    const int b0 = 4000 * N / SR;
    for (int b = b0; b < N / 2; b++) {
        double re = 0, im = 0;
        for (int i = 0; i < N; i++) {
            const double w = 0.5 - 0.5 * cos(2 * M_PI * i / (N - 1));
            re += w * x[i] * cos(2 * M_PI * b * i / N);
            im -= w * x[i] * sin(2 * M_PI * b * i / N);
        }
        mag[b] = (float) sqrt(re * re + im * im);
        if (mag[b] > mx) mx = mag[b];
    }
    int dense = 0;
    for (int b = b0; b < N / 2; b++) if (mag[b] > mx * 0.1f) dense++;
    return (float) dense / (float) (N / 2 - b0);
}

static void test_metal_not_a_bell(void) {
    if (E->id != DR32_ENG_FM_METAL) return;
    const char *slugs[] = {"fm/chat", "fm/ohat", "fm/cymbal"};
    for (int s = 0; s < 3; s++) {
        const dr32_model *m = dr32_model_at(dr32_model_find(slugs[s]));
        void *v = E->create(SR);
        for (int k = 0; k < E->nparams; k++) E->set(v, k, m->values[k]);
        set(v, "noise", 0);                    /* the TONE alone must be dense */
        E->note_on(v, 1.0f, 0.0f);
        run(v, buf, SR / 10);
        const float d = density(buf + SR / 200);
        /* The first engine's hats (one pair, fb 70%): 0.13-0.17, measured. */
        CHECK(d > 0.5f, "%s tone is sparse (%.2f of bins within 20 dB) — a bell, not metal", slugs[s], d);
        printf("  %-10s tone density %.2f\n", m->name, d);
        E->destroy(v);
    }
}

static void test_live_knob(void) {
    /* Decay shortened mid-ring must shorten THIS ring. */
    void *v = bare(200, pmax("decay"));
    E->note_on(v, 1.0f, 0.0f);
    run(v, buf, SR / 10);
    const float before = peak(buf + SR / 10 - 256, 256);
    set(v, "decay", 50);
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
        if (m->engine != E->id) continue;
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
    printf("fm drums\n");
    for (unsigned n = 0; n < sizeof ENGINES / sizeof ENGINES[0]; n++) {
        E = dr32_engine_get(ENGINES[n]);
        CHECK(E != NULL, "no engine %d", ENGINES[n]);
        if (!E) continue;
        printf(" %s\n", E->name);
        test_layout();
        test_pitch();
        test_decay();
        test_sweep();
        test_bursts();
        test_mix();
        test_hicut();
        test_velocity();
        test_vel_hard_hit_is_the_knobs();
        test_vel_targets();
        test_mangle();
        test_metal_not_a_bell();
        test_live_knob();
        test_stops();
        bench();
    }
    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
