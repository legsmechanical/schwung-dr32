/* See test_split.c: _GNU_SOURCE is the one feature macro that is additive on
 * both glibc and Darwin under -std=c11. */
#define _GNU_SOURCE

/*
 * Wide — the per-pad Haas spread (dr32_kit.c wide_run), on the Mix page with
 * its crossover, Wide Freq. What it promises, measured on the kit's output:
 *   - Wide 0 is a TRUE bypass: the pad renders bit for bit as with the stage
 *     absent, whatever Wide Freq says
 *   - full band (Wide Freq 20): + delays the RIGHT side, - the LEFT, by
 *     exactly 15 ms x (Wide/100)^2 — the CURVE, pinned at 10%, 50% and 100% —
 *     and the other side is untouched
 *   - above a crossover only: a kick's body stays centred (L and R nearly
 *     equal) while a hat's top is spread by the knob's ms
 *   - nothing is cut off: the delayed side's last ms play out after the pad
 *     stops sounding
 *   - both render entry points agree (the host switches between them at
 *     runtime, CLAUDE.md "Module buses")
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

/* A 40 ms mono WAV of a 3 kHz tone that STOPS DEAD: a sample pad goes quiet
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
    for (uint32_t i = 0; i < n; i++) w16(f, (uint16_t) (int16_t) (12000.0 * sin(i * 2 * 3.14159265 * 3000 / SR)));
    fclose(f);
}

/* The frames the knob's % must delay by: 15 ms x (pct/100)^2, rounded. */
static int frames_for(float pct) {
    const float a = fabsf(pct) * 0.01f;
    return (int) (15.0f * a * a * 0.001f * SR + 0.5f);
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

    /* ---- full band: the curve's ms, on the knob's side ------------------- */
    {
        const char *pct[] = {"10", "50", "100", "-10", "-50", "-100"};
        for (int t = 0; t < 6; t++) {
            const float v = (float) atof(pct[t]);
            const int d = frames_for(v), dch = v > 0 ? 1 : 0;
            hit(&k, "fm/snare", pct[t], "20", b);
            int ok_other = 1, ok_delayed = 1;
            for (int i = 0; i < LEN; i++) {
                if (b[2 * i + (1 - dch)] != a[2 * i + (1 - dch)]) ok_other = 0;
                const float want = i >= d ? a[2 * (i - d) + dch] : 0.0f;
                if (b[2 * i + dch] != want) ok_delayed = 0;
            }
            CHECK(ok_other, "Wide %s%% changed the %s side", pct[t], dch ? "LEFT" : "RIGHT");
            CHECK(ok_delayed, "Wide %s%% is not the %s side delayed by exactly %d frames", pct[t],
                  dch ? "right" : "left", d);
        }
        CHECK(frames_for(10) == 7 && frames_for(50) == 165 && frames_for(100) == 662,
              "the curve: %d %d %d frames, want 7 165 662", frames_for(10), frames_for(50), frames_for(100));
    }

    /* ---- nothing cut off: a short hit's delayed side plays out ---------- */
    {
        hit(&k, "fm/chat", "100", "20", b);
        const double el = energy(b, 0, 0), er = energy(b, 1, 0);
        CHECK(fabs(er / el - 1.0) < 1e-4, "the delayed side lost its end: R/L energy %.6f", er / el);
        CHECK(!dr32_pad_sounding(&k.pads[0]) && k.pads[0].wide.tail <= 0,
              "the stage did not finish after the hit (tail %d)", k.pads[0].wide.tail);
    }

    /* ...and on a SAMPLE pad, on BOTH render paths: the sample ends abruptly,
     * so the pad stops sounding while the delayed side still has 15 ms to go. */
    {
        const char *wav = "/tmp/dr32_wide.wav";
        make_wav(wav);
        for (int path = 0; path < 2; path++) {
            dr32_kit_init(&k);
            set(&k, "pad1_sample", wav);
            set(&k, "pad1_pan", "0");
            set(&k, "pad1_hold", "60");
            set(&k, "pad1_wide", "100");
            set(&k, "pad1_wide_freq", "20");
            set(&k, "pad1_play", "127");
            static int16_t mo[2 * FR];
            for (int at = 0; at < LEN; at += FR) {
                if (!path) { dr32_kit_render(&k, b + 2 * at, FR); continue; }
                memset(mo, 0, sizeof mo);
                int16_t *vo[32] = {0};
                dr32_kit_render_split(&k, vo, 32, mo, FR);
                for (int n = 0; n < 2 * FR; n++) b[2 * at + n] = mo[n] / 32767.0f;
            }
            const double el = energy(b, 0, 0), er = energy(b, 1, 0);
            CHECK(el > 0 && fabs(er / el - 1.0) < 1e-3, "%s: a sample pad's delayed side lost its end: R/L energy %.4f",
                  path ? "split render" : "render_block", er / el);
        }
    }

    /* ---- the crossover: a kick's body stays centred --------------------- */
    {
        double side[2];
        const char *freq[] = {"20", "400"};
        for (int f = 0; f < 2; f++) {
            hit(&k, "fm/kick", "100", freq[f], b);
            double s = 0, m = 0;
            for (int i = 0; i < LEN; i++) {
                const double l = b[2 * i], r = b[2 * i + 1];
                s += (l - r) * (l - r); m += (l + r) * (l + r);
            }
            side[f] = s / m;
        }
        CHECK(side[0] > 0.1, "full-band Wide on a kick barely spreads it (side/mid %.3f)", side[0]);
        CHECK(side[1] < 0.2 * side[0], "Wide Freq 400 did not keep the kick's body centred (side/mid %.3f vs %.3f)",
              side[1], side[0]);
        printf("  kick side/mid: full band %.3f, crossover 400 Hz %.4f\n", side[0], side[1]);
    }

    /* ---- ...and a hat's top is still spread by the knob's ms ------------ */
    {
        const int d = frames_for(80);
        hit(&k, "fm/chat", "80", "150", b);
        double xy = 0, xx = 0, yy = 0;
        for (int i = d; i < LEN; i++) {
            const double l = b[2 * (i - d)];
            xy += l * b[2 * i + 1]; xx += l * l; yy += (double) b[2 * i + 1] * b[2 * i + 1];
        }
        const double lagged = xy / sqrt(xx * yy);
        CHECK(lagged > 0.95, "a hat above the crossover is not R = L delayed (corr %.3f)", lagged);
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
        set(&k, "pad1_wide", "-7");
        set(&k, "pad1_wide_freq", "220");
        set(&k, "pad1_model", "fm/kick");
        CHECK(!strcmp(get(&k, "pad1_wide"), "-7") && !strcmp(get(&k, "pad1_wide_freq"), "220"),
              "choosing a model reset Wide (%s, %s)", get(&k, "pad1_wide"), get(&k, "pad1_wide_freq"));

        /* State round trip. */
        static char blob[1 << 16];
        CHECK(dr32_state_write(&k, "", blob, sizeof blob, NULL) > 0, "state write");
        static dr32_kit r;
        dr32_kit_init(&r);
        CHECK(dr32_state_read(&r, blob, NULL, NULL), "state read");
        CHECK(!strcmp(get(&r, "pad1_wide"), "-7") && !strcmp(get(&r, "pad1_wide_freq"), "220"),
              "Wide did not survive a state round trip (%s, %s)", get(&r, "pad1_wide"), get(&r, "pad1_wide_freq"));
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
