/*
 * The PER-VOICE render (move_plugin_render_split / dr32_kit_render_split).
 *
 * ⭐ THE PROPERTY THAT MATTERS IS EQUIVALENCE. A host switches between
 * render_block and the split render AT RUNTIME, PER FRAME, by whether any voice
 * is currently assigned to a bus — so assigning one voice and then unassigning
 * it must not change what the other 31 pads sound like. If the two paths do not
 * agree on the unrouted case, every kit changes the moment a user touches a bus,
 * and it would present as "the drums got quieter", nowhere near this code.
 *
 * The comparison is against the EXISTING render, not against a recorded
 * fixture: a fixture would pin whatever the split path happens to do today,
 * which is the opposite of what needs pinning.
 *
 * ⚠ EXACT equality is not the bar and asking for it would be wrong: the two
 * paths convert float->int16 at different points (render_block converts the
 * summed mix once; the split path converts per destination and accumulates
 * saturating), so they legitimately differ by rounding. The bar is that the
 * difference stays at that scale rather than at the scale of a missing pad.
 */

#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_params.h"

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

static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }

/* A 1 s 16-bit mono WAV of constant `level` — loud enough that a dropped pad is
 * unmistakable and a rounding difference is not. */
static void make_wav(const char *path, float level) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = SR * 2;
    fwrite("RIFF", 1, 4, f); w32(f, 36 + data); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); w32(f, 16); w16(f, 1); w16(f, 1);
    w32(f, SR); w32(f, SR * 2); w16(f, 2); w16(f, 16);
    fwrite("data", 1, 4, f); w32(f, data);
    int16_t s = (int16_t)(level * 32767.0f);
    for (uint32_t i = 0; i < SR; i++) w16(f, (uint16_t)s);
    fclose(f);
}

/* A kit with `n` pads loaded and held open, so every block is non-silent. */
static void arm(dr32_kit *k, const char *wav, int n) {
    dr32_kit_init(k);
    for (int i = 0; i < n; i++) {
        dr32_kit_load_sample(k, i, wav);
        k->pads[i].params.hold = DR32_HOLD_INFINITE;
        k->pads[i].params.attack = 0.0001f;
        k->pads[i].params.filter_on = 0;
        k->pads[i].params.vel_to_volume = 0.0f;
    }
    for (int i = 0; i < n; i++) dr32_kit_note_on(k, 36 + i, 100);
}

static void f32_to_i16(const float *src, int16_t *dst, int n) {
    for (int i = 0; i < n; i++) {
        float v = src[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        dst[i] = (int16_t)(v * 32767.0f);
    }
}

static long abs_sum(const int16_t *a, int n) {
    long s = 0;
    for (int i = 0; i < n; i++) s += labs((long)a[i]);
    return s;
}

int main(void) {
    const char *wav = "/tmp/dr32_split.wav";
    make_wav(wav, 0.5f);
    printf("split render\n");

    const int NP = 4;                 /* four loaded pads is enough to route one */
    float fbuf[2 * FR];
    int16_t ref[2 * FR], got[2 * FR];

    /* ---- 1. NOTHING ROUTED OUT: the split path must match render_block ---- */
    {
        dr32_kit a, b;
        arm(&a, wav, NP);
        arm(&b, wav, NP);

        memset(fbuf, 0, sizeof fbuf);
        dr32_kit_render(&a, fbuf, FR);
        f32_to_i16(fbuf, ref, 2 * FR);

        /* Every voice_out entry NULL = the host asked about no voices, so every
         * pad stays in the kit. This is the state a slot is in until the user
         * makes a bus, and it is by far the most common one. */
        int16_t *vo[DR32_PADS];
        for (int i = 0; i < DR32_PADS; i++) vo[i] = NULL;
        memset(got, 0, sizeof got);
        dr32_kit_render_split(&b, vo, DR32_PADS, got, FR);

        long r = abs_sum(ref, 2 * FR), g = abs_sum(got, 2 * FR);
        CHECK(r > 1000, "the reference render is silent — the rig proves nothing (%ld)", r);
        long diff = labs(r - g);
        /* Rounding-scale, not pad-scale: one of four pads is ~25% of the sum. */
        CHECK(diff * 100 < r, "unrouted split differs from render_block by %ld/%ld", diff, r);
    }

    /* ---- 2. ROUTING A PAD OUT MOVES ITS AUDIO, and does not duplicate it -- */
    {
        dr32_kit k;
        arm(&k, wav, NP);

        int16_t pad0[2 * FR];
        memset(pad0, 0, sizeof pad0);
        memset(got, 0, sizeof got);
        int16_t *vo[DR32_PADS];
        for (int i = 0; i < DR32_PADS; i++) vo[i] = NULL;
        vo[0] = pad0;                              /* pad 1 leaves the kit */
        dr32_kit_render_split(&k, vo, DR32_PADS, got, FR);

        CHECK(abs_sum(pad0, 2 * FR) > 1000, "the routed pad produced no audio");
        CHECK(abs_sum(got, 2 * FR) > 1000, "main lost the other three pads");

        /* And the routed pad is GONE from main rather than in both places —
         * the failure that would read as "it got louder when I bussed it". */
        dr32_kit whole;
        arm(&whole, wav, NP);
        memset(fbuf, 0, sizeof fbuf);
        dr32_kit_render(&whole, fbuf, FR);
        f32_to_i16(fbuf, ref, 2 * FR);
        CHECK(abs_sum(got, 2 * FR) < abs_sum(ref, 2 * FR),
              "main is not smaller with a pad routed out (%ld vs %ld)",
              abs_sum(got, 2 * FR), abs_sum(ref, 2 * FR));
    }

    /* ---- 3. ALIASING: two pads on ONE destination sum, never overwrite ---- */
    {
        dr32_kit one, two;
        arm(&one, wav, NP);
        arm(&two, wav, NP);

        int16_t shared[2 * FR], solo[2 * FR];
        int16_t *vo[DR32_PADS];

        for (int i = 0; i < DR32_PADS; i++) vo[i] = NULL;
        vo[0] = solo;
        memset(solo, 0, sizeof solo);
        memset(got, 0, sizeof got);
        dr32_kit_render_split(&one, vo, DR32_PADS, got, FR);

        /* The host hands two voices on one bus the SAME pointer. If the split
         * render overwrote instead of accumulating, the second pad would erase
         * the first and this would match the one-pad case exactly. */
        for (int i = 0; i < DR32_PADS; i++) vo[i] = NULL;
        vo[0] = shared; vo[1] = shared;
        memset(shared, 0, sizeof shared);
        memset(got, 0, sizeof got);
        dr32_kit_render_split(&two, vo, DR32_PADS, got, FR);

        CHECK(abs_sum(shared, 2 * FR) > abs_sum(solo, 2 * FR),
              "two pads sharing a buffer did not sum (%ld vs one pad %ld)",
              abs_sum(shared, 2 * FR), abs_sum(solo, 2 * FR));
    }

    /* ---- 4. A SHORT n_voices IS NOT A DROPPED PAD ------------------------ */
    {
        dr32_kit k;
        arm(&k, wav, NP);
        int16_t *vo[1] = { NULL };
        memset(got, 0, sizeof got);
        /* An older or stingier host asking about one voice must still hear all
         * four: anything it did not ask about falls back to main. */
        dr32_kit_render_split(&k, vo, 1, got, FR);
        CHECK(abs_sum(got, 2 * FR) > 1000, "a short n_voices silenced the kit");
    }

    printf(failures ? "FAILED (%d checks, %d failures)\n" : "PASSED (%d checks, %d failures)\n",
           checks, failures);
    return failures ? 1 : 0;
}
