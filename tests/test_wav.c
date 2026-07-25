// Off-device WAV loader tests. Build+run: tests/run.sh
//
// Synthesizes WAV files covering every shape the loader claims to handle, then
// checks decode accuracy. Also runs against real device samples if a directory
// is passed as argv[1].

#include "../dsp/wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }

/** Write a WAV whose samples are a known ramp, so decode errors are visible. */
static void write_wav(const char *path, int bits, int channels, int is_float,
                      int frames, int extensible, int junk_chunk) {
    FILE *f = fopen(path, "wb");
    int bps = bits / 8;
    uint32_t data_bytes = (uint32_t)frames * bps * channels;
    uint32_t fmt_size = extensible ? 40 : 16;
    uint32_t junk_bytes = junk_chunk ? 10 : 0;   // odd size on purpose: tests word-align skip

    fputs("RIFF", f);
    w32(f, 4 + (8 + fmt_size) + (junk_chunk ? 8 + junk_bytes + (junk_bytes & 1) : 0) + 8 + data_bytes);
    fputs("WAVE", f);

    if (junk_chunk) {   // an unknown chunk BEFORE fmt — the loader must skip it
        fputs("LIST", f); w32(f, junk_bytes);
        for (uint32_t i = 0; i < junk_bytes; i++) fputc('x', f);
        if (junk_bytes & 1) fputc(0, f);
    }

    fputs("fmt ", f); w32(f, fmt_size);
    w16(f, extensible ? 0xFFFE : (is_float ? 3 : 1));
    w16(f, channels);
    w32(f, 44100);
    w32(f, 44100 * bps * channels);
    w16(f, bps * channels);
    w16(f, bits);
    if (extensible) {
        w16(f, 22); w16(f, bits); w32(f, 0);
        w16(f, is_float ? 3 : 1);          // real tag hides in the GUID head
        for (int i = 0; i < 14; i++) fputc(0, f);
    }

    fputs("data", f); w32(f, data_bytes);
    for (int i = 0; i < frames; i++) {
        // ramp from -1 to +1 across the file
        float v = (frames == 1) ? 0.0f : (2.0f * (float)i / (float)(frames - 1) - 1.0f);
        v *= 0.999f;                        // stay inside full scale
        for (int c = 0; c < channels; c++) {
            if (is_float) { float x = v; fwrite(&x, 4, 1, f); }
            // Round, don't truncate — truncation costs most of an LSB, which at
            // 8-bit is 0.008 and would look like a decoder error.
            else if (bits == 8)  fputc((int)lrintf(v * 127.0f) + 128, f);
            else if (bits == 16) w16(f, (uint16_t)(int16_t)lrintf(v * 32767.0f));
            else if (bits == 24) { int32_t s = (int32_t)lrintf(v * 8388607.0f);
                                   fputc(s & 0xff, f); fputc((s >> 8) & 0xff, f); fputc((s >> 16) & 0xff, f); }
            else if (bits == 32) w32(f, (uint32_t)(int32_t)lrintf(v * 2147483647.0f));
        }
    }
    fclose(f);
}

static void expect_ramp(const char *label, const char *path, int frames, float tol) {
    dr32_wav w;
    dr32_wav_err e = dr32_wav_load(path, &w);
    CHECK(e == DR32_WAV_OK, "%s: load failed: %s", label, dr32_wav_strerror(e));
    if (e != DR32_WAV_OK) return;
    CHECK(w.frames == (size_t)frames, "%s: frames %zu != %d", label, w.frames, frames);
    CHECK(w.sample_rate == 44100, "%s: rate %d", label, w.sample_rate);
    for (int i = 0; i < frames; i++) {
        float want = (2.0f * (float)i / (float)(frames - 1) - 1.0f) * 0.999f;
        if (fabsf(w.data[i] - want) > tol) {
            CHECK(0, "%s: frame %d = %.6f, want %.6f (tol %.6f)", label, i, w.data[i], want, tol);
            break;
        }
    }
    dr32_wav_free(&w);
    CHECK(w.data == NULL, "%s: free did not zero the struct", label);
}

int main(int argc, char **argv) {
    const char *tmp = "/tmp/dr32_wav_test.wav";
    const int N = 64;

    printf("wav loader\n");

    write_wav(tmp, 16, 1, 0, N, 0, 0); expect_ramp("16-bit mono",   tmp, N, 1e-4f);
    write_wav(tmp, 24, 1, 0, N, 0, 0); expect_ramp("24-bit mono",   tmp, N, 1e-6f);
    write_wav(tmp, 32, 1, 0, N, 0, 0); expect_ramp("32-bit mono",   tmp, N, 1e-6f);
    write_wav(tmp,  8, 1, 0, N, 0, 0); expect_ramp("8-bit unsigned",tmp, N, 1e-2f);
    write_wav(tmp, 32, 1, 1, N, 0, 0); expect_ramp("float32 mono",  tmp, N, 1e-6f);

    // Stereo with identical channels must downmix to the same ramp.
    write_wav(tmp, 24, 2, 0, N, 0, 0); expect_ramp("24-bit stereo downmix", tmp, N, 1e-6f);

    // WAVE_FORMAT_EXTENSIBLE and a leading unknown odd-sized chunk.
    write_wav(tmp, 24, 1, 0, N, 1, 0); expect_ramp("extensible 24-bit", tmp, N, 1e-6f);
    write_wav(tmp, 24, 1, 0, N, 0, 1); expect_ramp("skips LIST chunk",  tmp, N, 1e-6f);

    // Failure paths must be clean, not crashes.
    dr32_wav w;
    CHECK(dr32_wav_load("/nonexistent/x.wav", &w) == DR32_WAV_ERR_OPEN, "missing file");
    CHECK(w.data == NULL, "missing file left a buffer");
    FILE *bad = fopen(tmp, "wb"); fputs("NOTRIFFATALL", bad); fclose(bad);
    CHECK(dr32_wav_load(tmp, &w) == DR32_WAV_ERR_FORMAT, "non-RIFF rejected");
    dr32_wav_free(&w);
    dr32_wav_free(&w);                                  // double free must be safe
    remove(tmp);

    // Optional: sweep real device samples for regressions the synthetic set misses.
    if (argc > 1) {
        DIR *d = opendir(argv[1]);
        if (d) {
            struct dirent *ent; int ok = 0, bad_n = 0; size_t total = 0;
            while ((ent = readdir(d))) {
                const char *n = ent->d_name;
                size_t len = strlen(n);
                if (len < 5 || strcmp(n + len - 4, ".wav")) continue;
                char path[1024];
                snprintf(path, sizeof(path), "%s/%s", argv[1], n);
                dr32_wav rw;
                if (dr32_wav_load(path, &rw) == DR32_WAV_OK) { ok++; total += rw.frames; dr32_wav_free(&rw); }
                else { bad_n++; if (bad_n < 5) printf("  could not load %s\n", n); }
            }
            closedir(d);
            printf("  real samples: %d loaded, %d failed, %zu frames total\n", ok, bad_n, total);
            CHECK(bad_n == 0, "%d real device samples failed to load", bad_n);
        }
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
