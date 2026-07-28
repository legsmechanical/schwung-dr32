// FX bus tests — the send buses and the effect algorithms they run.
//
// These exist because "it isn't doing anything" and "it thins the low end" are
// both measurable, and were both true of the first Drum Bus: Transients used a
// level-dependent difference that vanished on quiet material, and Crunch was a
// band-split that audibly removed lows.

#include "../dsp/dr32_fxbus.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define SR 44100
#define N  (SR / 2)
/* The delay tests need room for a 4-sixteenth repeat (0.667 s at 90 BPM) and,
 * for ping-pong, its crossfed second pass — well past N. */
#define ND (SR * 3 / 2)


/** A drum-like hit: fast attack, exponential tail, at `freq` Hz. */
static void hit(float *out, int n, float freq, float amp) {
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)SR;
        float env = expf(-t * 18.0f);
        float v = amp * env * sinf(2.0f * (float)M_PI * freq * t);
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
}

static float peak_range(const float *x, int from, int to) {
    float p = 0.0f;
    for (int i = from; i < to; i++) { float a = fabsf(x[2 * i]); if (a > p) p = a; }
    return p;
}

static float rms_range(const float *x, int from, int to) {
    double s = 0; int n = 0;
    for (int i = from; i < to; i++) { s += (double)x[2 * i] * x[2 * i]; n++; }
    return n ? (float)sqrt(s / n) : 0.0f;
}

/** Run a signal through an effect and return the 100% WET result.
 *
 *  Routed through a send bus with return 1.0, which IS fully wet by design, so
 *  this measures the algorithm itself. (It used to run through an insert slot at
 *  mix=1.0 — sample-equivalent, but kit inserts are gone.) */
// p4 is pre-delay for the reverbs and TONE for the Delay; p5 is the Delay's
// ping-pong and unused by everything else.
static void run_wet_p(dr32_efx_type type, const float *p, const float *in,
                      float *out, int n, float bpm) {
    dr32_fxbus *fx = dr32_fxbus_create(SR);
    dr32_fxbus_set_send_type(fx, 0, type);
    dr32_fxbus_set_bpm(fx, bpm);
    dr32_fxbus_set_send_params(fx, 0, p, DR32_SEND_PARAMS);
    dr32_fxbus_set_send_return(fx, 0, 1.0f);
    memset(out, 0, sizeof(float) * 2 * (size_t)n);
    for (int p = 0; p < n; p += 128) {
        int m = (p + 128 <= n) ? 128 : (n - p);
        for (int i = 0; i < m; i++)
            dr32_fxbus_send(fx, 0, i, in[2 * (p + i)], in[2 * (p + i) + 1]);
        dr32_fxbus_process(fx, out + 2 * p, m);
    }
    dr32_fxbus_destroy(fx);
}

/* Synced delay / reverb: slots 0-4, sync ON, free times unused. */
static void run_wet5(dr32_efx_type type, float p1, float p2, float p3, float p4,
                     float p5, const float *in, float *out, int n, float bpm) {
    const float p[DR32_SEND_PARAMS] = { p1, p2, p3, p4, p5, 1.0f, 125.0f, 500.0f };
    run_wet_p(type, p, in, out, n, bpm);
}

/* FREE-running delay: the times are milliseconds and the tempo is irrelevant,
 * which is the whole point — the bpm passed here is deliberately absurd so a
 * test that still tracks it fails loudly. */
static void run_delay_free(float msL, float msR, float fb, float tone, float pp,
                           const float *in, float *out, int n, float bpm) {
    const float p[DR32_SEND_PARAMS] = { 1.0f, 4.0f, fb, tone, pp, 0.0f, msL, msR };
    run_wet_p(DR32_EFX_DELAY, p, in, out, n, bpm);
}

static void run_wet4(dr32_efx_type type, float p1, float p2, float p3, float p4,
                     const float *in, float *out, int n) {
    run_wet5(type, p1, p2, p3, p4, 0.0f, in, out, n, 120.0f);
}

static void run_wet(dr32_efx_type type, float p1, float p2, float p3,
                    const float *in, float *out, int n) {
    run_wet4(type, p1, p2, p3, 0.0f, in, out, n);
}

/** Run a signal through the always-on Drum Bus.
 *
 *  ⚠ NOT a send. The bus is a fixed stage over the SUMMED mix, so the signal
 *  goes into the block that dr32_fxbus_process() is handed — the same place the
 *  dry pads and the send returns have already landed by the time it runs. An
 *  earlier version of these tests drove it as a send type, which no longer
 *  exists.
 *
 *  Attack and Sustain are BIPOLAR -1..+1 with neutral at 0 (the 0..1-about-0.5
 *  form lives inside DrumBuss and nowhere above it). `mix` is the parallel
 *  blend; 1 = fully processed. */
static void run_bus5(float comp, float crunch, float attack, float sustain,
                     float mix, const float *in, float *out, int n) {
    dr32_fxbus *fx = dr32_fxbus_create(SR);
    dr32_fxbus_set_bus_params(fx, comp, crunch, attack, sustain, mix);
    for (int p = 0; p < n; p += 128) {
        int m = (p + 128 <= n) ? 128 : (n - p);
        memcpy(out + 2 * p, in + 2 * p, sizeof(float) * 2 * (size_t)m);
        dr32_fxbus_process(fx, out + 2 * p, m);
    }
    dr32_fxbus_destroy(fx);
}

static void run_bus(float comp, float crunch, float attack, float sustain,
                    const float *in, float *out, int n) {
    run_bus5(comp, crunch, attack, sustain, 1.0f, in, out, n);
}

/* RT60-ish: time for the tail to fall 60 dB below its own early peak. Used by
 * the default-length, decay-range and gate tests, so they cannot disagree. */
static float rt60_default(dr32_efx_type t, const float *p) {
    static float imp[2 * (SR * 8)], out[2 * (SR * 8)];
    const int NR = SR * 8;
    memset(imp, 0, sizeof(float) * 2 * (size_t)NR);
    for (int i = 0; i < 32; i++) { imp[2 * i] = 0.7f; imp[2 * i + 1] = 0.7f; }
    run_wet_p(t, p, imp, out, NR, 120.0f);
    float peak = 0.0f;
    int last = 0;
    for (int i = 0; i < SR / 4; i++) { float a = fabsf(out[2 * i]); if (a > peak) peak = a; }
    for (int i = 0; i < NR; i++) if (fabsf(out[2 * i]) > peak * 0.001f) last = i;
    return (float)last / (float)SR;
}

/* EVERY reverb-ish type, in one place. The stereo, mono-fold, decay-range and
 * default-RT60 tests all loop over this, so adding a type to the picker without
 * adding it here is the one way to get an untested effect — and each of those
 * properties has been a real defect in this module at least once. */
static const struct { dr32_efx_type t; const char *n; } kVerbs[] = {
    { DR32_EFX_PLATE,   "Plate"   },
    { DR32_EFX_SPACES,  "Spaces"  },
    { DR32_EFX_GATED,   "Gated"   },
    { DR32_EFX_DIGITAL, "Digital" },
    { DR32_EFX_HALL,    "Hall"    },
    { DR32_EFX_NONLIN,  "NonLin"  },
};
#define NVERBS ((int)(sizeof(kVerbs) / sizeof(kVerbs[0])))

int main(void) {
    printf("fx bus\n");
    static float dry[2 * N], wet[2 * N], wet2[2 * N];

    // ---- Drum Bus / Attack must actually change the attack-to-tail balance
    {
        hit(dry, N, 120.0f, 0.4f);
        const int atk_from = 0, atk_to = SR / 200;          // first 5 ms
        const int tail_from = SR / 20, tail_to = SR / 5;    // 50-200 ms

        run_bus(0.0f, 0.0f, 0.0f, 0.0f, dry, wet, N);
        float n_atk = rms_range(wet, atk_from, atk_to), n_tail = rms_range(wet, tail_from, tail_to);
        CHECK(n_tail > 1e-6f, "neutral attack produced no tail");
        float neutral = n_atk / (n_tail + 1e-9f);

        run_bus(0.0f, 0.0f, 1.0f, 0.0f, dry, wet, N);
        float s_atk = rms_range(wet, atk_from, atk_to), s_tail = rms_range(wet, tail_from, tail_to);
        float sharp = s_atk / (s_tail + 1e-9f);

        run_bus(0.0f, 0.0f, -1.0f, 0.0f, dry, wet, N);
        float f_atk = rms_range(wet, atk_from, atk_to), f_tail = rms_range(wet, tail_from, tail_to);
        float soft = f_atk / (f_tail + 1e-9f);

        printf("  attack knob attack:tail  soft %.3f  neutral %.3f  sharp %.3f\n", soft, neutral, sharp);
        CHECK(sharp > neutral * 1.15f, "Attack up did not sharpen: %.3f vs %.3f", sharp, neutral);
        CHECK(soft < neutral * 0.87f, "Attack down did not soften: %.3f vs %.3f", soft, neutral);
    }

    // ---- Crunch must saturate WITHOUT reshaping the spectrum.
    //      Drive a low tone and a high tone; both should survive in proportion.
    {
        static float low[2 * N], high[2 * N];
        hit(low, N, 80.0f, 0.4f);
        hit(high, N, 4000.0f, 0.4f);

        run_bus(0.0f, 0.0f, 0.0f, 0.0f, low, wet, N);
        float low_dry = rms_range(wet, 0, SR / 10);
        run_bus(0.0f, 0.8f, 0.0f, 0.0f, low, wet, N);
        float low_crunch = rms_range(wet, 0, SR / 10);

        run_bus(0.0f, 0.0f, 0.0f, 0.0f, high, wet2, N);
        float high_dry = rms_range(wet2, 0, SR / 10);
        run_bus(0.0f, 0.8f, 0.0f, 0.0f, high, wet2, N);
        float high_crunch = rms_range(wet2, 0, SR / 10);

        float low_ratio = low_crunch / (low_dry + 1e-9f);
        float high_ratio = high_crunch / (high_dry + 1e-9f);
        printf("  crunch level ratio      low %.3f  high %.3f\n", low_ratio, high_ratio);

        // The old band-split version failed this: lows dropped while highs rose.
        CHECK(low_ratio > 0.7f, "Crunch cut the low end to %.2f of dry — it must not shape EQ", low_ratio);
        float tilt = high_ratio / (low_ratio + 1e-9f);
        CHECK(tilt > 0.6f && tilt < 1.7f,
              "Crunch tilted the spectrum: high/low gain ratio %.2f (want ~1)", tilt);
    }

    // ---- every reverb type must actually produce a tail (Hall was silent)
    {
        static float imp[2 * N];
        memset(imp, 0, sizeof(imp));
        for (int i = 0; i < 64; i++) { imp[2 * i] = 0.6f; imp[2 * i + 1] = 0.6f; }

        for (int k = 0; k < NVERBS; k++) {
            run_wet(kVerbs[k].t, 0.5f, 0.3f, 0.6f, imp, wet, N);
            float tail = rms_range(wet, SR / 20, SR / 4);   // 50-250 ms after the hit
            printf("  %-7s tail rms %.6f\n", kVerbs[k].n, tail);
            CHECK(tail > 1e-5f, "%s produced no tail (rms %.8f) — silent reverb", kVerbs[k].n, tail);
        }
    }

    // ---- a send bus returns signal, and its return level scales it
    {
        dr32_fxbus *fx = dr32_fxbus_create(SR);
        dr32_fxbus_set_send_type(fx, 0, DR32_EFX_PLATE);
        { const float pp5[5] = { 0.5f, 0.3f, 0.6f, 0.0f, 0.0f };
          dr32_fxbus_set_send_params(fx, 0, pp5, 5); }
        dr32_fxbus_set_send_return(fx, 0, 1.0f);

        float acc = 0.0f;
        for (int p = 0; p < N; p += 128) {
            float blk[2 * 128];
            memset(blk, 0, sizeof(blk));
            for (int i = 0; i < 128 && p + i < N; i++) {
                float v = (p + i < 64) ? 0.6f : 0.0f;
                dr32_fxbus_send(fx, 0, i, v, v);
            }
            dr32_fxbus_process(fx, blk, 128);
            for (int i = 0; i < 128; i++) acc += fabsf(blk[2 * i]);
        }
        CHECK(acc > 1e-3f, "send bus returned nothing (%.6f)", acc);
        dr32_fxbus_destroy(fx);
    }

    // ---- a send write must land on its OWN frame within the block.
    //
    //      The bug this catches: every send write landed on frame 0, so the bus
    //      saw one impulse per block — an impulse train at the block rate
    //      (~344 Hz) that rang metallically. A weaker test that merely asserted
    //      "the send returns something" passed the whole time.
    //
    //      This used to be checked by comparing against a fully wet INSERT,
    //      which was sample-equivalent. Inserts are gone, and comparing the send
    //      path against itself would prove nothing — so instead reproduce the
    //      bug deliberately (collapse every write onto frame 0) and require the
    //      correct path to differ MATERIALLY from it. If these two ever agree,
    //      frame placement is being ignored again.
    {
        static float sig[2 * N], correct[2 * N], collapsed[2 * N];
        hit(sig, N, 200.0f, 0.5f);

        for (int mode = 0; mode < 2; mode++) {
            float *out = mode ? collapsed : correct;
            dr32_fxbus *fx = dr32_fxbus_create(SR);
            dr32_fxbus_set_send_type(fx, 0, DR32_EFX_PLATE);
            { const float pp5[5] = { 0.5f, 0.3f, 0.5f, 0.0f, 0.0f };
              dr32_fxbus_set_send_params(fx, 0, pp5, 5); }
            dr32_fxbus_set_send_return(fx, 0, 1.0f);
            memset(out, 0, sizeof(float) * 2 * N);
            for (int p = 0; p + 128 <= N; p += 128) {
                for (int i = 0; i < 128; i++)
                    dr32_fxbus_send(fx, 0, mode ? 0 : i,
                                    sig[2 * (p + i)], sig[2 * (p + i) + 1]);
                dr32_fxbus_process(fx, out + 2 * p, 128);
            }
            dr32_fxbus_destroy(fx);
        }

        double num = 0.0, den = 0.0;
        for (int i = 0; i < 2 * (N - 128); i++) {
            double d = (double)correct[i] - collapsed[i];
            num += d * d;
            den += (double)correct[i] * correct[i];
        }
        double diff_db = (den > 0) ? 10.0 * log10((num + 1e-30) / den) : -300.0;
        printf("  frame placement vs collapsed-to-frame-0: %.1f dB\n", diff_db);
        CHECK(den > 0.0, "send bus produced nothing to compare");
        CHECK(diff_db > -6.0,
              "collapsing sends onto frame 0 barely changed the output (%.1f dB) "
              "— frame placement is being ignored", diff_db);
    }

    // ---- per-type defaults are musical AND distinct from each other
    {
        float pl[DR32_SEND_PARAMS], rm[DR32_SEND_PARAMS],
              hl[DR32_SEND_PARAMS], dl[DR32_SEND_PARAMS];
        dr32_efx_defaults(DR32_EFX_PLATE, pl);
        dr32_efx_defaults(DR32_EFX_SPACES, rm);
        dr32_efx_defaults(DR32_EFX_SPACES, hl);
        dr32_efx_defaults(DR32_EFX_DELAY, dl);

        // Deliberately NOT comparing knob values across types: Plate is the
        // Dattorro tank and Room/Hall are Chamber, so 0.40 on one is not
        // comparable to 0.45 on another. What matters is the RESULT, measured
        // below. Pre-delay is on the same scale for all of them, so that one is
        // comparable.
        // Pre-delay KNOB is no longer comparable either: kCosmos (Hall) carries
        // about 78 ms of onset of its own, inherent and independent of decay,
        // where kWoodRoom (Room) starts at 3.2 ms. Stacking a big knob value on
        // top of the hall would only push it further from the hit. What has to
        // hold is the RESULT -- measured onset -- so assert that instead.
        CHECK(hl[3] >= 0.0f && rm[3] >= 0.0f, "pre-delay defaults must be sane");
        for (int i = 0; i < 4; i++) {
            CHECK(pl[i] >= 0.0f && pl[i] <= 1.0f, "plate default %d out of range", i);
            CHECK(hl[i] >= 0.0f && hl[i] <= 1.0f, "hall default %d out of range", i);
        }
        // The Delay's first two slots are SIXTEENTHS, not 0..1, so the range
        // check above does not apply to them — this is the assertion that
        // catches someone "tidying" them into normalised values.
        CHECK(dl[0] >= 1.0f && dl[0] <= 16.0f,
              "delay time L default %.2f is not a sixteenth count (1..16)", dl[0]);
        CHECK(dl[1] >= 1.0f && dl[1] <= 16.0f,
              "delay time R default %.2f is not a sixteenth count (1..16)", dl[1]);
        // L != R is the whole point: ten of the twelve native delay returns run
        // L short against R = 4 sixteenths, and that asymmetry is the sound.
        CHECK(dl[0] != dl[1], "delay defaults have L == R (%.1f) — no stereo pattern", dl[0]);
        CHECK(dl[2] > 0.0f && dl[2] < 1.0f, "delay feedback default %.2f out of range", dl[2]);
    }

    // ---- RT60 at the defaults: audible, and never enormous.
    //      Hall once measured >6 s at its defaults and swamped the kit (07ca02c),
    //      which is why it was pulled from the module entirely; it is back now
    //      with its decay scaled, and this is the check that keeps it honest.
    {
        for (int k = 0; k < NVERBS; k++) {
            float d[DR32_SEND_PARAMS];
            dr32_efx_defaults(kVerbs[k].t, d);
            float rt = rt60_default(kVerbs[k].t, d);
            printf("  %-7s default RT60 %.2f s\n", kVerbs[k].n, rt);
            CHECK(rt > 0.1f, "%s default decay %.2f s is inaudibly short", kVerbs[k].n, rt);
            CHECK(rt < 3.0f, "%s default decay %.2f s is too big for a drum kit",
                  kVerbs[k].n, rt);
        }
    }

    // ---- ONSET: the reverb must arrive with the hit, not a beat later.
    //
    //      The plate was silent for its first 44 ms — at 90 BPM that is a third
    //      of a beat after the drum, and it read as a pre-delay nobody asked
    //      for. Every output tap read a TANK line, the shortest at 0.19 of
    //      len_d1, while the four input diffusers (3.6-12.7 ms) were never
    //      tapped at all. They are now, and this is the check that keeps them.
    //
    //      Hall and Digital are deliberately allowed more room: a hall with a
    //      40 ms build is a hall, not a defect. The bar here is "not absurd".
    {
        static float src[2 * N];
        memset(src, 0, sizeof(src));
        for (int i = 0; i < 64; i++) {
            float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / 63.0f));
            src[2 * i] = 0.8f * w; src[2 * i + 1] = 0.8f * w;
        }
        for (int k = 0; k < NVERBS; k++) {
            float d[DR32_SEND_PARAMS];
            dr32_efx_defaults(kVerbs[k].t, d);
            d[3] = 0.0f;                       /* pre-delay off — measure the ALGORITHM */
            run_wet_p(kVerbs[k].t, d, src, wet, N, 120.0f);
            float pk = 0.0f;
            for (int i = 0; i < N; i++) { float a = fabsf(wet[2 * i]); if (a > pk) pk = a; }
            int first = -1;
            for (int i = 0; i < N; i++) if (fabsf(wet[2 * i]) > pk * 0.02f) { first = i; break; }
            float ms = (first < 0) ? 9999.0f : 1000.0f * (float)first / (float)SR;
            printf("  %-7s first arrival %5.1f ms\n", kVerbs[k].n, ms);
            const float limit = (kVerbs[k].t == DR32_EFX_HALL ||
                                 kVerbs[k].t == DR32_EFX_DIGITAL) ? 60.0f : 20.0f;
            CHECK(ms < limit,
                  "%s does not arrive until %.1f ms — that is a pre-delay, not a reverb",
                  kVerbs[k].n, ms);
        }
    }

    // ---- the DECAY KNOB must actually travel.
    //
    //      This is the general form of a defect found while adding these models:
    //      SpaceExtra's LoFi type has an RT60 of 0.19 s at decay 0 and 0.26 s at
    //      decay 1 — the effect works, the control does not, and raising its bit
    //      depth 7 -> 11 only reached 0.41 s, so the ceiling is structural. It
    //      was dropped from the picker rather than shipped with a dead knob.
    //      Anything offered has to answer this.
    {
        for (int k = 0; k < NVERBS; k++) {
            float d[DR32_SEND_PARAMS];
            dr32_efx_defaults(kVerbs[k].t, d);
            d[2] = 0.0f;  float lo = rt60_default(kVerbs[k].t, d);
            d[2] = 1.0f;  float hi = rt60_default(kVerbs[k].t, d);
            printf("  %-7s decay knob: %.2f s -> %.2f s (x%.1f)\n",
                   kVerbs[k].n, lo, hi, hi / (lo + 1e-6f));
            CHECK(hi > lo * 1.5f,
                  "%s decay knob is dead: %.2f s at 0 vs %.2f s at 1", kVerbs[k].n, lo, hi);
        }
    }

    // ---- NONLIN must be NONLINEAR: a flat window, not a decay.
    //
    //      This is the whole difference between it and the Gated type sitting
    //      next to it in the picker. A gate chops an exponential decay, so the
    //      level is always falling underneath it. NonLin overrides the decay:
    //      the window holds a roughly constant level and then stops dead. If
    //      this test ever passes for the plate as well, the type is not earning
    //      its slot.
    {
        /* ⚠ A SHORT burst, not hit(). hit() keeps feeding the reverb for
         * hundreds of ms, so a plate's level tracks the input instead of
         * decaying and the comparison below measures nothing. The burst has to
         * be long enough to trigger (a bare impulse is not a transient to a
         * 0.5 ms follower) and short enough that everything after it is tail. */
        static float src[2 * N];
        memset(src, 0, sizeof(src));
        for (int i = 0; i < 64; i++) {
            float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / 63.0f));
            src[2 * i] = 0.8f * w; src[2 * i + 1] = 0.8f * w;
        }

        float d[DR32_SEND_PARAMS];
        dr32_efx_defaults(DR32_EFX_NONLIN, d);
        d[2] = 0.45f;  d[4] = 0.5f;               /* ~300 ms, dead flat */
        run_wet_p(DR32_EFX_NONLIN, d, src, wet, N, 120.0f);

        /* compare the same two slices well inside the window */
        const int e0 = SR * 40 / 1000, e1 = SR * 80 / 1000;     /*  40- 80 ms */
        const int l0 = SR * 180 / 1000, l1 = SR * 220 / 1000;   /* 180-220 ms */
        float nlEarly = rms_range(wet, e0, e1), nlLate = rms_range(wet, l0, l1);
        float nlDrop = 20.0f * log10f(nlLate / (nlEarly + 1e-12f) + 1e-12f);

        float pd[DR32_SEND_PARAMS];
        dr32_efx_defaults(DR32_EFX_PLATE, pd);
        run_wet_p(DR32_EFX_PLATE, pd, src, wet2, N, 120.0f);
        float plEarly = rms_range(wet2, e0, e1), plLate = rms_range(wet2, l0, l1);
        float plDrop = 20.0f * log10f(plLate / (plEarly + 1e-12f) + 1e-12f);

        printf("  nonlin window %+.1f dB over 40->200 ms   (plate %+.1f dB)\n",
               nlDrop, plDrop);
        CHECK(nlEarly > 1e-5f, "nonlin produced nothing inside its window");
        CHECK(fabsf(nlDrop) < 6.0f,
              "nonlin window fell %+.1f dB — it is decaying, not holding", nlDrop);
        /* Flatness is the ABSOLUTE deviation, not a signed drop. The plate
         * does not fall across this span — it RISES about 4 dB, because its
         * first arrival is ~40 ms and it is still building density at 80 ms.
         * Comparing signed values would have called a rising plate "flatter"
         * than a flat window. */
        CHECK(fabsf(nlDrop) < fabsf(plDrop) - 2.0f,
              "nonlin deviates %+.1f dB across its window against the plate's "
              "%+.1f dB — it is not holding a level the way a plate does not",
              nlDrop, plDrop);

        /* The cliff. ⚠ Stay inside the buffer: N is 500 ms, and reading to
         * 700 ms ran off the end of `wet` into the next static array — which
         * reported a healthy signal past a window that had actually closed. */
        float after = rms_range(wet, SR * 350 / 1000, N);
        float cliff = 20.0f * log10f(after / (nlEarly + 1e-12f) + 1e-12f);
        printf("  nonlin cliff %.1f dB\n", cliff);
        CHECK(cliff < -60.0f, "nonlin does not stop dead (%.1f dB)", cliff);

        /* it must START with the hit — the tank alone arrives 19-53 ms late,
         * which is a hole exactly where this effect lives */
        float first = rms_range(wet, 0, SR * 5 / 1000);        /* first 5 ms */
        CHECK(first > nlEarly * 0.2f,
              "nonlin is silent for the first 5 ms (%.6f vs %.6f in the window) — "
              "the early energy is missing", first, nlEarly);

        /* shape: rising must end higher than falling */
        d[4] = 1.0f; run_wet_p(DR32_EFX_NONLIN, d, src, wet, N, 120.0f);
        float up = 20.0f * log10f(rms_range(wet, l0, l1) / (rms_range(wet, e0, e1) + 1e-12f) + 1e-12f);
        d[4] = 0.0f; run_wet_p(DR32_EFX_NONLIN, d, src, wet, N, 120.0f);
        float dn = 20.0f * log10f(rms_range(wet, l0, l1) / (rms_range(wet, e0, e1) + 1e-12f) + 1e-12f);
        printf("  nonlin shape: falling %+.1f dB, rising %+.1f dB\n", dn, up);
        CHECK(up > dn + 6.0f, "shape knob did nothing (%+.1f vs %+.1f dB)", dn, up);
    }

    // ---- GATED and NONLIN must not be the same effect.
    //
    //      They sit next to each other in the picker and both end a window, so
    //      "is there any real difference?" is the right question — and the first
    //      answer was NO. At equal window lengths the gated type ROSE +6 dB
    //      across its whole hold and stayed there, because its tank decay was
    //      pinned at 0.72 and the tank's density build dominates the first
    //      ~180 ms. It was a flat window with a chop, i.e. NonLin.
    //
    //      Envelope SHAPE turned out to be the wrong thing to assert: with the
    //      gate's decay exposed (slot 4) and NonLin's Shape control, their
    //      ranges legitimately overlap — a gate at full tail measures -2.8 dB
    //      peak-to-end and a rising NonLin -2.7 dB. Tuning one to differ from
    //      the other would only be true until someone turned a knob.
    //
    //      What separates them is STRUCTURAL and survives any setting: the
    //      gate's hold RE-ARMS for as long as the input stays above threshold,
    //      so on sustained material it simply stays open; NonLin runs a fixed
    //      window from a TRANSIENT and then cuts, and a continuous source never
    //      gives it a new transient to fire on.
    //
    //      ⚠ The lesson is the test, not the tuning: a new type has to be
    //      measured against its NEIGHBOUR. NonLin passed a flatness test against
    //      the PLATE and shipped anyway, because the plate was never the thing
    //      it could be confused with.
    {
        static float sus[2 * N];
        unsigned rr = 12345u;
        for (int i = 0; i < N; i++) {                 /* sustained, no transients */
            rr = rr * 1664525u + 1013904223u;
            float v = 0.25f * ((float)(int)rr / 2147483648.0f);
            sus[2 * i] = v; sus[2 * i + 1] = v;
        }
        float gp[DR32_SEND_PARAMS], np[DR32_SEND_PARAMS];
        dr32_efx_defaults(DR32_EFX_GATED, gp);  gp[2] = 0.45f;
        dr32_efx_defaults(DR32_EFX_NONLIN, np); np[2] = 0.36f;

        run_wet_p(DR32_EFX_GATED, gp, sus, wet, N, 120.0f);
        float gEarly = rms_range(wet, 0, SR / 10);
        float gLate  = rms_range(wet, SR * 4 / 10, N);
        run_wet_p(DR32_EFX_NONLIN, np, sus, wet2, N, 120.0f);
        float nEarly = rms_range(wet2, 0, SR / 10);
        float nLate  = rms_range(wet2, SR * 4 / 10, N);

        float gDb = 20.0f * log10f(gLate / (gEarly + 1e-12f) + 1e-12f);
        float nDb = 20.0f * log10f(nLate / (nEarly + 1e-12f) + 1e-12f);
        printf("  sustained input, late vs early:  Gated %+.1f dB   NonLin %+.1f dB\n",
               gDb, nDb);
        CHECK(gDb > -6.0f,
              "Gated shut on sustained input (%+.1f dB) — its hold must re-arm "
              "while the source is above threshold", gDb);
        CHECK(nDb < -40.0f,
              "NonLin stayed open on sustained input (%+.1f dB) — its window is "
              "a fixed length from a transient, not a gate that follows the "
              "material", nDb);
    }

    // ---- the GATE must actually gate.
    //      Its hold knob sets how long the tail survives before it is chopped,
    //      so the tail has to END, and end EARLIER at a shorter hold. A plate
    //      with a decay knob would pass "shorter is shorter" too, which is why
    //      the second check looks for the chop: past the hold there must be
    //      essentially nothing left, not a fade.
    {
        static float imp[2 * N];
        memset(imp, 0, sizeof(imp));
        for (int i = 0; i < 64; i++) { imp[2 * i] = 0.6f; imp[2 * i + 1] = 0.6f; }

        float d[DR32_SEND_PARAMS];
        dr32_efx_defaults(DR32_EFX_GATED, d);
        d[2] = 0.15f;  float shortHold = rt60_default(DR32_EFX_GATED, d);
        d[2] = 0.90f;  float longHold  = rt60_default(DR32_EFX_GATED, d);
        printf("  gate hold: %.2f s -> %.2f s\n", shortHold, longHold);
        CHECK(longHold > shortHold * 1.5f,
              "gate hold did not lengthen the tail (%.2f -> %.2f s)", shortHold, longHold);

        /* the chop: energy well past the hold must be far below the tail's own */
        d[2] = 0.15f;
        run_wet_p(DR32_EFX_GATED, d, imp, wet, N, 120.0f);
        float inside = rms_range(wet, SR / 50, SR / 12);       /* 20-83 ms */
        float after  = rms_range(wet, SR / 3, N);              /* past 333 ms */
        float drop = 20.0f * log10f(after / (inside + 1e-12f) + 1e-12f);
        printf("  gate chop: %.1f dB from inside the gate to past it\n", drop);
        CHECK(drop < -40.0f,
              "the gated reverb does not close (%.1f dB) — it is just a reverb", drop);
    }

    // ---- an idle send bus must stop costing CPU, but only after its tail
    {
        dr32_fxbus *fx = dr32_fxbus_create(SR);
        dr32_fxbus_set_send_type(fx, 0, DR32_EFX_PLATE);
        { const float pp5[5] = { 0.5f, 0.3f, 0.6f, 0.0f, 0.0f };
          dr32_fxbus_set_send_params(fx, 0, pp5, 5); }
        dr32_fxbus_set_send_return(fx, 0, 1.0f);

        float blk[2 * 128];
        /* one hit, then silence */
        for (int i = 0; i < 128; i++) dr32_fxbus_send(fx, 0, i, 0.6f, 0.6f);
        memset(blk, 0, sizeof(blk));
        dr32_fxbus_process(fx, blk, 128);

        float tail_early = 0.0f;
        for (int b = 0; b < 20; b++) {          /* ~58 ms later: tail must survive */
            memset(blk, 0, sizeof(blk));
            dr32_fxbus_process(fx, blk, 128);
            for (int i = 0; i < 128; i++) tail_early += fabsf(blk[2 * i]);
        }
        CHECK(tail_early > 1e-4f, "idle-skip cut the reverb tail off (%.6f)", tail_early);

        /* run well past the idle threshold, then measure the SAME window size
         * as tail_early — comparing a 3000-block sum with a 20-block sum would
         * just measure the block count. */
        for (int b = 0; b < 3000; b++) {
            memset(blk, 0, sizeof(blk));
            dr32_fxbus_process(fx, blk, 128);
        }
        float tail_late = 0.0f;
        for (int b = 0; b < 20; b++) {
            memset(blk, 0, sizeof(blk));
            dr32_fxbus_process(fx, blk, 128);
            for (int i = 0; i < 128; i++) tail_late += fabsf(blk[2 * i]);
        }
        CHECK(tail_late < tail_early * 0.01f,
              "bus still ringing long after silence (%.6f vs %.6f)", tail_late, tail_early);
        dr32_fxbus_destroy(fx);
    }


    // ---- Compress must NEVER lift quiet material.
    //      This is the defect that got Pressure4 replaced: it had no threshold,
    //      so it pulled a -48 dBFS signal up by 17.7 dB — sample noise floor,
    //      room bleed and reverb tails along with it. Pop3 can only attenuate,
    //      and the makeup gain is measured per block and gated on real signal,
    //      so the whole chain must stay silent on a quiet input.
    {
        static float quiet[2 * N];
        for (int i = 0; i < N; i++) {
            float v = 0.00398f * sinf(2.0f * (float)M_PI * 200.0f * i / SR);  // -48 dBFS
            quiet[2 * i] = v; quiet[2 * i + 1] = v;
        }
        float in_rms = rms_range(quiet, N / 2, N);
        float worst = 0.0f;
        for (int k = 0; k <= 4; k++) {
            run_bus(k * 0.25f, 0.0f, 0.0f, 0.0f, quiet, wet, N);
            float lift = 20.0f * log10f(rms_range(wet, N / 2, N) / (in_rms + 1e-12f) + 1e-12f);
            if (fabsf(lift) > fabsf(worst)) worst = lift;
        }
        printf("  compress low-level lift  worst %+.2f dB over the whole knob\n", worst);
        CHECK(fabsf(worst) < 1.0f,
              "Compress lifted a -48 dBFS signal by %+.2f dB — it must have a real threshold", worst);
    }

    // ---- Attack and Sustain must be ORTHOGONAL.
    //      The stage these replaced applied one broadband gain, so its "sustain"
    //      direction dragged the attack with it. Sustain must move the tail and
    //      leave the hit alone.
    {
        hit(dry, N, 60.0f, 0.4f);
        const int atk_to = SR / 125;                       // first 8 ms
        const int t_from = SR * 8 / 100, t_to = SR / 4;     // 80-250 ms

        run_bus(0.0f, 0.0f, 0.0f, 0.0f, dry, wet, N);
        float n_atk = peak_range(wet, 0, atk_to), n_tail = rms_range(wet, t_from, t_to);

        run_bus(0.0f, 0.0f, 0.0f, 1.0f, dry, wet, N);
        float up_atk = peak_range(wet, 0, atk_to), up_tail = rms_range(wet, t_from, t_to);
        run_bus(0.0f, 0.0f, 0.0f, -1.0f, dry, wet, N);
        float dn_atk = peak_range(wet, 0, atk_to), dn_tail = rms_range(wet, t_from, t_to);

        float up_t = 20.0f * log10f(up_tail / (n_tail + 1e-12f) + 1e-12f);
        float dn_t = 20.0f * log10f(dn_tail / (n_tail + 1e-12f) + 1e-12f);
        float up_a = 20.0f * log10f(up_atk / (n_atk + 1e-12f) + 1e-12f);
        float dn_a = 20.0f * log10f(dn_atk / (n_atk + 1e-12f) + 1e-12f);
        printf("  sustain knob   tail %+.2f / %+.2f dB   attack %+.2f / %+.2f dB\n",
               dn_t, up_t, dn_a, up_a);
        CHECK(up_t > 4.0f,  "Sustain up did not lengthen the tail (%+.2f dB)", up_t);
        CHECK(dn_t < -4.0f, "Sustain down did not shorten the tail (%+.2f dB)", dn_t);
        CHECK(fabsf(up_a) < 1.0f && fabsf(dn_a) < 1.0f,
              "Sustain moved the ATTACK (%+.2f / %+.2f dB) — it must only shape the decay",
              dn_a, up_a);
    }

    // ---- The reverbs must be STEREO.
    //      Chamber, which Room and Hall used to share, ran as two independent
    //      mono reverbs: an L-only impulse put -107 dB in the right channel, so
    //      centre-panned pads collapsed the whole send to mono.
    {
        for (int t = 0; t < NVERBS; t++) {
            const dr32_efx_type *types = &kVerbs[t].t;
            const char *names[1] = { kVerbs[t].n };
            (void)types;
            static float imp[2 * N];
            memset(imp, 0, sizeof(imp));
            imp[0] = 1.0f;                                  // LEFT only
            run_wet4(kVerbs[t].t, 0.5f, 0.3f, 0.5f, 0.0f, imp, wet, N);
            double el = 0, er = 0;
            for (int i = 0; i < N; i++) {
                el += (double)wet[2 * i] * wet[2 * i];
                er += (double)wet[2 * i + 1] * wet[2 * i + 1];
            }
            float rl = 10.0f * log10f((float)(er / (el + 1e-30)) + 1e-30f);
            /* correlation of the two outputs from a MONO feed -- the thing that
             * actually broke before: Chamber produced bit-identical L and R. */
            static float imp2[2 * N];
            memset(imp2, 0, sizeof(imp2));
            imp2[0] = 1.0f; imp2[1] = 1.0f;
            run_wet4(kVerbs[t].t, 0.5f, 0.3f, 0.5f, 0.0f, imp2, wet2, N);
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = SR / 10; i < N; i++) {
                double x = wet2[2 * i], y = wet2[2 * i + 1];
                sxy += x * y; sxx += x * x; syy += y * y;
            }
            float corr = (sxx > 0 && syy > 0) ? (float)(sxy / sqrt(sxx * syy)) : 1.0f;
            printf("  %-7s corr %+.2f   L-only -> R/L %+.1f dB\n", names[0], corr, rl);
            /* BOTH must be decorrelated. The Plate always was. Spaces
             * (Verbity2) is symmetric internally and on its own produced
             * bit-identical L and R (corr +1.00); it is fed through the same
             * per-channel diffuser as the plate, whose two sides run different
             * prime lengths, which takes it to about +0.05. */
            CHECK(corr < 0.50f,
                  "%s produced correlated L/R (corr %+.2f) — it is running mono",
                  names[0], corr);
            CHECK(rl > -50.0f,
                  "%s put only %+.1f dB into the right channel from an L-only hit",
                  names[0], rl);
        }
    }


    // ---- The plate must damp high frequencies FASTER than low ones.
    //      Every real plate and room does; a tank with no HF loss in the
    //      feedback path rings on glassily. The damping knob always had the
    //      mechanism, but its whole useful range sat above 0.75 — at the
    //      plate's 0.35 default the HF/LF decay ratio was ~0.88, i.e. barely
    //      any. The knob is now curved, so check the default actually damps.
    {
        static float imp[2 * N];
        memset(imp, 0, sizeof(imp));
        imp[0] = 1.0f; imp[1] = 1.0f;
        run_wet4(DR32_EFX_PLATE, 0.45f, 0.35f, 0.45f, 0.0f, imp, wet, N);

        /* one-pole band energies are enough here: compare how much of the tail
         * survives at 6 kHz against 500 Hz, early window vs late window. */
        float lo_e = 0, lo_l = 0, hi_e = 0, hi_l = 0;
        float zl = 0, zh = 0;
        const float al = expf(-2.0f * (float)M_PI * 500.0f / SR);
        const float ah = expf(-2.0f * (float)M_PI * 6000.0f / SR);
        for (int i = 0; i < N; i++) {
            float x = wet[2 * i];
            zl = x * (1.0f - al) + zl * al;          /* lowpass  -> LF content */
            zh = x * (1.0f - ah) + zh * ah;
            float hf = x - zh;                        /* highpass -> HF content */
            int early = (i > SR / 20 && i < SR / 10);      /*  50-100 ms */
            int late  = (i > SR * 3 / 10 && i < SR * 4 / 10); /* 300-400 ms */
            if (early) { lo_e += zl * zl; hi_e += hf * hf; }
            if (late)  { lo_l += zl * zl; hi_l += hf * hf; }
        }
        /* how much each band fell from the early window to the late one */
        float lo_drop = 10.0f * log10f((lo_l + 1e-20f) / (lo_e + 1e-20f));
        float hi_drop = 10.0f * log10f((hi_l + 1e-20f) / (hi_e + 1e-20f));
        printf("  plate decay 50-100ms -> 300-400ms:  LF %+.1f dB   HF %+.1f dB\n",
               lo_drop, hi_drop);
        CHECK(hi_drop < lo_drop - 3.0f,
              "plate HF did not decay faster than LF (HF %+.1f dB vs LF %+.1f dB) — "
              "the tank is ringing with no HF loss", hi_drop, lo_drop);
    }


    // ---- Stereo width must survive a fold to mono.
    //      Decorrelating with allpasses is only legitimate if the two channels
    //      are genuinely independent. A phase-inversion "widener" measures wide
    //      and then largely disappears when the kit is summed, which on a drum
    //      bus is a trap. Summing two uncorrelated signals loses ~3 dB; summing
    //      two anti-correlated ones loses far more.
    {
        for (int t = 0; t < NVERBS; t++) {
            static float imp[2 * N];
            memset(imp, 0, sizeof(imp));
            imp[0] = 1.0f; imp[1] = 1.0f;
            run_wet4(kVerbs[t].t, 0.5f, 0.3f, 0.5f, 0.0f, imp, wet, N);
            double mono = 0, one = 0;
            for (int i = SR / 10; i < N; i++) {
                double m = 0.5 * (wet[2 * i] + wet[2 * i + 1]);
                mono += m * m; one += (double)wet[2 * i] * wet[2 * i];
            }
            float fold = 10.0f * log10f((float)(mono / (one + 1e-30)) + 1e-30f);
            printf("  %-7s mono-fold %+.2f dB\n", kVerbs[t].n, fold);
            CHECK(fold > -6.0f,
                  "%s loses %+.2f dB when summed to mono — the width is phase cancellation",
                  kVerbs[t].n, fold);
        }
    }


    // ---- Type names must round-trip.
    //      Types persist as strings, so name and from_name have to agree. This
    //      is NOT a backward-compatibility check: kits are a clean slate until
    //      development settles, so renaming a type needs no legacy fallback.
    {
        for (int t = 1; t < DR32_EFX_COUNT; t++) {
            const char *n = dr32_efx_name((dr32_efx_type)t);
            CHECK(dr32_efx_from_name(n) == (dr32_efx_type)t,
                  "type %d name \"%s\" does not round-trip", t, n);
        }
        CHECK(dr32_efx_from_name("Off") == DR32_EFX_NONE, "\"Off\" should be NONE");
        CHECK(dr32_efx_from_name("nonsense") == DR32_EFX_NONE,
              "an unknown name should resolve to NONE");
        printf("  type names round-trip\n");
    }


    // ---- The always-on Drum Bus must be BIT-IDENTICAL at neutral.
    //
    //      This is the whole justification for running it on every instance
    //      unconditionally. "Close enough" is not the claim being made: a
    //      neutral-looking setting that still ran the saturator or an envelope
    //      follower would colour every kit in DR32 forever, and would show up as
    //      a small null figure rather than as an obvious break. So require
    //      EXACT equality — the stage has to be skipped, not merely quiet.
    {
        hit(dry, N, 90.0f, 0.5f);
        run_bus(0.0f, 0.0f, 0.0f, 0.0f, dry, wet, N);
        int differing = 0;
        for (int i = 0; i < 2 * N; i++) if (wet[i] != dry[i]) differing++;
        printf("  drum bus neutral: %d of %d samples differ\n", differing, 2 * N);
        CHECK(differing == 0,
              "Drum Bus is not bypassed at neutral (%d samples differ) — an "
              "always-on stage must be bit-transparent until a knob moves", differing);
    }

    // ---- ...and the bypass must be a real bypass, not a dead code path.
    //      If the stage never ran at all the test above would also pass, so
    //      prove that a non-neutral setting DOES reach the output.
    {
        hit(dry, N, 90.0f, 0.5f);
        run_bus(0.0f, 1.0f, 0.0f, 0.0f, dry, wet, N);
        int differing = 0;
        for (int i = 0; i < 2 * N; i++) if (wet[i] != dry[i]) differing++;
        CHECK(differing > N / 10,
              "Crunch at full changed only %d samples — the bus is never running",
              differing);
    }

    // ---- The bus processes the send RETURNS too, not just the dry pads.
    //      It sits after the returns are summed, which is what makes it a drum
    //      BUS rather than a pad insert. Feed only a send and require the bus to
    //      still colour the result.
    {
        static float busout[2 * N], plain[2 * N];
        for (int pass = 0; pass < 2; pass++) {
            float *out = pass ? busout : plain;
            dr32_fxbus *fx = dr32_fxbus_create(SR);
            dr32_fxbus_set_send_type(fx, 0, DR32_EFX_PLATE);
            { const float pp5[5] = { 0.5f, 0.3f, 0.6f, 0.0f, 0.0f };
          dr32_fxbus_set_send_params(fx, 0, pp5, 5); }
            dr32_fxbus_set_send_return(fx, 0, 1.0f);
            if (pass) dr32_fxbus_set_bus_params(fx, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
            memset(out, 0, sizeof(float) * 2 * N);
            for (int p = 0; p + 128 <= N; p += 128) {
                /* one hit into the SEND only — the dry path stays empty */
                if (p == 0) for (int i = 0; i < 64; i++) dr32_fxbus_send(fx, 0, i, 0.6f, 0.6f);
                dr32_fxbus_process(fx, out + 2 * p, 128);
            }
            dr32_fxbus_destroy(fx);
        }
        double d2 = 0, r2 = 0;
        for (int i = 0; i < 2 * N; i++) {
            double e = (double)busout[i] - plain[i];
            d2 += e * e; r2 += (double)plain[i] * plain[i];
        }
        CHECK(r2 > 0.0, "send produced nothing to test the bus against");
        CHECK(d2 > r2 * 1e-6,
              "the Drum Bus left the send return untouched — it must run on the "
              "SUMMED mix, after the returns");
    }

    // ---- Delay: the synced time law.
    //      4 sixteenths at 120 BPM is 0.500 s; at 90 BPM it is 0.667 s. This is
    //      the check that catches an off-by-four in the division maths, which
    //      would still produce a perfectly plausible-sounding delay.
    {
        static float imp[2 * ND], out[2 * ND];
        struct { float bpm; float want; } T[] = { { 120.0f, 0.500f }, { 90.0f, 0.6667f } };
        for (int k = 0; k < 2; k++) {
            memset(imp, 0, sizeof(imp));
            imp[0] = 1.0f; imp[1] = 1.0f;
            /* time L = time R = 4 sixteenths, no feedback, tone wide open */
            run_wet5(DR32_EFX_DELAY, 4.0f, 4.0f, 0.0f, 1.0f, 0.0f, imp, out, ND, T[k].bpm);
            int at = -1;
            float best = 0.0f;
            for (int i = 16; i < ND; i++) {
                float a = fabsf(out[2 * i]);
                if (a > best) { best = a; at = i; }
            }
            float secs = (at < 0) ? -1.0f : (float)at / (float)SR;
            printf("  delay %5.1f BPM, 4/16: repeat at %.4f s (want %.4f)\n",
                   T[k].bpm, secs, T[k].want);
            CHECK(best > 0.05f, "delay produced no repeat at %.0f BPM", T[k].bpm);
            CHECK(fabsf(secs - T[k].want) < 0.002f,
                  "delay repeat at %.4f s, expected %.4f — the sixteenth maths is wrong",
                  secs, T[k].want);
        }
    }

    // ---- Delay: L and R are timed INDEPENDENTLY.
    //      Every one of the twelve native delay returns has L != R, so a single
    //      shared time would reproduce none of them.
    {
        static float imp[2 * ND], out[2 * ND];
        memset(imp, 0, sizeof(imp));
        imp[0] = 1.0f; imp[1] = 1.0f;
        run_wet5(DR32_EFX_DELAY, 1.0f, 4.0f, 0.0f, 1.0f, 0.0f, imp, out, ND, 120.0f);

        const int lAt = (int)(0.125f * SR), rAt = (int)(0.500f * SR);
        float lPeak = 0.0f, rPeak = 0.0f, rEarly = 0.0f;
        for (int i = lAt - 64; i < lAt + 64; i++) {
            float a = fabsf(out[2 * i]);     if (a > lPeak) lPeak = a;
            float b = fabsf(out[2 * i + 1]); if (b > rEarly) rEarly = b;
        }
        for (int i = rAt - 64; i < rAt + 64; i++) {
            float b = fabsf(out[2 * i + 1]); if (b > rPeak) rPeak = b;
        }
        printf("  delay L=1/16 R=4/16: L@125ms %.3f  R@125ms %.3f  R@500ms %.3f\n",
               lPeak, rEarly, rPeak);
        CHECK(lPeak > 0.05f, "left tap missing at 125 ms (1 sixteenth)");
        CHECK(rPeak > 0.05f, "right tap missing at 500 ms (4 sixteenths)");
        CHECK(rEarly < lPeak * 0.1f,
              "the right channel repeated on the LEFT tap's time — L and R are "
              "sharing one delay length");
    }

    // ---- Delay: ping-pong endpoints.
    //      0 must keep an L-only hit out of R entirely; 1 must throw it across.
    {
        static float impL[2 * ND], out[2 * ND];
        memset(impL, 0, sizeof(impL));
        impL[0] = 1.0f;                                  // LEFT only

        run_wet5(DR32_EFX_DELAY, 2.0f, 2.0f, 0.5f, 1.0f, 0.0f, impL, out, ND, 120.0f);
        float rOff = 0.0f;
        for (int i = 0; i < ND; i++) { float a = fabsf(out[2 * i + 1]); if (a > rOff) rOff = a; }

        run_wet5(DR32_EFX_DELAY, 2.0f, 2.0f, 0.5f, 1.0f, 1.0f, impL, out, ND, 120.0f);
        float rOn = 0.0f;
        for (int i = 0; i < ND; i++) { float a = fabsf(out[2 * i + 1]); if (a > rOn) rOn = a; }

        printf("  delay ping-pong: R from an L-only hit  off %.4f  on %.4f\n", rOff, rOn);
        CHECK(rOff < 1e-6f, "ping-pong at 0 still crossed into R (%.6f)", rOff);
        CHECK(rOn > 0.02f, "ping-pong at 1 did not cross into R (%.6f)", rOn);
    }

    // ---- Delay, FREE mode: the time is milliseconds and IGNORES the tempo.
    //      The whole point of unsyncing. Both runs ask for 300 ms at wildly
    //      different tempos and must land in the same place.
    {
        static float imp[2 * ND], out[2 * ND];
        const float bpms[2] = { 60.0f, 180.0f };
        for (int k = 0; k < 2; k++) {
            memset(imp, 0, sizeof(imp));
            imp[0] = 1.0f; imp[1] = 1.0f;
            run_delay_free(300.0f, 300.0f, 0.0f, 1.0f, 0.0f, imp, out, ND, bpms[k]);
            int at = -1; float best = 0.0f;
            for (int i = 16; i < ND; i++) {
                float a = fabsf(out[2 * i]);
                if (a > best) { best = a; at = i; }
            }
            float secs = (at < 0) ? -1.0f : (float)at / (float)SR;
            printf("  delay FREE 300 ms at %5.1f BPM: repeat at %.4f s\n", bpms[k], secs);
            CHECK(best > 0.05f, "free delay produced no repeat at %.0f BPM", bpms[k]);
            CHECK(fabsf(secs - 0.300f) < 0.002f,
                  "free delay repeat at %.4f s, expected 0.300 — it is still "
                  "following the tempo", secs);
            /* ⚠ The AMPLITUDE, not just the position. 300 ms is 13230.001
             * samples, not an integer, so this exercises the fractional read —
             * and the first version of that read wrapped in FLOAT, which both
             * lost the fraction and indexed one past the end of the line. A
             * unit impulse came back as a single sample of 1/1024: the energy
             * did not smear, it vanished. Every synced time in these tests
             * happens to land on an exact integer, so nothing else catches it.
             * Energy is compared, since a fractional tap legitimately splits
             * across two samples. */
            double e = 0.0;
            for (int i = 0; i < ND; i++) e += (double)out[2 * i] * out[2 * i];
            CHECK(e > 0.4,
                  "free delay lost the impulse (energy %.6f of 1.0) — a "
                  "fractional read must conserve it, not drop it", e);
        }
    }

    // ---- Delay: the synced and free times are SEPARATE and both survive.
    //      The native device carries SyncedSixteenth and its free time at once
    //      (Chicago Kit sits unsynced while still holding 3/4), so flipping sync
    //      must recall what that mode last had rather than reinterpreting one
    //      number in the wrong unit.
    {
        static float imp[2 * ND], out[2 * ND];
        /* synced 4/16 at 120 BPM = 0.500 s; free = 200 ms. Same param array. */
        const float p[DR32_SEND_PARAMS] = { 4.0f, 4.0f, 0.0f, 1.0f, 0.0f,
                                            1.0f, 200.0f, 200.0f };
        float pFree[DR32_SEND_PARAMS];
        memcpy(pFree, p, sizeof(p));
        pFree[5] = 0.0f;                                  // ONLY the flag differs

        float seen[2];
        for (int k = 0; k < 2; k++) {
            memset(imp, 0, sizeof(imp));
            imp[0] = 1.0f; imp[1] = 1.0f;
            run_wet_p(DR32_EFX_DELAY, k ? pFree : p, imp, out, ND, 120.0f);
            int at = -1; float best = 0.0f;
            for (int i = 16; i < ND; i++) {
                float a = fabsf(out[2 * i]);
                if (a > best) { best = a; at = i; }
            }
            seen[k] = (at < 0) ? -1.0f : (float)at / (float)SR;
        }
        printf("  delay sync flag only:  synced %.3f s   free %.3f s\n", seen[0], seen[1]);
        CHECK(fabsf(seen[0] - 0.500f) < 0.002f,
              "synced time wrong (%.3f s) with the free time also set", seen[0]);
        CHECK(fabsf(seen[1] - 0.200f) < 0.002f,
              "free time wrong (%.3f s) — flipping sync must recall the free "
              "value, not reinterpret the sixteenth count", seen[1]);
    }

    // ---- Delay: ping-pong must work with the two times EQUAL and a CENTRED hit.
    //
    //      This is the case that shipped broken (Josh, on the device): crossing
    //      only the FEEDBACK is a no-op when outL and outR are identical, so the
    //      control did nothing at all in the most ordinary setup there is — a
    //      centre-panned pad with L and R synced. The test above missed it
    //      precisely because an L-only impulse is the one input that works
    //      without steering the input as well.
    //
    //      Real ping-pong alternates: repeat 1 left, repeat 2 right, and so on.
    {
        static float imp[2 * ND], out[2 * ND];
        memset(imp, 0, sizeof(imp));
        imp[0] = 1.0f; imp[1] = 1.0f;                  // CENTRED, both channels
        /* 2 sixteenths = 250 ms, both sides, feedback up so there are repeats */
        run_wet5(DR32_EFX_DELAY, 2.0f, 2.0f, 0.7f, 1.0f, 1.0f, imp, out, ND, 120.0f);

        const int step = (int)(0.250f * SR);
        float l1 = 0, r1 = 0, l2 = 0, r2 = 0;
        for (int i = step - 64; i < step + 64; i++) {
            float a = fabsf(out[2 * i]);     if (a > l1) l1 = a;
            float b = fabsf(out[2 * i + 1]); if (b > r1) r1 = b;
        }
        for (int i = 2 * step - 64; i < 2 * step + 64; i++) {
            float a = fabsf(out[2 * i]);     if (a > l2) l2 = a;
            float b = fabsf(out[2 * i + 1]); if (b > r2) r2 = b;
        }
        printf("  delay ping-pong, equal times, centred hit:  "
               "repeat1 L %.3f R %.3f   repeat2 L %.3f R %.3f\n", l1, r1, l2, r2);
        CHECK(l1 > 0.05f, "no first repeat at all");
        CHECK(r1 < l1 * 0.25f,
              "repeat 1 came out of BOTH channels (L %.3f R %.3f) — ping-pong is "
              "not steering the input, only the feedback", l1, r1);
        CHECK(r2 > l2 * 4.0f,
              "repeat 2 did not swap to the right (L %.3f R %.3f) — the taps are "
              "not alternating", l2, r2);
    }

    // ---- Delay: ping-pong at 0 must leave a centred hit centred.
    //      The other half of the same control: steering the input must not
    //      collapse an ordinary stereo delay to one side.
    {
        static float imp[2 * ND], out[2 * ND];
        memset(imp, 0, sizeof(imp));
        imp[0] = 1.0f; imp[1] = 1.0f;
        run_wet5(DR32_EFX_DELAY, 2.0f, 2.0f, 0.5f, 1.0f, 0.0f, imp, out, ND, 120.0f);
        double el = 0, er = 0;
        for (int i = 0; i < ND; i++) {
            el += (double)out[2 * i] * out[2 * i];
            er += (double)out[2 * i + 1] * out[2 * i + 1];
        }
        float bal = 10.0f * log10f((float)((er + 1e-30) / (el + 1e-30)));
        printf("  delay ping-pong 0, centred hit: R/L balance %+.2f dB\n", bal);
        CHECK(fabsf(bal) < 0.5f,
              "ping-pong at 0 unbalanced a centred hit by %+.2f dB", bal);
    }

    // ---- Drum Bus: Mix is a real dry/wet blend (parallel compression).
    //      It was on the Drum Bus as an insert and went missing when the stage
    //      was lifted onto the master mix (Josh spotted it, 2026-07-28).
    {
        hit(dry, N, 90.0f, 0.5f);
        static float wetFull[2 * N], wetHalf[2 * N], wetNone[2 * N];
        run_bus5(0.0f, 1.0f, 0.0f, 0.0f, 1.0f, dry, wetFull, N);
        run_bus5(0.0f, 1.0f, 0.0f, 0.0f, 0.5f, dry, wetHalf, N);
        run_bus5(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, dry, wetNone, N);

        /* mix 0 = the dry signal back, exactly */
        int differing = 0;
        for (int i = 0; i < 2 * N; i++) if (wetNone[i] != dry[i]) differing++;
        CHECK(differing == 0, "Mix at 0 is not the dry signal (%d samples differ)", differing);

        /* mix 0.5 = exactly halfway between dry and fully processed */
        double err = 0, ref = 0;
        for (int i = 0; i < 2 * N; i++) {
            double want = 0.5 * dry[i] + 0.5 * wetFull[i];
            double e = wetHalf[i] - want;
            err += e * e; ref += want * want;
        }
        float nulldb = 10.0f * log10f((float)((err + 1e-30) / (ref + 1e-30)));
        printf("  drum bus mix 0.5 vs the exact blend: %.1f dB\n", nulldb);
        CHECK(nulldb < -100.0f, "Mix is not a linear dry/wet blend (%.1f dB)", nulldb);
    }

    // ---- Delay: feedback must be BOUNDED.
    //      A send return has no dry path to balance it, so a runaway loop is not
    //      self-limiting the way an insert's would feel. Drive it at the top of
    //      the knob with a repeating hit and require it to stay finite.
    {
        dr32_fxbus *fx = dr32_fxbus_create(SR);
        dr32_fxbus_set_send_type(fx, 0, DR32_EFX_DELAY);
        dr32_fxbus_set_bpm(fx, 120.0f);
        { const float pd8[DR32_SEND_PARAMS] = { 1.0f, 2.0f, 1.0f, 1.0f, 0.5f, 1.0f, 125.0f, 500.0f };
          dr32_fxbus_set_send_params(fx, 0, pd8, DR32_SEND_PARAMS); }
        dr32_fxbus_set_send_return(fx, 0, 1.0f);
        float peak = 0.0f;
        const int blocks = SR * 30 / 128;              // 30 s
        for (int b = 0; b < blocks; b++) {
            float blk[2 * 128];
            memset(blk, 0, sizeof(blk));
            /* a hit every ~0.37 s, forever */
            if (b % 128 == 0) for (int i = 0; i < 32; i++) dr32_fxbus_send(fx, 0, i, 0.7f, 0.7f);
            dr32_fxbus_process(fx, blk, 128);
            for (int i = 0; i < 128; i++) {
                float a = fabsf(blk[2 * i]);
                if (a > peak) peak = a;
            }
        }
        printf("  delay 30 s at max feedback: peak %.3f\n", peak);
        CHECK(isfinite(peak) && peak < 8.0f,
              "delay ran away at max feedback (peak %.3f) — the cap is not holding", peak);
        dr32_fxbus_destroy(fx);
    }

    // ---- Delay: the tone filter is in the FEEDBACK path, not on the output.
    //      On the output it would darken the first tap and leave the repeats
    //      accumulating; in the loop each pass is filtered again, so late
    //      repeats darken progressively while the first is barely touched.
    {
        static float imp[2 * ND], out[2 * ND];
        float first[2], late[2];
        for (int k = 0; k < 2; k++) {
            memset(imp, 0, sizeof(imp));
            imp[0] = 1.0f; imp[1] = 1.0f;
            /* tone 1.0 = wide open, 0.35 = a dark loop; 1 sixteenth = 125 ms */
            run_wet5(DR32_EFX_DELAY, 1.0f, 1.0f, 0.9f, k ? 0.35f : 1.0f, 0.0f,
                     imp, out, ND, 120.0f);
            /* HF energy of the first repeat vs the fourth */
            float z = 0.0f;
            const float a = expf(-2.0f * (float)M_PI * 5000.0f / SR);
            double e1 = 0, e4 = 0;
            for (int i = 0; i < ND; i++) {
                float x = out[2 * i];
                z = x * (1.0f - a) + z * a;
                float hf = x - z;
                if (i > (int)(0.120f * SR) && i < (int)(0.140f * SR)) e1 += (double)hf * hf;
                if (i > (int)(0.495f * SR) && i < (int)(0.515f * SR)) e4 += (double)hf * hf;
            }
            first[k] = (float)e1;
            late[k]  = (float)e4;
        }
        /* the dark setting must cost the FOURTH repeat far more HF than the first */
        float dFirst = 10.0f * log10f((first[1] + 1e-20f) / (first[0] + 1e-20f));
        float dLate  = 10.0f * log10f((late[1]  + 1e-20f) / (late[0]  + 1e-20f));
        printf("  delay tone: HF change  first repeat %+.1f dB   fourth %+.1f dB\n",
               dFirst, dLate);
        CHECK(dLate < dFirst - 6.0f,
              "tone hit the first repeat as hard as the fourth (%+.1f vs %+.1f dB) — "
              "the filter is on the output, not in the loop", dFirst, dLate);
    }

    // ---- Delay: the idle-skip must not cut the repeats off.
    //      The bus stops processing a slot after ~4 s of silence at its INPUT,
    //      which is fine for a reverb and wrong for a delay: silence between
    //      hits is the state a delay is for, and at 16 sixteenths (2 s at 120
    //      BPM) with high feedback it is still repeating long after that.
    {
        dr32_fxbus *fx = dr32_fxbus_create(SR);
        dr32_fxbus_set_send_type(fx, 0, DR32_EFX_DELAY);
        dr32_fxbus_set_bpm(fx, 120.0f);
        /* 16 sixteenths = 2 s per repeat, feedback high, tone wide open */
        { const float pd8[DR32_SEND_PARAMS] = { 16.0f, 16.0f, 0.9f, 1.0f, 0.0f, 1.0f, 125.0f, 500.0f };
          dr32_fxbus_set_send_params(fx, 0, pd8, DR32_SEND_PARAMS); }
        dr32_fxbus_set_send_return(fx, 0, 1.0f);

        float blk[2 * 128];
        for (int i = 0; i < 128; i++) dr32_fxbus_send(fx, 0, i, 0.6f, 0.6f);
        memset(blk, 0, sizeof(blk));
        dr32_fxbus_process(fx, blk, 128);

        /* nothing more goes in, ever — run out to 9 s and watch the repeat at 8 s */
        float late = 0.0f;
        const int upto = SR * 9 / 128;
        for (int b = 1; b < upto; b++) {
            memset(blk, 0, sizeof(blk));
            dr32_fxbus_process(fx, blk, 128);
            if (b * 128 > SR * 15 / 2) {                 /* past 7.5 s */
                for (int i = 0; i < 128; i++) {
                    float a = fabsf(blk[2 * i]);
                    if (a > late) late = a;
                }
            }
        }
        printf("  delay still repeating at 7.5-9 s: peak %.4f\n", late);
        CHECK(late > 1e-3f,
              "the idle-skip silenced the delay after 4 s of quiet (peak %.6f) — "
              "a delay's input IS silent between hits", late);
        dr32_fxbus_destroy(fx);
    }

    // ---- Delay: an absurdly slow tempo must clamp, not read out of bounds.
    {
        static float imp[2 * ND], out[2 * ND];
        memset(imp, 0, sizeof(imp));
        imp[0] = 1.0f; imp[1] = 1.0f;
        /* 16 sixteenths at 40 BPM would be 6 s, past the 4 s line */
        run_wet5(DR32_EFX_DELAY, 16.0f, 16.0f, 0.3f, 1.0f, 0.0f, imp, out, ND, 40.0f);
        int bad = 0;
        for (int i = 0; i < 2 * ND; i++) if (!isfinite(out[i])) bad++;
        CHECK(bad == 0, "delay produced %d non-finite samples past its buffer limit", bad);
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
