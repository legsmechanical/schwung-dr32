// render_verb.c — render the Native (SuperEco) reverb from RAW device
// parameters, for the null test.
//
// The pad-effect null test goes through score.mjs -> render_score.c and drives
// the whole kit engine. A reverb does not need any of that: the reference
// renders in build/ir are a single-sample impulse through a return chain, so
// the tail is already isolated and the only thing that has to be reproduced is
// the reverb itself, driven by the preset's own 33 parameters.
//
// Usage: render_verb <in.verb> <out.wav> [frames]
//
//   in.verb   key/value list from tools/verb_score.mjs
//   out.wav   32-bit float stereo, to diff against the oracle's render
//
// ⚠ Every parameter the port does not model is REPORTED, not swallowed. A null
// test whose renderer quietly ignores half a preset produces a bad number with
// nothing to say why, and that is worse than no test.

#include "../dsp/dr32_fxbus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SR    44100
#define BLOCK 128

static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }

static void write_float_wav(const char *path, const float *data, size_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    uint32_t bytes = (uint32_t)(frames * 2 * sizeof(float));
    fputs("RIFF", f); w32(f, 36 + bytes); fputs("WAVE", f);
    fputs("fmt ", f); w32(f, 16);
    w16(f, 3); w16(f, 2);
    w32(f, SR);
    w32(f, SR * 2 * (uint32_t)sizeof(float));
    w16(f, 2 * (uint16_t)sizeof(float));
    w16(f, 32);
    fputs("data", f); w32(f, bytes);
    fwrite(data, 1, bytes, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: render_verb <in.verb> <out.wav> [frames]\n");
        return 2;
    }
    size_t frames = (argc > 3) ? (size_t)atol(argv[3]) : (size_t)(SR * 20);

    FILE *in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }

    dr32_fxbus *fx = dr32_fxbus_create((float)SR);
    if (!fx) { fprintf(stderr, "out of memory\n"); return 1; }
    dr32_fxbus_set_send_type(fx, 0, DR32_EFX_NATIVE);
    dr32_fxbus_set_send_return(fx, 0, 1.0f);

    char line[512];
    int applied = 0, unmodelled = 0, unknown = 0;
    while (fgets(line, sizeof(line), in)) {
        char key[128]; double val;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%127s %lf", key, &val) != 2) continue;
        switch (dr32_fxbus_native_set_raw(fx, 0, key, (float)val)) {
            case 1:  applied++; break;
            case 0:  unmodelled++;
                     fprintf(stderr, "  not modelled: %-24s = %g\n", key, val);
                     break;
            default: unknown++;
                     fprintf(stderr, "  UNKNOWN KEY:  %-24s = %g\n", key, val);
                     break;
        }
    }
    fclose(in);
    dr32_fxbus_native_raw_commit(fx, 0);

    // An unknown key means the preset and the port disagree about what this
    // device even is — most likely RoomType is not SuperEco, in which case the
    // reference was rendered by a different algorithm and no null number from
    // it would mean anything.
    if (unknown) {
        fprintf(stderr, "render_verb: %d unknown parameter(s); refusing to render\n",
                unknown);
        dr32_fxbus_destroy(fx);
        return 1;
    }
    fprintf(stderr, "render_verb: %d applied, %d not modelled\n", applied, unmodelled);

    float *out = (float *)calloc(frames * 2, sizeof(float));
    if (!out) { fprintf(stderr, "out of memory\n"); return 1; }

    // A single-sample impulse, matching how the references in build/ir were
    // rendered. With a true impulse the dry contribution is ONE sample, so the
    // tail needs no subtraction to isolate.
    float blk[2 * BLOCK];
    for (size_t p = 0; p < frames; p += BLOCK) {
        size_t n = (frames - p < BLOCK) ? frames - p : BLOCK;
        memset(blk, 0, sizeof(blk));
        for (size_t i = 0; i < n; i++) {
            const float v = (p + i == 0) ? 1.0f : 0.0f;
            dr32_fxbus_send(fx, 0, (int)i, v, v);
        }
        dr32_fxbus_process(fx, blk, (int)n);
        memcpy(out + 2 * p, blk, n * 2 * sizeof(float));
    }

    write_float_wav(argv[2], out, frames);
    free(out);
    dr32_fxbus_destroy(fx);
    return 0;
}
