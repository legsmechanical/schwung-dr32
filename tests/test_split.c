// Per-voice render: the split_voices list DR32 publishes and the
// move_plugin_render_split entry point Schwung's bus routing dlsym's.
//
// The failures these are written against are the ones the contract says a
// module ported from a single-output render walks into by default: clearing a
// destination the host already cleared (and that another voice is sharing),
// overwriting instead of accumulating, and a list whose ids move when the kit
// does. None of them is visible from inside DR32 — they show up as another
// bus's audio going missing — so they are pinned here.

#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_params.h"
#include "../dsp/host/plugin_api_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exported by dsp/dr32.c. render_split is a free symbol on purpose — it is
 * never a field on plugin_api_v2_t — so the test resolves it the same way the
 * chain host does: by name. */
extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
extern void move_plugin_render_split(void *instance, int16_t *const *voice_out,
                                     int n_voices, int16_t *main_out, int frames);

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define SR 44100
#define FRAMES 128

static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }

/** A 1 s 16-bit mono WAV of constant `level`. */
static void make_wav(const char *path, float level) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = SR * 2;
    fputs("RIFF", f); w32(f, 4 + 24 + 8 + data); fputs("WAVE", f);
    fputs("fmt ", f); w32(f, 16); w16(f, 1); w16(f, 1); w32(f, SR);
    w32(f, SR * 2); w16(f, 2); w16(f, 16);
    fputs("data", f); w32(f, data);
    for (int i = 0; i < SR; i++) w16(f, (uint16_t)(int16_t)(level * 32767.0f));
    fclose(f);
}

static void note_on(plugin_api_v2_t *api, void *inst, int note) {
    uint8_t m[3] = { 0x90, (uint8_t)note, 100 };
    api->on_midi(inst, m, 3, 0);
}

/** Count non-overlapping occurrences of `needle`. */
static int count(const char *hay, const char *needle) {
    int n = 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle)) n++;
    return n;
}

static int16_t peak_abs(const int16_t *b, int n) {
    int16_t p = 0;
    for (int i = 0; i < n; i++) { int a = abs(b[i]); if (a > p) p = (int16_t)a; }
    return p;
}

int main(void) {
    printf("split render\n");

    const char *wa = "/tmp/dr32_split_a.wav", *wb = "/tmp/dr32_split_b.wav";
    /* Quiet on purpose: the two together must stay well inside full scale, or
     * the sum this file is about is a clip and every buffer agrees for the
     * wrong reason. */
    make_wav(wa, 0.25f);
    make_wav(wb, 0.1f);

    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    CHECK(api != NULL, "no v2 api");
    if (!api) return 1;

    // ---------------------------------------------------------------- the list
    {
        void *inst = api->create_instance("src", NULL);
        CHECK(inst != NULL, "create_instance");
        if (inst) {
            char js[8192];
            api->set_param(inst, "pad0_sample", wa);
            int n = api->get_param(inst, "split_voices", js, (int)sizeof(js));

            CHECK(n > 0, "split_voices answered %d — presence of the key is the "
                         "opt-in, so nothing routes without it", n);
            /* The chain host reads this through a 4096-byte buffer and a short
             * read is SILENT — it just sees fewer voices. */
            CHECK(n < 4096, "split_voices is %d bytes, over the host's 4096", n);
            CHECK((int)strlen(js) == n, "returned length %d != strlen %d",
                  n, (int)strlen(js));
            printf("  split_voices: %d bytes of the host's 4096\n", n);

            /* Entry i is buffer i, so there must be exactly one entry per pad
             * and the first must be pad 1. */
            CHECK(count(js, "\"id\":") == DR32_PADS,
                  "%d entries, expected %d", count(js, "\"id\":"), DR32_PADS);
            CHECK(strncmp(js, "[{\"id\":\"pad1\",", 14) == 0,
                  "list does not start at pad1: '%.24s'", js);
            CHECK(strstr(js, "\"id\":\"pad32\"") != NULL, "no pad32 entry");

            /* A loaded pad is labelled by its sample, an empty one by its
             * position. */
            CHECK(strstr(js, "\"label\":\"dr32_split_a\"") != NULL,
                  "pad 1 is not labelled by its sample: %.120s", js);
            CHECK(strstr(js, "\"id\":\"pad2\",\"label\":\"Pad 2\"") != NULL,
                  "empty pad 2 is not labelled 'Pad 2'");

            /* THE point of positional ids: swapping a pad's sample — which is
             * what loading a kit does to all 32 at once — must not move an id,
             * or every bus assignment in the set is orphaned. */
            api->set_param(inst, "pad0_sample", wb);
            char js2[8192];
            api->get_param(inst, "split_voices", js2, (int)sizeof(js2));
            CHECK(count(js2, "\"id\":") == DR32_PADS, "id count changed with the kit");
            CHECK(strncmp(js2, "[{\"id\":\"pad1\",", 14) == 0, "pad1's id moved");
            CHECK(strstr(js2, "\"label\":\"dr32_split_b\"") != NULL,
                  "the label did not follow the sample");

            api->destroy_instance(inst);
        }
    }

    // ------------------------------------------- a long / hostile sample name
    {
        dr32_kit k;
        dr32_kit_init(&k);
        snprintf(k.pads[0].path, sizeof(k.pads[0].path),
                 "/x/a \"quoted\" name that runs on well past the cap.wav");
        char js[8192];
        int n = dr32_split_voices_json(&k, js, (int)sizeof(js));
        CHECK(n > 0 && n < 4096, "hostile name blew the budget: %d", n);
        /* An unescaped quote would end the value early in the host's scan,
         * which has no escape handling at all. */
        CHECK(count(js, "\"") == 8 * DR32_PADS, "unbalanced quoting: %.80s", js);
        CHECK(strstr(js, "\\\"") == NULL, "an escape reached the host's flat scan");
        dr32_kit_free(&k);
    }

    // ------------------------------------------------ a destination is NEVER cleared
    {
        void *inst = api->create_instance("src", NULL);
        CHECK(inst != NULL, "create_instance");
        if (inst) {
            int16_t main_out[2 * FRAMES];
            for (int i = 0; i < 2 * FRAMES; i++) main_out[i] = 1000;
            int16_t *voices[DR32_PADS];
            for (int i = 0; i < DR32_PADS; i++) voices[i] = main_out;

            /* Silent kit: nothing to accumulate, so every sample must come back
             * exactly as the caller left it. A memset anywhere in the render —
             * the carry-over mistake from a single-output port — deletes
             * whatever another voice already put there, and this is what sees
             * it. */
            move_plugin_render_split(inst, voices, DR32_PADS, main_out, FRAMES);
            int intact = 1;
            for (int i = 0; i < 2 * FRAMES; i++) if (main_out[i] != 1000) intact = 0;
            CHECK(intact, "render_split cleared or overwrote its destination");
            api->destroy_instance(inst);
        }
    }

    // ------------------------------- two voices on one buffer SUM; parity with mixed
    {
        int16_t solo0[2 * FRAMES], solo1[2 * FRAMES], both[2 * FRAMES];
        int16_t mixed[2 * FRAMES];
        int16_t sink[2 * FRAMES];
        int16_t *voices[DR32_PADS];

        /* Each instance is driven identically and independently, so any
         * difference between them is the render path and nothing else. */
        for (int which = 0; which < 4; which++) {
            void *inst = api->create_instance("src", NULL);
            if (!inst) { CHECK(0, "create_instance"); break; }
            api->set_param(inst, "pad0_sample", wa);
            api->set_param(inst, "pad1_sample", wb);

            int16_t *dst = which == 0 ? solo0 : which == 1 ? solo1
                         : which == 2 ? both : mixed;
            memset(dst, 0, sizeof(int16_t) * 2 * FRAMES);
            memset(sink, 0, sizeof(sink));

            if (which == 0) note_on(api, inst, 36);
            else if (which == 1) note_on(api, inst, 37);
            else { note_on(api, inst, 36); note_on(api, inst, 37); }

            if (which == 3) {
                api->render_block(inst, dst, FRAMES);
            } else {
                /* pad1 and pad2 share one destination — the aliasing the host
                 * hands out when two voices are on the same bus. Everything
                 * else goes to a sink that is not it. */
                for (int i = 0; i < DR32_PADS; i++) voices[i] = sink;
                voices[0] = dst;
                voices[1] = dst;
                move_plugin_render_split(inst, voices, DR32_PADS, sink, FRAMES);
            }
            api->destroy_instance(inst);
        }

        CHECK(peak_abs(solo0, 2 * FRAMES) > 100, "pad 1 produced no audio");
        CHECK(peak_abs(solo1, 2 * FRAMES) > 100, "pad 2 produced no audio");

        int worst_sum = 0, worst_mix = 0;
        for (int i = 0; i < 2 * FRAMES; i++) {
            int d = abs((int)both[i] - ((int)solo0[i] + (int)solo1[i]));
            if (d > worst_sum) worst_sum = d;
            int m = abs((int)both[i] - (int)mixed[i]);
            if (m > worst_mix) worst_mix = m;
        }
        /* Both voices were handed the same pointer, so their sum happened
         * inside our own render with no mixing pass. Slack is the per-voice
         * int16 rounding that summing in the destination costs. */
        CHECK(worst_sum <= 2, "aliased voices did not sum (worst %d LSB)", worst_sum);
        /* And that sum is the mixed render, which is the property that lets the
         * host switch entry points per frame without it being audible. */
        CHECK(worst_mix <= 2, "split != render_block (worst %d LSB)", worst_mix);
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED",
           checks, failures);
    return failures ? 1 : 0;
}
