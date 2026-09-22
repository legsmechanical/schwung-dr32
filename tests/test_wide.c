/* See test_split.c: _GNU_SOURCE is the one feature macro that is additive on
 * both glibc and Darwin under -std=c11. */
#define _GNU_SOURCE

/*
 * Wide — the per-pad widener (dr32_kit.c), on the Stereo page with WMODE and
 * its crossover, Wide Freq. WMODE Comb, a complementary-comb widener:
 *     side = g * HP(mid)(t - 8 ms),  L += side,  R -= side,  g = Wide / 100
 * What it promises, measured on the kit's output:
 *   - Wide 0 is a TRUE bypass, bit for bit, whatever Wide Freq says
 *   - the MONO SUM is the dry pad: L + R is untouched at any setting
 *   - full band: (L - R) / 2 is exactly g x the mid, 8 ms late
 *   - the amount is linear: side energy goes as g squared
 *   - NO LEAN: the two sides carry the same energy (the Haas delay it
 *     replaced moved the image toward the leading side)
 *   - above a crossover only: a kick's body stays centred
 *   - nothing is cut off: the delayed copy plays out after the pad stops
 *     sounding, on BOTH render paths
 *   - a NEGATIVE Wide mirrors: the comb's side flips sign; Haas delays the
 *     LEFT side instead of the right
 *   - WMODE Haas is the Haas delay, exactly: one side 15 ms x (Wide/100)^2
 *     late, the other untouched
 *   - the knobs clamp, read back, and survive a state round trip
 */
#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_params.h"
#include "../dsp/dr32_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define SR 44100
#define FR 128
#define LEN (FR * 172)          /* ~0.5 s, WHOLE blocks: the render loops step by FR */

static void set(dr32_kit *k, const char *key, const char *val) { dr32_apply_param(k, key, val); }
static const char *get(const dr32_kit *k, const char *key) {
    static char b[256];
    b[0] = '\0';
    dr32_read_param(k, key, b, sizeof b);
    return b;
}

/* One hit of `model` on pad 1, centred, rendered LEN frames into out (L,R). */
static void hit(dr32_kit *k, const char *model, const char *wide, const char *freq, float *out) {
    dr32_kit_init(k);
    set(k, "pad1_model", model);
    set(k, "pad1_pan", "0");
    if (wide) set(k, "pad1_wide", wide);
    if (freq) set(k, "pad1_wide_freq", freq);
    set(k, "pad1_play", "127");
    for (int at = 0; at < LEN; at += FR) dr32_kit_render(k, out + 2 * at, FR);
}

static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }

/* A 40 ms mono WAV of a 3 kHz tone that STOPS DEAD, QUIET: at Wide 100% a
 * side can peak at twice the dry level, and the split path's int16 would clip
 * it — which reads as a lost tail. It is loud enough to measure, not to clip.
 * A sample pad goes quiet
 * the moment its sample ends, with none of a synth's 100 ms of near-silence
 * before its gate, so it is the case where a lost tail is audible. */
static void make_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const uint32_t n = SR / 25, data = n * 2;
    fwrite("RIFF", 1, 4, f); w32(f, 36 + data); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); w32(f, 16); w16(f, 1); w16(f, 1);
    w32(f, SR); w32(f, SR * 2); w16(f, 2); w16(f, 16);
    fwrite("data", 1, 4, f); w32(f, data);
    for (uint32_t i = 0; i < n; i++) w16(f, (uint16_t) (int16_t) (4000.0 * sin(i * 2 * 3.14159265 * 3000 / SR)));
    fclose(f);
}

static double energy(const float *x, int ch, int lag) {
    double e = 0;
    for (int i = lag; i < LEN; i++) e += (double) x[2 * i + ch] * x[2 * i + ch];
    return e;
}

int main(void) {
    printf("wide\n");
    static dr32_kit k;
    static float a[2 * LEN], b[2 * LEN];

    /* ---- Wide 0 is a true bypass -------------------------------------- */
    hit(&k, "fm/snare", NULL, NULL, a);
    hit(&k, "fm/snare", "0", "900", b);
    CHECK(!memcmp(a, b, sizeof a), "Wide 0 with a crossover set changed the pad");
    for (int i = 0; i < LEN; i++)
        if (a[2 * i] != a[2 * i + 1]) { CHECK(0, "a centred pad is not mono at frame %d", i); break; }

    const int D = (int) (8.0f * 0.001f * SR + 0.5f);      /* 353 */

    /* ---- the mono sum is the dry pad ------------------------------------ */
    {
        hit(&k, "fm/snare", "70", "400", b);
        float worst = 0, pk = 0;
        for (int i = 0; i < LEN; i++) {
            const float e = fabsf((b[2 * i] + b[2 * i + 1]) - (a[2 * i] + a[2 * i + 1]));
            if (e > worst) worst = e;
            if (fabsf(a[2 * i]) > pk) pk = fabsf(a[2 * i]);
        }
        CHECK(worst <= 4e-7f * pk, "Wide changed the mono sum by %g (peak %g)", worst, pk);
    }

    /* ---- full band: the side IS g x the mid, 8 ms late ------------------ */
    {
        hit(&k, "fm/snare", "50", "20", b);
        float worst = 0;
        for (int i = 0; i < LEN; i++) {
            const float want = i >= D ? 0.5f * a[2 * (i - D)] : 0.0f;   /* a is mono: mid = L */
            const float e = fabsf(0.5f * (b[2 * i] - b[2 * i + 1]) - want);
            if (e > worst) worst = e;
        }
        CHECK(worst < 1e-6f, "the side is not 0.5 x the mid 8 ms late (worst %g)", worst);

        /* A NEGATIVE Wide mirrors it: the same side, sign flipped. */
        hit(&k, "fm/snare", "-50", "20", b);
        worst = 0;
        for (int i = 0; i < LEN; i++) {
            const float want = i >= D ? -0.5f * a[2 * (i - D)] : 0.0f;
            const float e = fabsf(0.5f * (b[2 * i] - b[2 * i + 1]) - want);
            if (e > worst) worst = e;
        }
        CHECK(worst < 1e-6f, "Wide -50%% is not the mirrored side (worst %g)", worst);
    }

    /* ---- linear amount -------------------------------------------------- */
    {
        double side[2];
        const char *pct[] = {"50", "100"};
        for (int t = 0; t < 2; t++) {
            hit(&k, "fm/snare", pct[t], "20", b);
            double e = 0;
            for (int i = 0; i < LEN; i++) { const double d = b[2 * i] - b[2 * i + 1]; e += d * d; }
            side[t] = e;
        }
        CHECK(fabs(side[1] / side[0] - 4.0) < 1e-3, "Wide 100%% vs 50%%: side energy x%.4f, want x4", side[1] / side[0]);
    }

    /* ---- no lean --------------------------------------------------------- */
    {
        hit(&k, "fm/chat", "100", "150", b);
        const double el = energy(b, 0, 0), er = energy(b, 1, 0);
        CHECK(fabs(er / el - 1.0) < 0.1, "Wide leans: R/L energy %.3f on a centred hat", er / el);
        printf("  hat at Wide 100%%: R/L energy %.3f\n", er / el);
    }

    /* ---- nothing cut off: a short hit's delayed copy plays out ---------- */
    {
        hit(&k, "fm/chat", "100", "20", b);
        double s2 = 0;
        for (int i = 0; i < LEN; i++) { const double d = 0.5 * (b[2 * i] - b[2 * i + 1]); s2 += d * d; }
        static float dry[2 * LEN];
        hit(&k, "fm/chat", NULL, NULL, dry);
        const double m2 = energy(dry, 0, 0);
        CHECK(fabs(s2 / m2 - 1.0) < 1e-4, "the delayed copy lost its end: side/mid energy %.6f, want 1", s2 / m2);
        CHECK(!dr32_pad_sounding(&k.pads[0]) && k.pads[0].wide.tail <= 0,
              "the stage did not finish after the hit (tail %d)", k.pads[0].wide.tail);
    }

    /* ...and on a SAMPLE pad, on BOTH render paths: the sample ends abruptly,
     * so the pad stops sounding while the delayed copy still has 8 ms to go. */
    {
        const char *wav = "/tmp/dr32_wide.wav";
        make_wav(wav);
        static float dry[2 * LEN];
        for (int path = 0; path < 3; path++) {
            dr32_kit_init(&k);
            set(&k, "pad1_sample", wav);
            set(&k, "pad1_pan", "0");
            set(&k, "pad1_hold", "60");
            if (path) { set(&k, "pad1_wide", "100"); set(&k, "pad1_wide_freq", "20"); }
            set(&k, "pad1_play", "127");
            float *o = path ? b : dry;
            static int16_t mo[2 * FR];
            for (int at = 0; at < LEN; at += FR) {
                if (path < 2) { dr32_kit_render(&k, o + 2 * at, FR); continue; }
                memset(mo, 0, sizeof mo);
                int16_t *vo[32] = {0};
                dr32_kit_render_split(&k, vo, 32, mo, FR);
                for (int n = 0; n < 2 * FR; n++) o[2 * at + n] = mo[n] / 32767.0f;
            }
            if (!path) continue;
            double s2 = 0;
            for (int i = 0; i < LEN; i++) { const double d = 0.5 * (b[2 * i] - b[2 * i + 1]); s2 += d * d; }
            const double m2 = energy(dry, 0, 0);
            CHECK(m2 > 0 && fabs(s2 / m2 - 1.0) < 2e-3, "%s: a sample pad's delayed copy lost its end: side/mid %.4f",
                  path == 2 ? "split render" : "render_block", s2 / m2);
        }
    }

    /* ---- the crossover: a kick's body stays centred --------------------- */
    {
        double side[2];
        const char *freq[] = {"20", "400"};
        for (int f = 0; f < 2; f++) {
            hit(&k, "fm/kick", "100", freq[f], b);
            double sd = 0, m = 0;
            for (int i = 0; i < LEN; i++) {
                const double l = b[2 * i], r = b[2 * i + 1];
                sd += (l - r) * (l - r); m += (l + r) * (l + r);
            }
            side[f] = sd / m;
        }
        CHECK(side[0] > 0.1, "full-band Wide on a kick barely widens it (side/mid %.3f)", side[0]);
        CHECK(side[1] < 0.2 * side[0], "Wide Freq 400 did not keep the kick's body centred (side/mid %.3f vs %.3f)",
              side[1], side[0]);
        printf("  kick side/mid: full band %.3f, crossover 400 Hz %.4f\n", side[0], side[1]);
    }

    /* ---- WMODE Haas: the Haas delay, exactly, on the sign's side ---------- */
    {
        const int pcts[] = {50, 100, -50, -100};
        for (int t = 0; t < 4; t++) {
            dr32_kit_init(&k);
            set(&k, "pad1_model", "fm/snare");
            set(&k, "pad1_pan", "0");
            set(&k, "pad1_wide_mode", "Haas");
            char w[8]; snprintf(w, sizeof w, "%d", pcts[t]);
            set(&k, "pad1_wide", w);
            set(&k, "pad1_wide_freq", "20");
            set(&k, "pad1_play", "127");
            for (int at = 0; at < LEN; at += FR) dr32_kit_render(&k, b + 2 * at, FR);
            const float aa = pcts[t] * 0.01f;
            const int d = (int) (15.0f * aa * aa * 0.001f * SR + 0.5f);
            const int dch = pcts[t] > 0 ? 1 : 0;
            int ok_other = 1, ok_delayed = 1;
            for (int i = 0; i < LEN; i++) {
                if (b[2 * i + 1 - dch] != a[2 * i + 1 - dch]) ok_other = 0;
                if (b[2 * i + dch] != (i >= d ? a[2 * (i - d) + dch] : 0.0f)) ok_delayed = 0;
            }
            CHECK(ok_other, "Haas %d%% changed the %s side", pcts[t], dch ? "left" : "right");
            CHECK(ok_delayed, "Haas %d%% is not the %s side delayed by %d frames", pcts[t],
                  dch ? "right" : "left", d);
        }
        dr32_kit_init(&k);
        CHECK(!strcmp(get(&k, "pad1_wide_mode"), "Comb"), "Wide Mode defaults to %s, want Comb", get(&k, "pad1_wide_mode"));
    }

    /* ---- both render entry points agree --------------------------------- */
    {
        static dr32_kit c;
        static int16_t mo[2 * FR];
        hit(&k, "fm/snare", "12", "300", b);
        dr32_kit_init(&c);
        set(&c, "pad1_model", "fm/snare");
        set(&c, "pad1_pan", "0");
        set(&c, "pad1_wide", "12");
        set(&c, "pad1_wide_freq", "300");
        set(&c, "pad1_play", "127");
        float worst = 0;
        for (int at = 0; at < LEN; at += FR) {
            memset(mo, 0, sizeof mo);
            int16_t *vo[32] = {0};
            dr32_kit_render_split(&c, vo, 32, mo, FR);
            for (int n = 0; n < 2 * FR; n++) {
                float want = b[2 * at + n];
                if (want > 1) want = 1;
                if (want < -1) want = -1;
                const float e = fabsf(mo[n] / 32767.0f - want);
                if (e > worst) worst = e;
            }
        }
        CHECK(worst < 2.0f / 32767.0f, "the split render differs from render_block by %g", worst);
    }

    /* ---- the knobs ------------------------------------------------------ */
    {
        dr32_kit_init(&k);
        CHECK(!strcmp(get(&k, "pad1_wide"), "0") && !strcmp(get(&k, "pad1_wide_freq"), "150"),
              "defaults: wide %s, wide_freq %s", get(&k, "pad1_wide"), get(&k, "pad1_wide_freq"));
        set(&k, "pad1_wide", "37");
        CHECK(!strcmp(get(&k, "pad1_wide"), "37"), "wide reads %s", get(&k, "pad1_wide"));
        set(&k, "pad1_wide", "150");
        CHECK(!strcmp(get(&k, "pad1_wide"), "100"), "wide clamps to 100: %s", get(&k, "pad1_wide"));
        set(&k, "pad1_wide", "-150");
        CHECK(!strcmp(get(&k, "pad1_wide"), "-100"), "wide clamps to -100: %s", get(&k, "pad1_wide"));
        set(&k, "pad1_wide_freq", "5");
        CHECK(!strcmp(get(&k, "pad1_wide_freq"), "20"), "wide_freq clamps to 20: %s", get(&k, "pad1_wide_freq"));
        set(&k, "pad1_wide_freq", "9000");
        CHECK(!strcmp(get(&k, "pad1_wide_freq"), "4000"), "wide_freq clamps to 4000: %s", get(&k, "pad1_wide_freq"));

        /* Choosing a model or a sample keeps it: it is where the pad sits. */
        set(&k, "pad1_wide", "7");
        set(&k, "pad1_wide_freq", "220");
        set(&k, "pad1_wide_mode", "Haas");
        set(&k, "pad1_model", "fm/kick");
        CHECK(!strcmp(get(&k, "pad1_wide"), "7") && !strcmp(get(&k, "pad1_wide_freq"), "220"),
              "choosing a model reset Wide (%s, %s)", get(&k, "pad1_wide"), get(&k, "pad1_wide_freq"));

        /* State round trip. */
        static char blob[1 << 16];
        CHECK(dr32_state_write(&k, "", blob, sizeof blob, NULL) > 0, "state write");
        static dr32_kit r;
        dr32_kit_init(&r);
        CHECK(dr32_state_read(&r, blob, NULL, NULL), "state read");
        CHECK(!strcmp(get(&r, "pad1_wide_mode"), "Haas"), "Wide Mode did not survive a state round trip");
        CHECK(!strcmp(get(&r, "pad1_wide"), "7") && !strcmp(get(&r, "pad1_wide_freq"), "220"),
              "Wide did not survive a state round trip (%s, %s)", get(&r, "pad1_wide"), get(&r, "pad1_wide_freq"));
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
