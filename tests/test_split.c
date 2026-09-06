// Per-voice render: the split_voices list DR32 publishes and the
// move_plugin_render_split entry point Schwung's bus routing dlsym's.
//
// The failures these are written against are the ones the contract says a
// module ported from a single-output render walks into by default: clearing a
// destination the host already cleared (and that another voice is sharing),
// overwriting instead of accumulating, and a list whose ids move when the kit
// does. None of them is visible from inside DR32 — they show up as another
// bus's audio going missing — so they are pinned here.
//
// The other half of this file is the DESK SEMANTIC: a voice routed to a bus
// leaves the kit's drum bus, and a voice left alone stays on it and is glued.
// That is an ordering property of render_split (voices, then returns, then the
// glue in place) and it is invisible from any single buffer — you can only see
// it by comparing a glued run against an unglued one.

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

    // ------------------------------------------------ the Drum Bus is the desk's
    // A voice you route to a bus LEAVES the kit's drum bus; a voice you leave
    // alone stays on it. Both halves are measured the same way: render once
    // with a violently non-neutral Drum Bus and once with it neutral, and ask
    // whether the buffer moved. Glue that ran shows up as a difference; glue
    // that never met the audio cannot.
    {
        /* Enough compression and crunch that any signal through this bus is
         * unrecognisable — the test is "did the glue touch it", so the setting
         * only has to be far from transparent. */
        struct { const char *key, *val; } glue[] = {
            { "bus_comp", "1" }, { "bus_crunch", "1" },
            { "bus_attack", "1" }, { "bus_sustain", "1" }, { "bus_mix", "1" },
        };

        /* which: 0 = pad 1 unrouted (main), 1 = pad 1 routed to its own bus,
         *        2 = every pad routed away.
         * glued: run the Drum Bus, or leave it neutral. */
        int16_t out[3][2][2 * FRAMES];
        int16_t bus[2 * FRAMES];

        for (int which = 0; which < 3; which++) {
            for (int glued = 0; glued < 2; glued++) {
                void *inst = api->create_instance("src", NULL);
                if (!inst) { CHECK(0, "create_instance"); break; }
                api->set_param(inst, "pad0_sample", wa);
                /* Sends are on ONLY for the all-bused case, where they are the
                 * only thing left on main. Off for the other two, so what is
                 * measured there is the DRY pad and nothing else: with a
                 * return in the buffer, a glue that saw only the returns
                 * (the bug this replaces) would still show a difference and
                 * the check would pass for the wrong reason. */
                if (which == 2) {
                    api->set_param(inst, "pad0_send1", "-6");
                    api->set_param(inst, "send1_type", "Plate");
                    api->set_param(inst, "send1_return", "0.9");
                }
                if (glued)
                    for (unsigned g = 0; g < sizeof(glue) / sizeof(glue[0]); g++)
                        api->set_param(inst, glue[g].key, glue[g].val);

                int16_t *main_out = out[which][glued];
                int16_t *voices[DR32_PADS];
                for (int i = 0; i < DR32_PADS; i++)
                    voices[i] = (which == 2) ? bus : main_out;
                if (which == 1) voices[0] = bus;

                note_on(api, inst, 36);
                /* Long enough for the Plate's return to have built up — one
                 * block in, a reverb has emitted nothing and "main carries the
                 * returns" is unmeasurable. The host clears the destinations
                 * every block; the last one is what is compared. */
                for (int b = 0; b < 40; b++) {
                    memset(main_out, 0, sizeof(int16_t) * 2 * FRAMES);
                    memset(bus, 0, sizeof(bus));
                    move_plugin_render_split(inst, voices, DR32_PADS, main_out, FRAMES);
                }
                api->destroy_instance(inst);
            }
        }

        int moved[3];
        for (int which = 0; which < 3; which++) {
            int worst = 0;
            for (int i = 0; i < 2 * FRAMES; i++) {
                int d = abs((int)out[which][1][i] - (int)out[which][0][i]);
                if (d > worst) worst = d;
            }
            moved[which] = worst;
        }

        /* An unassigned voice IS glued — this is the bug the first cut had,
         * where the glue started from silence and only ever saw the returns. */
        CHECK(moved[0] > 100,
              "an unrouted pad did not reach the Drum Bus (worst %d LSB)", moved[0]);

        /* Route that same pad to a bus and its audio must not appear on main at
         * all, glued or not. With no sends in this case main is left holding
         * literally nothing. */
        CHECK(peak_abs(out[0][0], 2 * FRAMES) > 100, "the unrouted pad was silent");
        CHECK(peak_abs(out[1][0], 2 * FRAMES) == 0,
              "a routed pad still landed on main (peak %d)",
              peak_abs(out[1][0], 2 * FRAMES));
        CHECK(peak_abs(out[1][1], 2 * FRAMES) == 0,
              "a routed pad landed on main once the glue was live (peak %d)",
              peak_abs(out[1][1], 2 * FRAMES));

        /* And with every pad routed away, main is the returns only — which
         * still go through the glue. Correct under this semantic, not a bug. */
        CHECK(peak_abs(out[2][0], 2 * FRAMES) > 0,
              "all-bused main carried no send return at all");
        CHECK(moved[2] > 0,
              "all-bused: the glue did not process the returns (worst %d LSB)",
              moved[2]);
    }

    // --------------------------- an unrouted kit is still the mixed render
    // With nothing routed away, split and render_block differ in nothing but
    // the int16 round trip the in-place glue costs. This is the assertion that
    // would have failed on the old "glue starts from silence" render with any
    // non-neutral Drum Bus, and it is why the glue runs in the pre-master-gain
    // domain.
    {
        int16_t split_out[2 * FRAMES], mixed_out[2 * FRAMES];
        for (int which = 0; which < 2; which++) {
            void *inst = api->create_instance("src", NULL);
            if (!inst) { CHECK(0, "create_instance"); break; }
            api->set_param(inst, "pad0_sample", wa);
            api->set_param(inst, "pad1_sample", wb);
            api->set_param(inst, "pad1_send1", "-6");
            api->set_param(inst, "send1_type", "Plate");
            api->set_param(inst, "send1_return", "0.8");
            api->set_param(inst, "bus_comp", "0.7");
            api->set_param(inst, "bus_crunch", "0.4");
            api->set_param(inst, "bus_mix", "0.9");
            api->set_param(inst, "master", "0.8");

            int16_t *dst = which ? mixed_out : split_out;
            memset(dst, 0, sizeof(int16_t) * 2 * FRAMES);
            note_on(api, inst, 36);
            note_on(api, inst, 37);

            if (which) {
                api->render_block(inst, dst, FRAMES);
            } else {
                int16_t *voices[DR32_PADS];
                for (int i = 0; i < DR32_PADS; i++) voices[i] = dst;
                move_plugin_render_split(inst, voices, DR32_PADS, dst, FRAMES);
            }
            api->destroy_instance(inst);
        }
        int worst = 0;
        for (int i = 0; i < 2 * FRAMES; i++) {
            int d = abs((int)split_out[i] - (int)mixed_out[i]);
            if (d > worst) worst = d;
        }
        /* Slack: per-voice int16 rounding on the way in, plus the one extra
         * quantisation the in-place glue's read-back costs. */
        CHECK(worst <= 4,
              "unrouted split != render_block with a live Drum Bus (worst %d LSB)",
              worst);
        printf("  unrouted split vs mixed: worst %d LSB\n", worst);
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED",
           checks, failures);
    return failures ? 1 : 0;
}
