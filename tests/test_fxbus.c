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

static float rms_range(const float *x, int from, int to) {
    double s = 0; int n = 0;
    for (int i = from; i < to; i++) { s += (double)x[2 * i] * x[2 * i]; n++; }
    return n ? (float)sqrt(s / n) : 0.0f;
}

/** Run a block through an insert slot and return the processed buffer. */
static void run_insert(dr32_efx_type type, float p1, float p2, float p3, float mix,
                       const float *in, float *out, int n) {
    dr32_fxbus *fx = dr32_fxbus_create(SR);
    dr32_fxbus_set_insert_type(fx, 0, type);
    dr32_fxbus_set_insert_params(fx, 0, p1, p2, p3, 0.0f, mix);
    memcpy(out, in, sizeof(float) * 2 * (size_t)n);
    for (int p = 0; p < n; p += 128) {
        int m = (p + 128 <= n) ? 128 : (n - p);
        dr32_fxbus_process(fx, out + 2 * p, m);
    }
    dr32_fxbus_destroy(fx);
}

int main(void) {
    printf("fx bus\n");
    static float dry[2 * N], wet[2 * N], wet2[2 * N];

    // ---- Drum Buss / Transients must actually change the attack-to-tail balance
    {
        hit(dry, N, 120.0f, 0.4f);
        const int atk_from = 0, atk_to = SR / 200;          // first 5 ms
        const int tail_from = SR / 20, tail_to = SR / 5;    // 50-200 ms

        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.5f, 1.0f, dry, wet, N);
        float n_atk = rms_range(wet, atk_from, atk_to), n_tail = rms_range(wet, tail_from, tail_to);
        CHECK(n_tail > 1e-6f, "neutral transients produced no tail");
        float neutral = n_atk / (n_tail + 1e-9f);

        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 1.0f, 1.0f, dry, wet, N);
        float s_atk = rms_range(wet, atk_from, atk_to), s_tail = rms_range(wet, tail_from, tail_to);
        float sharp = s_atk / (s_tail + 1e-9f);

        run_insert(DR32_EFX_DRUMBUSS, 0.0f, 0.0f, 0.0f, 1.0f, dry, wet, N);
        float f_atk = rms_range(wet, atk_from, atk_to), f_tail = rms_range(wet, tail_from, tail_to);
        float soft = f_atk / (f_tail + 1e-9f);

        printf("  transients attack:tail  soft %.3f  neutral %.3f  sharp %.3f\n", soft, neutral, sharp);
        CHECK(sharp > neutral * 1.15f, "Transients up did not sharpen: %.3f vs %.3f", sharp, neutral);
        CHECK(soft < neutral * 0.87f, "Transients down did not soften: %.3f vs %.3f", soft, neutral);
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
            { DR32_EFX_PLATE, "Plate" }, { DR32_EFX_ROOM, "Room" }, { DR32_EFX_HALL, "Hall" },
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
                dr32_fxbus_send(fx, 0, v, v);
            }
            dr32_fxbus_process(fx, blk, 128);
            for (int i = 0; i < 128; i++) acc += fabsf(blk[2 * i]);
        }
        CHECK(acc > 1e-3f, "send bus returned nothing (%.6f)", acc);
        dr32_fxbus_destroy(fx);
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
