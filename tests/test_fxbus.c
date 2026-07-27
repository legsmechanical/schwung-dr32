// FX bus tests — sends, inserts, and the Drum Buss stages.
//
// These exist because "it isn't doing anything" and "it thins the low end" are
// both measurable, and were both true of the first Drum Buss: Transients used a
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

/** Run a block through an insert slot and return the processed buffer. */
// p4 is pre-delay for the reverbs and SUSTAIN for the Drum Buss. Drum Buss
// callers must pass 0.5 for neutral -- 0.0 pulls the tail down 8 dB.
static void run_insert4(dr32_efx_type type, float p1, float p2, float p3, float p4,
                        float mix, const float *in, float *out, int n) {
    dr32_fxbus *fx = dr32_fxbus_create(SR);
    dr32_fxbus_set_insert_type(fx, 0, type);
    dr32_fxbus_set_insert_params(fx, 0, p1, p2, p3, p4, mix);
    memcpy(out, in, sizeof(float) * 2 * (size_t)n);
    for (int p = 0; p < n; p += 128) {
        int m = (p + 128 <= n) ? 128 : (n - p);
        dr32_fxbus_process(fx, out + 2 * p, m);
    }
    dr32_fxbus_destroy(fx);
}

static void run_insert(dr32_efx_type type, float p1, float p2, float p3, float mix,
                       const float *in, float *out, int n) {
    run_insert4(type, p1, p2, p3, type == DR32_EFX_DRUMBUSS ? 0.5f : 0.0f, mix, in, out, n);
}

int main(void) {
    printf("fx bus\n");
    static float dry[2 * N], wet[2 * N], wet2[2 * N];

    // ---- Drum Buss / Attack must actually change the attack-to-tail balance
    {
        hit(dry, N, 120.0f, 0.4f);
        const int atk_from = 0, atk_to = SR / 200;          // first 5 ms
        const int tail_from = SR / 20, tail_to = SR / 5;    // 50-200 ms

        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.5f, 1.0f, dry, wet, N);
        float n_atk = rms_range(wet, atk_from, atk_to), n_tail = rms_range(wet, tail_from, tail_to);
        CHECK(n_tail > 1e-6f, "neutral attack produced no tail");
        float neutral = n_atk / (n_tail + 1e-9f);

        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 1.0f, 1.0f, dry, wet, N);
        float s_atk = rms_range(wet, atk_from, atk_to), s_tail = rms_range(wet, tail_from, tail_to);
        float sharp = s_atk / (s_tail + 1e-9f);

        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.0f, 1.0f, dry, wet, N);
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

        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.5f, 1.0f, low, wet, N);
        float low_dry = rms_range(wet, 0, SR / 10);
        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.8f, 0.5f, 1.0f, low, wet, N);
        float low_crunch = rms_range(wet, 0, SR / 10);

        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.5f, 1.0f, high, wet2, N);
        float high_dry = rms_range(wet2, 0, SR / 10);
        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.8f, 0.5f, 1.0f, high, wet2, N);
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

        struct { dr32_efx_type t; const char *n; } types[] = {
            { DR32_EFX_PLATE, "Plate" }, { DR32_EFX_SPACES, "Spaces" },
        };
        for (size_t k = 0; k < sizeof(types) / sizeof(types[0]); k++) {
            run_insert(types[k].t, 0.5f, 0.3f, 0.6f, 1.0f, imp, wet, N);
            float tail = rms_range(wet, SR / 20, SR / 4);   // 50-250 ms after the hit
            printf("  %-5s tail rms %.6f\n", types[k].n, tail);
            CHECK(tail > 1e-5f, "%s produced no tail (rms %.8f) — silent reverb", types[k].n, tail);
        }
    }

    // ---- a send bus returns signal, and its return level scales it
    {
        dr32_fxbus *fx = dr32_fxbus_create(SR);
        dr32_fxbus_set_send_type(fx, 0, DR32_EFX_PLATE);
        dr32_fxbus_set_send_params(fx, 0, 0.5f, 0.3f, 0.6f, 0.0f);
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

    // ---- a send bus must be SAMPLE-EQUIVALENT to the same effect as a fully
    //      wet insert. This is the decisive test: any corruption of when things
    //      land inside the block shows up immediately.
    //
    //      The bug this catches: send writes were all landing on frame 0, so a
    //      send became an impulse train at the block rate (~344 Hz) and rang
    //      metallically, while inserts stayed clean. A weaker test that merely
    //      asserted "the send returns something" passed the whole time.
    {
        static float sig[2 * N], as_insert[2 * N], as_send[2 * N];
        hit(sig, N, 200.0f, 0.5f);

        run_insert(DR32_EFX_PLATE, 0.5f, 0.3f, 0.5f, 1.0f, sig, as_insert, N);

        dr32_fxbus *fx = dr32_fxbus_create(SR);
        dr32_fxbus_set_send_type(fx, 0, DR32_EFX_PLATE);
        dr32_fxbus_set_send_params(fx, 0, 0.5f, 0.3f, 0.5f, 0.0f);
        dr32_fxbus_set_send_return(fx, 0, 1.0f);
        memset(as_send, 0, sizeof(as_send));
        for (int p = 0; p + 128 <= N; p += 128) {
            for (int i = 0; i < 128; i++) dr32_fxbus_send(fx, 0, i, sig[2 * (p + i)], sig[2 * (p + i) + 1]);
            dr32_fxbus_process(fx, as_send + 2 * p, 128);
        }
        dr32_fxbus_destroy(fx);

        double num = 0.0, den = 0.0;
        for (int i = 0; i < 2 * (N - 128); i++) {
            double d = (double)as_send[i] - as_insert[i];
            num += d * d;
            den += (double)as_insert[i] * as_insert[i];
        }
        double err_db = (den > 0) ? 10.0 * log10((num + 1e-30) / den) : 0.0;
        printf("  send vs wet insert: %.1f dB error\n", err_db);
        CHECK(err_db < -60.0, "send path does not match a fully wet insert (%.1f dB error)", err_db);
    }

    // ---- per-type defaults are musical AND distinct from each other
    {
        float pl[5], rm[5], hl[5], db[5];
        dr32_efx_defaults(DR32_EFX_PLATE, pl);
        dr32_efx_defaults(DR32_EFX_SPACES, rm);
        dr32_efx_defaults(DR32_EFX_SPACES, hl);
        dr32_efx_defaults(DR32_EFX_DRUMBUSS, db);

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
        // Drum Buss should be doing something on arrival, but gently.
        CHECK(db[0] > 0.05f && db[0] < 0.6f, "drum buss compress default %.2f is not gentle", db[0]);
        CHECK(db[2] > 0.5f, "drum buss should add a little attack by default (%.2f)", db[2]);

        // A REVERB as an insert must never arrive fully wet — that replaces the
        // kit with its own ambience. A processor like Drum Buss should be wet.
        CHECK(pl[4] < 0.5f, "plate insert default mix %.2f is too wet", pl[4]);
        CHECK(rm[4] < 0.5f, "room insert default mix %.2f is too wet", rm[4]);
        CHECK(hl[4] < 0.5f, "hall insert default mix %.2f is too wet", hl[4]);
        CHECK(db[4] > 0.9f, "drum buss should be fully wet (%.2f)", db[4]);
    }

    // ---- at their DEFAULT settings, the three reverbs must actually differ in
    //      decay time, and none may be enormous. This is the check that matters
    //      (Hall once measured >6 s at its defaults and swamped the kit).
    {
        struct { dr32_efx_type t; const char *n; } T[] = {
            { DR32_EFX_SPACES, "Spaces" }, { DR32_EFX_PLATE, "Plate" },
        };
        float rt[2];
        for (int k = 0; k < 2; k++) {
            float d[5];
            dr32_efx_defaults(T[k].t, d);
            dr32_fxbus *fx = dr32_fxbus_create(SR);
            dr32_fxbus_set_insert_type(fx, 0, T[k].t);
            dr32_fxbus_set_insert_params(fx, 0, d[0], d[1], d[2], d[3], 1.0f);
            float peak = 0.0f;
            int last = 0;
            const int blocks = SR * 8 / 128;
            for (int b = 0; b < blocks; b++) {
                float blk[2 * 128];
                memset(blk, 0, sizeof(blk));
                if (b == 0) for (int i = 0; i < 32; i++) { blk[2 * i] = 0.7f; blk[2 * i + 1] = 0.7f; }
                dr32_fxbus_process(fx, blk, 128);
                float m = 0.0f;
                for (int i = 0; i < 128; i++) { float a = fabsf(blk[2 * i]); if (a > m) m = a; }
                if (b < 40 && m > peak) peak = m;
                if (m > peak * 0.001f) last = b;
            }
            rt[k] = (float)last * 128.0f / SR;
            dr32_fxbus_destroy(fx);
        }
        printf("  default RT60: Spaces %.2fs  Plate %.2fs\n", rt[0], rt[1]);
        /* Room and Hall are gone -- Spaces is one flexible model covering both,
         * so there is no longer a "hall must be longer than room" relationship
         * to assert. What still matters: both open at a usable length and
         * neither swamps the kit (Hall once measured >6 s at its defaults). */
        for (int k = 0; k < 2; k++) {
            CHECK(rt[k] > 0.1f, "%s default decay %.2f s is inaudibly short", T[k].n, rt[k]);
            CHECK(rt[k] < 3.0f, "%s default decay %.2f s is too big for a drum kit", T[k].n, rt[k]);
        }
    }

    // ---- an idle send bus must stop costing CPU, but only after its tail
    {
        dr32_fxbus *fx = dr32_fxbus_create(SR);
        dr32_fxbus_set_send_type(fx, 0, DR32_EFX_PLATE);
        dr32_fxbus_set_send_params(fx, 0, 0.5f, 0.3f, 0.6f, 0.0f);
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
            run_insert4(DR32_EFX_DRUMBUSS, k * 0.25f, 0.0f, 0.5f, 0.5f, 1.0f, quiet, wet, N);
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

        run_insert4(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, dry, wet, N);
        float n_atk = peak_range(wet, 0, atk_to), n_tail = rms_range(wet, t_from, t_to);

        run_insert4(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f, dry, wet, N);
        float up_atk = peak_range(wet, 0, atk_to), up_tail = rms_range(wet, t_from, t_to);
        run_insert4(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.5f, 0.0f, 1.0f, dry, wet, N);
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
        const dr32_efx_type types[2] = { DR32_EFX_PLATE, DR32_EFX_SPACES };
        const char *names[2] = { "Plate", "Spaces" };
        for (int t = 0; t < 2; t++) {
            static float imp[2 * N];
            memset(imp, 0, sizeof(imp));
            imp[0] = 1.0f;                                  // LEFT only
            run_insert4(types[t], 0.5f, 0.3f, 0.5f, 0.0f, 1.0f, imp, wet, N);
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
            run_insert4(types[t], 0.5f, 0.3f, 0.5f, 0.0f, 1.0f, imp2, wet2, N);
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = SR / 10; i < N; i++) {
                double x = wet2[2 * i], y = wet2[2 * i + 1];
                sxy += x * y; sxx += x * x; syy += y * y;
            }
            float corr = (sxx > 0 && syy > 0) ? (float)(sxy / sqrt(sxx * syy)) : 1.0f;
            printf("  %-5s corr %+.2f   L-only -> R/L %+.1f dB\n", names[t], corr, rl);
            /* BOTH must be decorrelated. The Plate always was. Spaces
             * (Verbity2) is symmetric internally and on its own produced
             * bit-identical L and R (corr +1.00); it is fed through the same
             * per-channel diffuser as the plate, whose two sides run different
             * prime lengths, which takes it to about +0.05. */
            CHECK(corr < 0.50f,
                  "%s produced correlated L/R (corr %+.2f) — it is running mono",
                  names[t], corr);
            CHECK(rl > -50.0f,
                  "%s put only %+.1f dB into the right channel from an L-only hit",
                  names[t], rl);
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
        run_insert4(DR32_EFX_PLATE, 0.45f, 0.35f, 0.45f, 0.0f, 1.0f, imp, wet, N);

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
        const dr32_efx_type ty[2] = { DR32_EFX_PLATE, DR32_EFX_SPACES };
        const char *nm[2] = { "Plate", "Spaces" };
        for (int t = 0; t < 2; t++) {
            static float imp[2 * N];
            memset(imp, 0, sizeof(imp));
            imp[0] = 1.0f; imp[1] = 1.0f;
            run_insert4(ty[t], 0.5f, 0.3f, 0.5f, 0.0f, 1.0f, imp, wet, N);
            double mono = 0, one = 0;
            for (int i = SR / 10; i < N; i++) {
                double m = 0.5 * (wet[2 * i] + wet[2 * i + 1]);
                mono += m * m; one += (double)wet[2 * i] * wet[2 * i];
            }
            float fold = 10.0f * log10f((float)(mono / (one + 1e-30)) + 1e-30f);
            printf("  %-6s mono-fold %+.2f dB\n", nm[t], fold);
            CHECK(fold > -6.0f,
                  "%s loses %+.2f dB when summed to mono — the width is phase cancellation",
                  nm[t], fold);
        }
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
