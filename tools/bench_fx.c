/* clock_gettime needs POSIX visibility under -std=c11 */
#define _POSIX_C_SOURCE 200809L
// bench_fx.c — measure FX cost ON THE DEVICE, as a share of one core.
//
// A Mac timing says nothing about a Cortex-A72, so this is built for aarch64
// and run on the Move. Reports the realtime factor and the percentage of one
// core each effect costs at 44.1 kHz / 128-frame blocks.

#include "../dsp/dr32_fxbus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define SR      44100
#define BLOCK   128
#define SECONDS 10

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void bench(const char *name, dr32_efx_type type, int as_insert) {
    dr32_fxbus *fx = dr32_fxbus_create(SR);
    if (as_insert) {
        dr32_fxbus_set_insert_type(fx, 0, type);
        dr32_fxbus_set_insert_params(fx, 0, 0.6f, 0.3f, 0.6f,
                                     type == DR32_EFX_DRUMBUSS ? 0.5f : 0.2f, 1.0f);
    } else {
        dr32_fxbus_set_send_type(fx, 0, type);
        dr32_fxbus_set_send_params(fx, 0, 0.6f, 0.3f, 0.6f, 0.2f);
        dr32_fxbus_set_send_return(fx, 0, 1.0f);
    }

    static float blk[2 * BLOCK];
    const int blocks = (SR * SECONDS) / BLOCK;

    /* warm the caches / let any startup transient settle */
    for (int b = 0; b < 100; b++) {
        for (int i = 0; i < BLOCK; i++) {
            float v = (b == 0 && i < 8) ? 0.5f : 0.0f;
            blk[2 * i] = v; blk[2 * i + 1] = v;
            if (!as_insert) dr32_fxbus_send(fx, 0, i, v, v);
        }
        dr32_fxbus_process(fx, blk, BLOCK);
    }

    double t0 = now_s();
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < BLOCK; i++) {
            float v = ((b & 63) == 0 && i < 8) ? 0.5f : 0.0f;   /* a hit every ~186 ms */
            blk[2 * i] = v; blk[2 * i + 1] = v;
            if (!as_insert) dr32_fxbus_send(fx, 0, i, v, v);
        }
        dr32_fxbus_process(fx, blk, BLOCK);
    }
    double dt = now_s() - t0;

    double audio_s = (double)blocks * BLOCK / SR;
    double pct = 100.0 * dt / audio_s;
    printf("  %-10s %6.2f%% of one core   (%.1fx realtime)\n", name, pct, audio_s / dt);
    dr32_fxbus_destroy(fx);
}

int main(void) {
    printf("DR32 FX cost @ %d Hz, %d-frame blocks, %d s of audio each\n", SR, BLOCK, SECONDS);
    printf("(one slot active; 4 slots exist, so multiply by what you actually use)\n");
    bench("bypass",    DR32_EFX_NONE,     1);
    bench("Plate",     DR32_EFX_PLATE,    0);
    bench("Spaces",    DR32_EFX_SPACES,   0);
    bench("Drum Bus", DR32_EFX_DRUMBUSS, 1);
    return 0;
}
