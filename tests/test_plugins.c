#define _GNU_SOURCE

// Engines ANOTHER MODULE brings (dsp/dr32_plugins.c, dsp/dr32_engine_api.h),
// loaded the way the device loads them: a `dr32_engine.so` in a sibling module
// folder, found by DR32 on its own, with no build-time knowledge of it.
//
// tests/run.sh lays the tree out before this runs:
//
//   dist/tests/plug/dr32  -> src        DR32's own module dir
//   dist/tests/plug/toy/dr32_engine.so  a good plugin   (fixtures/plugin)
//   dist/tests/plug/bad/dr32_engine.so  nine knobs on one page: refused WHOLE
//   dist/tests/plug/junk/dr32_engine.so not a shared object at all
//
// The property under test is the one a module author cares about: drop the
// file in, and the models are in the picker, play, have pages, save and copy —
// and a plugin that breaks a rule costs DR32 nothing.

#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_plugin_validate.h"
#include "../dsp/host/plugin_api_v1.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

/* dsp/dr32.c's DR32_FOCUS_SETTLE_BLOCKS: how long is_loading holds after focus
 * lands on an engine that was not being served. */
#define DR32_TEST_FOCUS_BLOCKS 20

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char logbuf[8192];
static void hostlog(const char *m) { strncat(logbuf, m, sizeof(logbuf) - strlen(logbuf) - 2); strcat(logbuf, "\n"); }

static int count(const char *h, const char *needle) {
    int n = 0;
    for (const char *q = h; (q = strstr(q, needle)); q++) n++;
    return n;
}

int main(void) {
    printf("test_plugins\n");
    static host_api_v1_t host;
    host.api_version = 1;
    host.sample_rate = 44100;
    host.frames_per_block = 128;
    host.log = hostlog;
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    void *inst = api ? api->create_instance("dist/tests/plug/dr32", NULL) : NULL;
    CHECK(inst != NULL, "create_instance returned NULL");
    if (!inst) return 1;

    static char h[131072];
    char v[256];
    static int16_t out[2 * 128];
    #define GET(k) (v[0] = '\0', api->get_param(inst, k, v, (int)sizeof v), v)

    /* ---- 1. found, without being asked for ----------------------------- */
    int builtin = dr32_model_count() - dr32_plugin_model_count();
    dr32_plugins_wait();
    CHECK(dr32_plugins_ready(), "the scan never published");
    CHECK(dr32_plugin_engine_count() == 15, "%d plugin engines; toy has 1, zraw 2, zzbig 12, and the bad one must add none", dr32_plugin_engine_count());
    CHECK(dr32_plugin_model_count() == 16, "%d plugin models, want toy's 2, zraw's 2 and zzbig's 12", dr32_plugin_model_count());
    CHECK(dr32_model_count() == builtin + 16, "the plugins' models are not in the model list");
    /* Ours do not move: a pad holds a model INDEX. */
    CHECK(dr32_model_find("simian/kick") == 0, "a built-in model moved when a plugin was installed");
    int mi = dr32_model_find("toy/low");
    CHECK(mi == builtin, "toy/low is model %d, want %d (right after ours)", mi, builtin);
    const dr32_engine_ops *e = dr32_engine_get(DR32_ENG_COUNT);
    CHECK(e && e->family == DR32_FAM_PLUGIN && e->nparams == 4, "the toy engine is not registered at the first plugin id");
    CHECK(e && !strcmp(e->params[0].key, "x_toy_sine_pitch"), "full key is '%s', want x_toy_sine_pitch", e ? e->params[0].key : "");

    /* The picker reads this. */
    api->get_param(inst, "plugin_models", h, (int)sizeof h);
    {
        /* Our sections first, in folder order; the big fixture's twelve follow. */
        static const char want[] =
            "[{\"id\":\"toy\",\"label\":\"Toy\",\"models\":[{\"slug\":\"toy/low\",\"name\":\"Toy Low\"},"
            "{\"slug\":\"toy/high\",\"name\":\"Toy High\"}]},"
            "{\"id\":\"zraw\",\"label\":\"Z Raw\",\"models\":[{\"slug\":\"zraw/raw\",\"name\":\"Raw\"},"
            "{\"slug\":\"zraw/late\",\"name\":\"Late\"}]},"
            "{\"id\":\"zzbig\",\"label\":\"Big\",\"models\":[{\"slug\":\"zzbig/e0\",";
        CHECK(!strncmp(h, want, sizeof(want) - 1), "plugin_models: %.300s", h);
    }

    /* ---- 2. no plugin pad: nothing of the plugin's is served ------------ */
    int n0 = api->get_param(inst, "ui_hierarchy", h, (int)sizeof h);
    CHECK(n0 > 2 && !strstr(h, "x_toy_"), "the toy's keys are served with no pad running it");

    /* ---- 3. a pad runs it ---------------------------------------------- */
    api->set_param(inst, "pad3_model", "toy/low");
    CHECK(!strcmp(GET("pad3_model"), "toy/low"), "pad3_model reads '%s'", v);
    api->set_param(inst, "ui_current_pad", "3");
    CHECK(atoi(GET("ui_engine")) == DR32_ENG_COUNT, "ui_engine is %s, want %d", v, DR32_ENG_COUNT);
    CHECK(atoi(GET("ui_family")) == DR32_FAM_PLUGIN, "ui_family is %s, want %d", v, DR32_FAM_PLUGIN);
    CHECK(!strcmp(GET("is_loading"), "1"), "a plugin model did not arm is_loading");
    /* The model's values and level are the pad's. */
    CHECK(atof(GET("pad3_x_toy_sine_pitch")) == 60.0, "pitch reads %s, want the model's 60", v);
    CHECK(!strcmp(GET("pad3_x_toy_sine_wave"), "Sine"), "the enum reads '%s', want its option's name", v);
    CHECK(fabs(atof(GET("pad3_volume")) + 6.0) < 1e-3, "pad volume is %s, want the model's -6 dB", v);

    /* It sounds, and a knob reaches it: 60 Hz vs 300 Hz by zero crossings. */
    int zc[2] = {0, 0};
    for (int pass = 0; pass < 2; pass++) {
        if (pass) api->set_param(inst, "pad3_x_toy_sine_pitch", "300");
        uint8_t on[3] = { 0x90, 38, 100 };
        api->on_midi(inst, on, 3, 0);
        double peak = 0;
        int16_t prev = 0;
        for (int b = 0; b < 40; b++) {
            api->render_block(inst, out, 128);
            for (int i = 0; i < 128; i++) {
                int16_t s = out[2 * i];
                if (abs(s) > peak) peak = abs(s);
                if ((prev < 0) != (s < 0)) zc[pass]++;
                prev = s;
            }
        }
        CHECK(peak > 500, "pass %d: the plugin pad is silent (peak %g)", pass, peak);
    }
    CHECK(zc[1] > 3 * zc[0], "the pitch knob did not reach the engine (%d vs %d crossings)", zc[0], zc[1]);
    CHECK(atof(GET("pad3_x_toy_sine_pitch")) == 300.0, "pitch reads back %s after the write", v);

    /* ---- 4. its pages, built from the template -------------------------- */
    int n1 = api->get_param(inst, "ui_hierarchy", h, (int)sizeof h);
    CHECK(n1 > n0, "the hierarchy did not grow when a plugin engine came into use");
    CHECK(strstr(h, "\"eng_x_toy_sine_tone\":{\"name\":\"Tone\",\"visible_if\":{\"param\":\"ui_engine\",\"equals\":59}") != NULL,
          "the Tone page is missing or mis-gated");
    CHECK(strstr(h, "\"eng_x_toy_sine_color\":{\"name\":\"Color\"") != NULL, "the second page (Color) is missing");
    CHECK(strstr(h, "{\"level\":\"eng_x_toy_sine_tone\",\"label\":\"Tone\"}") != NULL, "the Tone page has no root nav entry");
    CHECK(strstr(h, "{\"key\":\"x_toy_sine_wave\",\"name\":\"Wave\",\"short_name\":\"WAVE\",\"type\":\"enum\","
                    "\"options\":[\"Sine\",\"Square\"],\"default\":\"Sine\"}") != NULL, "the enum is not declared as DR32 declares its own");
    CHECK(strstr(h, "{\"key\":\"x_toy_sine_tone\",\"name\":\"Tone\",\"short_name\":\"TONE\",\"type\":\"float\","
                    "\"min\":0,\"max\":1,\"default\":0.5,\"step\":0.01}") != NULL, "the float knob is not declared with its step");
    CHECK(strstr(h, "\"knobs\":[\"x_toy_sine_pitch\",\"x_toy_sine_decay\",\"x_toy_sine_wave\"]") != NULL, "Tone's knobs are wrong");
    CHECK(!strstr(h, "@"), "a template token was left in the served hierarchy");
    /* Copy acts on the level you stand on: the four base banks and the toy's
     * two pages each carry its keys, right after `model`. */
    CHECK(count(h, "\"sample\",\"model\",\"x_toy_sine_pitch\",\"x_toy_sine_decay\",\"x_toy_sine_wave\",\"x_toy_sine_tone\",") == 6,
          "the toy's keys are in %d copy lists, want 6 (4 base banks + its 2 pages)",
          count(h, "\"sample\",\"model\",\"x_toy_sine_pitch\""));
    CHECK(count(h, "\"child_names\": [") == count(h, "\"child_index_param\""), "a plugin page lost its pad names");
    FILE *f = fopen("dist/tests/served_plugin_hierarchy.json", "w");
    if (f) { fputs(h, f); fclose(f); }

    /* ---- 5. it saves, and comes back ------------------------------------ */
    static char st[65536];
    int sn = api->get_param(inst, "state", st, (int)sizeof st);
    CHECK(sn > 2 && strstr(st, "toy/low") && strstr(st, "x_toy_sine_pitch"), "the state blob does not carry the plugin pad");
    void *inst2 = api->create_instance("dist/tests/plug/dr32", NULL);
    if (inst2) {
        api->set_param(inst2, "state", st);
        v[0] = '\0'; api->get_param(inst2, "pad3_model", v, (int)sizeof v);
        CHECK(!strcmp(v, "toy/low"), "restored pad3_model is '%s'", v);
        v[0] = '\0'; api->get_param(inst2, "pad3_x_toy_sine_pitch", v, (int)sizeof v);
        CHECK(atof(v) == 300.0, "restored pitch is %s, want the edited 300", v);
        api->destroy_instance(inst2);
    }

    /* ---- 6. the pad goes back to a sample: its pages and keys leave ----- */
    api->set_param(inst, "pad3_model", "simian/kick");
    api->get_param(inst, "ui_hierarchy", h, (int)sizeof h);
    CHECK(!strstr(h, "x_toy_"), "the toy's pages or keys outlived its last pad");
    CHECK(strstr(h, "\"eng_sm_tone\"") != NULL, "the built-in engine's pages did not arrive");

    /* ---- 6b. a model whose module is NOT installed is kept, not lost ---- */
    {
        /* A set saved on a Move that had another module's engine, opened on one
         * that does not: the pad cannot play, and must still be THERE — its
         * model, its knobs and its place in the mix — so that the next save
         * does not quietly delete it. */
        void *g = api->create_instance("dist/tests/plug/dr32", NULL);
        const char *wa = "/tmp/dr32_plug_real.wav";
        api->set_param(g, "state",
            "{\"v\":2,\"kit\":\"\",\"params\":{\"pad5_model\":\"ghost/kick\",\"pad5_volume\":\"-3\","
            "\"pad5_x_ghost_a_pitch\":\"77\",\"pad5_x_ghost_a_wave\":\"Square\",\"pad6_model\":\"toy/high\"}}");
        v[0] = 0; api->get_param(g, "pad5_model", v, (int)sizeof v);
        CHECK(!strcmp(v, "ghost/kick"), "a missing model reads '%s', want its slug kept", v);
        v[0] = 0; api->get_param(g, "pad5_sample", v, (int)sizeof v);
        CHECK(!strcmp(v, "ghost/kick missing"), "the pad's name reads '%s', want 'ghost/kick missing'", v);
        v[0] = 0; api->get_param(g, "pad6_model", v, (int)sizeof v);
        CHECK(!strcmp(v, "toy/high"), "an installed model beside it did not load ('%s')", v);
        api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
        CHECK(strstr(h, "\"kick missing\"") != NULL, "the pad is not named as missing in the served hierarchy");
        /* It is silent, and hitting it is safe. */
        uint8_t on[3] = { 0x90, 40, 100 };
        api->on_midi(g, on, 3, 0);
        int loud = 0;
        for (int b = 0; b < 20; b++) { api->render_block(g, out, 128); for (int i = 0; i < 256; i++) if (out[i]) loud = 1; }
        CHECK(!loud, "a pad with a missing model made a sound");
        /* Saved again, nothing of it is gone. */
        int gn = api->get_param(g, "state", st, (int)sizeof st);
        CHECK(gn > 2 && strstr(st, "\"pad5_model\":\"ghost/kick\""), "the missing model was dropped from the next save: %s", st);
        CHECK(strstr(st, "\"pad5_x_ghost_a_pitch\":\"77\"") && strstr(st, "\"pad5_x_ghost_a_wave\":\"Square\""),
              "its saved knobs were dropped from the next save: %s", st);
        CHECK(strstr(st, "\"pad5_volume\":\"-3\""), "its pad volume was dropped from the next save: %s", st);
        CHECK(!strstr(st, "pad5_sample"), "the 'missing' name was saved as a sample path: %s", st);
        /* A second round trip is the same blob: nothing accumulates. */
        void *g2 = api->create_instance("dist/tests/plug/dr32", NULL);
        static char st2[65536];
        api->set_param(g2, "state", st);
        api->get_param(g2, "state", st2, (int)sizeof st2);
        CHECK(!strcmp(st, st2), "the state does not survive a second round trip:\n %s\n %s", st, st2);
        api->destroy_instance(g2);
        /* Giving the pad a real sound ends it. */
        FILE *wf = fopen(wa, "wb");
        if (wf) {
            unsigned char hdr[44] = { 'R','I','F','F', 36+200,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
                                      0x44,0xAC,0,0, 0x88,0x58,1,0, 2,0, 16,0, 'd','a','t','a', 200,0,0,0 };
            static unsigned char pcm[200];
            fwrite(hdr, 1, 44, wf); fwrite(pcm, 1, 200, wf); fclose(wf);
        }
        /* An unknown name in a family that IS installed is not a missing module. */
        api->set_param(g, "pad6_model", "toy/nope");
        v[0] = 0; api->get_param(g, "pad6_model", v, (int)sizeof v);
        CHECK(!strcmp(v, "toy/high"), "an unknown model of an installed plugin replaced the pad ('%s')", v);
        api->set_param(g, "pad5_sample", wa);
        api->get_param(g, "state", st, (int)sizeof st);
        CHECK(!strstr(st, "ghost"), "the missing model outlived a sample loaded onto its pad: %s", st);
        remove(wa);
        api->destroy_instance(g);
    }

    /* ---- 6c. what DR32 does FOR a plugin engine ------------------------- */
    {
        /* `raw` has no choke and never says it has finished; `late` is silent
         * for 250 ms before it sounds. DR32 has to end the first, fade it on a
         * choke, and NOT end the second in its silence. The instance starts
         * with its kit, so the pads' voices can be read. */
        void *g = api->create_instance("dist/tests/plug/dr32", NULL);
        dr32_kit *kit = (dr32_kit *)g;
        api->set_param(g, "pad1_model", "zraw/raw");
        api->set_param(g, "pad2_model", "zraw/late");
        CHECK(kit->pads[0].engine && kit->pads[1].engine, "the zraw models did not load");
        uint8_t hit1[3] = { 0x90, 36, 110 }, hit2[3] = { 0x90, 37, 110 };

        /* raw: sounds, then DR32's gate ends it (its tail is under -80 dB by
         * ~0.6 s; the gate needs 100 ms more). It would otherwise run forever. */
        api->on_midi(g, hit1, 3, 0);
        int peak = 0, ended_at = -1;
        for (int b = 0; b < 700; b++) {
            api->render_block(g, out, 128);
            for (int i = 0; i < 256; i++) if (abs(out[i]) > peak) peak = abs(out[i]);
            if (ended_at < 0 && !kit->pads[0].synth.active) ended_at = b;
        }
        CHECK(peak > 500, "the raw engine is silent (peak %d)", peak);
        CHECK(ended_at > 100 && ended_at < 600, "DR32's gate ended the never-finishing voice at block %d; want after its tail, well before 2 s", ended_at);

        /* raw, choked through its choke group: no `choke` function to call,
         * and the output is silent within DR32's own 3 ms fade. */
        api->set_param(g, "pad1_choke", "1");
        api->set_param(g, "pad3_sample", "");
        api->set_param(g, "pad3_model", "toy/low");
        api->set_param(g, "pad3_choke", "1");
        api->on_midi(g, hit1, 3, 0);
        for (int b = 0; b < 4; b++) api->render_block(g, out, 128);
        uint8_t hit3[3] = { 0x90, 38, 1 };            /* pad 3, as quiet as a hit gets */
        api->set_param(g, "pad3_volume", "-70");
        api->on_midi(g, hit3, 3, 0);
        for (int b = 0; b < 4; b++) api->render_block(g, out, 128);   /* 11 ms: the fade is 3 */
        api->render_block(g, out, 128);
        int after = 0;
        for (int i = 0; i < 256; i++) if (abs(out[i]) > after) after = abs(out[i]);
        CHECK(after < 40, "a choked plugin pad with no choke function is still audible (peak %d)", after);

        /* late: nothing for 250 ms, and the gate must not take that for the end. */
        api->set_param(g, "pad1_model", "zraw/raw");   /* a fresh, silent pad 1 */
        api->on_midi(g, hit2, 3, 0);
        int early = 0, late = 0;
        for (int b = 0; b < 200; b++) {
            api->render_block(g, out, 128);
            int bp = 0;
            for (int i = 0; i < 256; i++) if (abs(out[i]) > bp) bp = abs(out[i]);
            if (b < 80) { if (bp > early) early = bp; } else if (bp > late) late = bp;
        }
        CHECK(early < 40, "the late voice sounded in its silence (peak %d): pad 3's quiet tail is all there should be", early);
        CHECK(late > 500, "a voice that says HOLD was ended in its silence: no burst after 250 ms (peak %d)", late);
        for (int b = 0; b < 400; b++) api->render_block(g, out, 128);
        CHECK(!kit->pads[1].synth.active, "the late voice said DONE and is still being rendered");
        api->destroy_instance(g);
    }

    /* ---- 6d. more engines than fit: the ones in hand are served --------- */
    {
        /* DR32 serves pages for the engines a kit runs, inside a 100 KB budget.
         * Twelve 32-knob engines do not fit it together. All twelve go on pads
         * anyway; what is served is the focused pad's engine and the ones
         * focused most recently, and focusing an engine that is not served
         * rebuilds the document and pulses is_loading so the host re-reads. */
        void *g = api->create_instance("dist/tests/plug/dr32", NULL);
        static char h2[131072];
        char key[32], slug[32], want[64];
        #define FOCUS(e) (snprintf(key, sizeof key, "%d", (e) + 1), api->set_param(g, "ui_current_pad", key))
        #define LOADING() (v[0] = 0, api->get_param(g, "is_loading", v, (int)sizeof v), v[0] == '1')
        #define SERVED(doc, e) (snprintf(want, sizeof want, "\"eng_x_zzbig_e%d_alpha\"", (e)), strstr(doc, want) != NULL)
        #define SETTLE(n) do { for (int b_ = 0; b_ < (n); b_++) api->render_block(g, out, 128); } while (0)
        api->set_param(g, "ui_current_pad", "32");          /* a sample pad: nothing is forced in */
        const char *log0 = logbuf + strlen(logbuf);
        int under_ok = 1, fit = 0;
        for (int e = 0; e < 12; e++) {
            snprintf(key, sizeof key, "pad%d_model", e + 1);
            snprintf(slug, sizeof slug, "zzbig/e%d", e);
            api->set_param(g, key, slug);
            v[0] = 0; api->get_param(g, key, v, (int)sizeof v);
            CHECK(!strcmp(v, slug), "engine %d was not taken (reads '%s'): a kit over the budget is not refused a model", e, v);
            char why[64] = ""; api->get_param(g, "model_refused", why, (int)sizeof why);
            CHECK(!why[0], "model_refused reads '%s' after engine %d was accepted", why, e);
            int hn = api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
            CHECK(hn > 2 && hn < 110000, "after engine %d the hierarchy is %d bytes: the pages are budgeted at 100 KB and this kit's names are short", e, hn);
            CHECK(count(h, "\"child_names\": [") == count(h, "\"child_index_param\""),
                  "after engine %d the served hierarchy lost its pad names (%d bytes)", e, hn);
            int all = 1;
            for (int k = 0; k <= e; k++) all = all && SERVED(h, k);
            if (all && under_ok) fit = e + 1; else under_ok = 0;
        }
        CHECK(fit >= 4 && fit < 12, "%d of the 12 big engines were served together; some must fit and not all can", fit);
        CHECK(strstr(log0, "serving the focused pad's engine and the most recently used"), "going over the budget was not logged");
        CHECK(count(log0, "serving the focused pad's engine") == 1, "going over the budget was logged %d times, want once", count(log0, "serving the focused pad's engine"));
        printf("  %d of these 32-knob engines are served together; a kit of 12 serves the ones in hand\n", fit);
        SETTLE(130);
        (void)LOADING();                                     /* the model writes' own pulse, spent */
        CHECK(!LOADING(), "is_loading still reads 1 long after the last model write");

        /* Every engine can be reached: focus it, and its pages and copy keys are there. */
        int unserved_seen = 0;
        api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
        for (int e = 0; e < 12; e++) {
            /* ⚠ `h` is what the host HOLDS (the last read). Reading the
             * hierarchy again here would rebuild it around the new focus, and
             * there would be nothing left for is_loading to announce. */
            int was = SERVED(h, e);
            unserved_seen += !was;
            FOCUS(e);
            int pulse = LOADING();
            CHECK(pulse == !was, "focus on engine %d (%s before): is_loading read %d", e, was ? "served" : "not served", pulse);
            if (pulse) {
                SETTLE(DR32_TEST_FOCUS_BLOCKS - 2);
                CHECK(LOADING(), "focus on engine %d: is_loading fell before %d blocks; a second reader would miss it", e, DR32_TEST_FOCUS_BLOCKS - 2);
                SETTLE(3);
            }
            CHECK(!LOADING(), "focus on engine %d: is_loading did not fall after %d blocks", e, DR32_TEST_FOCUS_BLOCKS + 1);
            int hn = api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
            CHECK(SERVED(h, e), "engine %d is in focus and its pages are not served", e);
            CHECK(hn < 110000, "focused on engine %d the hierarchy is %d bytes", e, hn);
            CHECK(count(h, "\"child_names\": [") == count(h, "\"child_index_param\""), "focused on engine %d the pad names were lost", e);
            /* Copy copies FROM the focused pad, so its engine's keys must be in the base banks' lists. */
            const char *cl = strstr(h, "\"sample\",\"model\"");
            const char *ce = cl ? strchr(cl, ']') : NULL;
            snprintf(want, sizeof want, "\"x_zzbig_e%d_knob_number_00\"", e);
            const char *ck = cl ? strstr(cl, want) : NULL;
            CHECK(ck && ce && ck < ce, "engine %d is in focus and its keys are not in the base copy list", e);
        }

        CHECK(unserved_seen >= 12 - fit, "only %d of the focus steps landed on an unserved engine; the walk tested nothing", unserved_seen);

        /* ⭑ THE RECENCY ORDER: the engines focused last (11, 10, 9 ...) are the
         * ones served, so going back over them re-reads nothing. */
        api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
        int kept = 0;
        for (int e = 0; e < 12; e++) kept += SERVED(h, e);
        CHECK(kept == fit, "%d engines are served in the 12-engine kit, want the %d that fit", kept, fit);
        for (int e = 12 - fit; e < 12; e++) CHECK(SERVED(h, e), "engine %d was focused among the last %d and is not served", e, fit);
        for (int round = 0; round < 2; round++)
            for (int e = 12 - fit; e < 12; e++) {
                FOCUS(e);
                CHECK(!LOADING(), "moving among served engines pulsed is_loading (engine %d)", e);
                api->get_param(g, "ui_hierarchy", h2, (int)sizeof h2);
                CHECK(!strcmp(h, h2), "moving among served engines changed the document (engine %d)", e);
            }
        /* One step outside, and it is the OLDEST of them that gives way. */
        FOCUS(0);
        CHECK(LOADING(), "focus on an unserved engine did not pulse is_loading");
        api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
        CHECK(SERVED(h, 0) && !SERVED(h, 12 - fit), "engine 0 took focus: served 0=%d, the oldest (%d)=%d; want 1 and 0", SERVED(h, 0), 12 - fit, SERVED(h, 12 - fit));
        for (int e = 12 - fit + 1; e < 12; e++) CHECK(SERVED(h, e), "engine %d was dropped though an older one could go", e);
        /* The net under the pulse: a host that reads the hierarchy before it
         * polls gets the focused engine's pages from that read. */
        SETTLE(DR32_TEST_FOCUS_BLOCKS + 1); (void)LOADING();
        FOCUS(1);
        CHECK(!SERVED(h, 1), "engine 1 should have been unserved here; the check below tests nothing");
        api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
        CHECK(SERVED(h, 1), "a hierarchy read with an unserved engine in focus did not serve it");
        /* A sample pad in focus asks for nothing. */
        SETTLE(DR32_TEST_FOCUS_BLOCKS + 1); (void)LOADING();
        api->set_param(g, "ui_current_pad", "32");
        CHECK(!LOADING(), "focus on a sample pad pulsed is_loading");
        api->get_param(g, "ui_hierarchy", h2, (int)sizeof h2);
        CHECK(!strcmp(h, h2), "focus on a sample pad changed the document");

        /* Both render paths and a saved kit: the set comes back whole, over budget or not. */
        static char st[65536];
        int sn = api->get_param(g, "state", st, (int)sizeof st);
        CHECK(sn > 0, "no state from the 12-engine kit");
        void *r = api->create_instance("dist/tests/plug/dr32", NULL);
        api->set_param(r, "state", st);
        for (int e = 0; e < 12; e++) {
            snprintf(key, sizeof key, "pad%d_model", e + 1);
            snprintf(slug, sizeof slug, "zzbig/e%d", e);
            v[0] = 0; api->get_param(r, key, v, (int)sizeof v);
            CHECK(!strcmp(v, slug), "restored: pad %d reads '%s', want %s", e + 1, v, slug);
        }
        int rn = api->get_param(r, "ui_hierarchy", h2, (int)sizeof h2);
        CHECK(rn > 2 && rn < 110000, "restored: the hierarchy is %d bytes", rn);
        api->destroy_instance(r);

        /* Back under the budget, every engine is served whatever is in focus. */
        for (int e = fit; e < 12; e++) {
            snprintf(key, sizeof key, "pad%d_model", e + 1);
            api->set_param(g, key, "toy/high");
        }
        api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
        for (int e = 0; e < fit; e++) CHECK(SERVED(h, e), "under the budget again and engine %d is not served", e);
        SETTLE(130); (void)LOADING();
        for (int e = 0; e < fit; e++) {
            FOCUS(e);
            CHECK(!LOADING(), "under the budget, focus on engine %d pulsed is_loading", e);
            api->get_param(g, "ui_hierarchy", h2, (int)sizeof h2);
            CHECK(!strcmp(h, h2), "under the budget, focus on engine %d changed the document", e);
        }
        api->destroy_instance(g);
        #undef FOCUS
        #undef LOADING
        #undef SERVED
        #undef SETTLE
    }

    /* ---- 6e. an engine too big to serve ALONE is refused ----------------- */
    {
        /* The backstop. No plugin that passes the rules is this big, so the
         * budget is brought down to where one is: room for the toy, not for a
         * 32-knob engine. The pad must be left exactly as it was — the
         * alternative is a synth pad that plays and can never have pages. */
        void *g = api->create_instance("dist/tests/plug/dr32", NULL);
        int empty = api->get_param(g, "ui_hierarchy", h, (int)sizeof h);
        api->destroy_instance(g);
        char budget[16];
        snprintf(budget, sizeof budget, "%d", empty + 5000);
        setenv("DR32_PAGE_BUDGET", budget, 1);
        g = api->create_instance("dist/tests/plug/dr32", NULL);
        unsetenv("DR32_PAGE_BUDGET");
        api->set_param(g, "pad12_model", "toy/low");
        api->set_param(g, "pad12_x_toy_sine_pitch", "222");
        v[0] = 0; api->get_param(g, "pad12_model", v, (int)sizeof v);
        CHECK(!strcmp(v, "toy/low"), "the small engine was refused under a %s byte budget (reads '%s')", budget, v);
        api->set_param(g, "pad12_model", "zzbig/e3");
        v[0] = 0; api->get_param(g, "pad12_model", v, (int)sizeof v);
        CHECK(!strcmp(v, "toy/low"), "the refused pad did not keep its model (reads '%s')", v);
        v[0] = 0; api->get_param(g, "pad12_x_toy_sine_pitch", v, (int)sizeof v);
        CHECK(atof(v) == 222.0, "the refused pad lost its knob value (reads %s)", v);
        char why[64] = ""; api->get_param(g, "model_refused", why, (int)sizeof why);
        CHECK(!strcmp(why, "12:zzbig/e3"), "model_refused reads '%s', want '12:zzbig/e3'", why);
        CHECK(strstr(logbuf, "refused: its engine's pages alone pass"), "the refusal was not logged");
        api->set_param(g, "pad5_model", "toy/high");
        why[0] = 0; api->get_param(g, "model_refused", why, (int)sizeof why);
        CHECK(!why[0], "model_refused reads '%s' after a model was accepted", why);
        api->destroy_instance(g);
    }

    /* ---- 7. the broken ones cost nothing, and say why ------------------- */
    CHECK(strstr(logbuf, "engine plugin bad refused") && strstr(logbuf, "more than 8 knobs"),
          "the refused plugin's reason was not logged:\n%s", logbuf);
    CHECK(strstr(logbuf, "junk") && strstr(logbuf, "did not load"), "the unloadable file was not reported:\n%s", logbuf);
    CHECK(strstr(logbuf, "engine plugin toy: 1 engines, 2 models, 2 pages"), "the good plugin was not reported:\n%s", logbuf);
    /* The validator's list of DR32's own families is the built-in models' real
     * prefixes: a plugin may not take one as its id. */
    for (int i = 0; i < builtin; i++) {
        char fam[32];
        const char *slug = dr32_model_at(i)->slug, *cut = strchr(slug, '/');
        snprintf(fam, sizeof(fam), "%.*s", cut ? (int)(cut - slug) : 0, slug);
        dr32x_plugin probe = { DR32X_API_VERSION, sizeof(dr32x_plugin), fam, "Probe", 0, NULL, 0, NULL };
        const char *why = dr32_plugin_validate(&probe);
        CHECK(why && strstr(why, "DR32's own engine families"), "a plugin could call itself '%s' (%s)", fam, why ? why : "accepted");
    }

    /* ...and in a file of DR32's own, which does not depend on the host's log. */
    {
        static char pl[4096];
        FILE *pf = fopen("dist/tests/plug/dr32/plugins.log", "rb");
        size_t pn = pf ? fread(pl, 1, sizeof(pl) - 1, pf) : 0;
        if (pf) fclose(pf);
        pl[pn] = '\0';
        CHECK(strstr(pl, "engine plugin toy: 1 engines") && strstr(pl, "engine plugin bad refused"),
              "plugins.log does not carry the scan's report: '%s'", pl);
        remove("dist/tests/plug/dr32/plugins.log");     /* that folder is src/, by symlink */
    }

    /* ---- 8. the rules, on plugins that break them one at a time ---------- */
    {
        /* Each is a plugin a developer could plausibly write. The validator has
         * to REFUSE it with a reason — and, for the second, without reading
         * through the NULL it is refusing (it once crashed there, and on the
         * device that is the host going down instead of a line in the log). */
        static void *(*const mk)(int) = NULL;
        (void)mk;
        const dr32_engine_ops *ok = dr32_engine_get(DR32_ENG_COUNT);    /* the toy: real functions to borrow */
        static const float v2[2] = { 0, 0 };
        #define ENGINE1(slug_, params_, n_) { slug_, "E", n_, params_, ok->create, ok->destroy, ok->set, ok->note_on, NULL, ok->render }
        #define REFUSED(what, needle, eng) do { \
            dr32x_model md_ = { "m", "M", 0, v2, 0, 0 }; \
            dr32x_plugin pl_ = { DR32X_API_VERSION, sizeof(dr32x_plugin), "probe", "Probe", 1, eng, 1, &md_ }; \
            const char *w_ = dr32_plugin_validate(&pl_); \
            CHECK(w_ && strstr(w_, needle), "%s: %s", what, w_ ? w_ : "ACCEPTED"); } while (0)

        static const dr32x_param good[2]  = { { "a", "A", "A", 0, 1, 0, 1, NULL, "Tone", NULL }, { "b", "B", "B", 0, 1, 0, 1, NULL, "Tone", NULL } };
        static const dr32x_param nullp[2] = { { "a", "A", "A", 0, 1, 0, 1, NULL, "Tone", NULL }, { "b", "B", "B", 0, 1, 0, 1, NULL, NULL, NULL } };
        static const dr32x_param digit[2] = { { "a", "A", "A", 0, 2, 0, 1, NULL, "Tone", "808|909|707" }, { "b", "B", "B", 0, 1, 0, 1, NULL, "Tone", NULL } };
        static const dr32x_param cased[2] = { { "a", "A", "A", 0, 1, 0, 1, NULL, "Tone", NULL }, { "b", "B", "B", 0, 1, 0, 1, NULL, "tone", NULL } };
        dr32x_engine e_good  = ENGINE1("fine", good, 2),  e_under = ENGINE1("a_b", good, 2);
        dr32x_engine e_null  = ENGINE1("fine", nullp, 2), e_digit = ENGINE1("fine", digit, 2), e_cased = ENGINE1("fine", cased, 2);
        {
            dr32x_model md = { "m", "M", 0, v2, 0, 0 };
            dr32x_plugin pl = { DR32X_API_VERSION, sizeof(dr32x_plugin), "probe", "Probe", 1, &e_good, 1, &md };
            const char *w = dr32_plugin_validate(&pl);
            CHECK(!w, "a plugin that keeps the rules was refused: %s", w);
        }
        REFUSED("a later param with a NULL page", "malformed", &e_null);
        REFUSED("an enum option that starts with a digit", "digit", &e_digit);
        REFUSED("two pages that differ only in case", "case or punctuation", &e_cased);
        REFUSED("an engine slug with an underscore", "slug or name is malformed", &e_under);
        CHECK(dr32_plugin_validate(NULL) != NULL, "a NULL plugin was accepted");
        #undef ENGINE1
        #undef REFUSED
    }

    api->destroy_instance(inst);
    printf("%s  (%d checks, %d failures)\n", fails ? "FAILED" : "PASSED", checks, fails);
    return fails ? 1 : 0;
}
