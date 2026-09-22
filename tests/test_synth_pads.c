/* See test_split.c for why every test file opens with _GNU_SOURCE. */
#define _GNU_SOURCE

/*
 * Synth pads IN THE KIT: a pad playing a SIMIAN or URCHIN model instead of a
 * sample, driven through the same surfaces the host drives — the param keys,
 * both render entry points, the state blob and the plugin API.
 *
 * The engines on their own are test_engine.c. This file is about what DR32
 * wraps around them and what the rest of DR32 has to keep doing right when a
 * pad is not a sample: focus gating (ui_engine), choke groups across kinds,
 * LINK, the kit browser dropping engines, persistence, names.
 */

#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_params.h"
#include "../dsp/dr32_preset.h"
#include "../dsp/dr32_state.h"
#include "../dsp/host/plugin_api_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
extern void move_plugin_render_split(void *instance, int16_t *const *voice_out,
                                     int n_voices, int16_t *main_out, int frames);

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define FR 128

static void set(dr32_kit *k, const char *key, const char *val) { dr32_apply_param(k, key, val); }
static const char *get(const dr32_kit *k, const char *key) {
    static char buf[4][512];
    static int slot;
    char *b = buf[slot++ & 3];
    b[0] = '\0';
    dr32_read_param(k, key, b, 512);
    return b;
}

static float render_peak(dr32_kit *k, int blocks) {
    static float out[2 * FR];
    float p = 0;
    for (int b = 0; b < blocks; b++) {
        dr32_kit_render(k, out, FR);
        for (int i = 0; i < 2 * FR; i++) if (fabsf(out[i]) > p) p = fabsf(out[i]);
    }
    return p;
}

static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }
static void make_wav(const char *path, float level) {
    FILE *f = fopen(path, "wb");
    int n = 44100;
    fwrite("RIFF", 1, 4, f); w32(f, 36 + n * 2); fwrite("WAVEfmt ", 1, 8, f);
    w32(f, 16); w16(f, 1); w16(f, 1); w32(f, 44100); w32(f, 88200); w16(f, 2); w16(f, 16);
    fwrite("data", 1, 4, f); w32(f, n * 2);
    for (int i = 0; i < n; i++) w16(f, (uint16_t)(int16_t)(level * 32767));
    fclose(f);
}

int main(void) {
    printf("synth pads\n");
    static dr32_kit k;
    const char *wav = "/tmp/dr32_synth_pads.wav";
    make_wav(wav, 0.5f);

    /* ---- choosing a model ---------------------------------------------- */
    {
        dr32_kit_init(&k);
        set(&k, "pad3_sample", wav);
        CHECK(!strcmp(get(&k, "pad3_loaded"), "1"), "sample loaded");
        set(&k, "pad3_model", "simian/kick");
        CHECK(k.pads[2].engine == DR32_ENG_SIMIAN, "engine %d", k.pads[2].engine);
        CHECK(!strcmp(get(&k, "pad3_model"), "simian/kick"), "model reads %s", get(&k, "pad3_model"));
        CHECK(!strcmp(get(&k, "pad3_sample"), "Kick"), "the engine cell reads the model name: %s", get(&k, "pad3_sample"));
        CHECK(!strcmp(get(&k, "pad3_loaded"), "0") && !k.pads[2].sample, "the sample is gone");
        CHECK(!strcmp(get(&k, "pad3_vel_vol"), "0"), "velocity is the engine's: vel_vol %s", get(&k, "pad3_vel_vol"));
        CHECK(!strcmp(get(&k, "pad3_sm_pitch"), "59.48"), "model values loaded: sm_pitch %s", get(&k, "pad3_sm_pitch"));

        /* ui_engine follows FOCUS, and refuses writes. */
        set(&k, "ui_current_pad", "3");
        CHECK(!strcmp(get(&k, "ui_engine"), "1"), "ui_engine on the kick %s", get(&k, "ui_engine"));
        set(&k, "ui_engine", "0");
        CHECK(!strcmp(get(&k, "ui_engine"), "1"), "ui_engine is read-only");
        set(&k, "ui_current_pad", "1");
        CHECK(!strcmp(get(&k, "ui_engine"), "0"), "ui_engine on an empty pad");
        set(&k, "pad1_model", "urchin/hat_closed");
        CHECK(!strcmp(get(&k, "ui_engine"), "4"), "ui_engine on an URCHIN cymbal %s", get(&k, "ui_engine"));
        CHECK(!strcmp(get(&k, "pad_uc_closed"), "100"), "the focused-pad alias reaches engine params");

        /* An unknown model changes nothing. */
        set(&k, "pad3_model", "simian/nope");
        CHECK(k.pads[2].engine == DR32_ENG_SIMIAN && !strcmp(get(&k, "pad3_model"), "simian/kick"),
              "unknown model ignored");

        /* Engine params: live, clamped, and only this engine's. */
        set(&k, "pad3_sm_pitch", "100");
        CHECK(!strcmp(get(&k, "pad3_sm_pitch"), "100"), "write/read %s", get(&k, "pad3_sm_pitch"));
        set(&k, "pad3_sm_pitch", "999999");
        CHECK(!strcmp(get(&k, "pad3_sm_pitch"), "4400"), "clamped to max: %s", get(&k, "pad3_sm_pitch"));
        CHECK(get(&k, "pad3_ud_pitch")[0] == '\0', "another engine's key is not this pad's");
        CHECK(get(&k, "pad2_sm_pitch")[0] == '\0', "a sample pad has no engine keys");
        dr32_kit_free(&k);
    }

    /* ---- it plays, on both render paths, and stops --------------------- */
    {
        dr32_kit_init(&k);
        set(&k, "pad1_model", "simian/kick");
        CHECK(render_peak(&k, 10) == 0.0f, "silent before a hit");
        dr32_kit_note_on(&k, 36, 110);
        CHECK(dr32_kit_active_voices(&k) == 1, "counts as a voice");
        float p = render_peak(&k, 40);
        CHECK(p > 0.05f, "a synth pad sounds (peak %g)", p);
        /* speaker off = silence, like a sample pad */
        set(&k, "pad1_speaker_on", "0");
        dr32_kit_note_on(&k, 36, 110);
        CHECK(render_peak(&k, 20) == 0.0f, "speaker off silences it");
        set(&k, "pad1_speaker_on", "1");
        /* note-off ends nothing: every engine here is percussive */
        dr32_kit_note_on(&k, 36, 110);
        dr32_kit_note_off(&k, 36);
        CHECK(render_peak(&k, 20) > 0.05f, "note-off does not cut a synth hit");
        /* the silence gate lets it go */
        render_peak(&k, 3000);
        CHECK(dr32_kit_active_voices(&k) == 0, "goes quiet on its own");

        /* Split path, nothing routed, must match render_block to rounding. */
        static dr32_kit a, b;
        dr32_kit_init(&a); dr32_kit_init(&b);
        const char *models[] = {"simian/snare", "urchin/kick", "urchin/cymbal"};
        char key[32];
        for (int i = 0; i < 3; i++) {
            snprintf(key, sizeof key, "pad%d_model", i + 1);
            set(&a, key, models[i]); set(&b, key, models[i]);
        }
        set(&a, "pad4_sample", wav); set(&b, "pad4_sample", wav);
        static float fo[2 * FR]; static int16_t mainb[2 * FR]; int16_t *vo[32];
        for (int i = 0; i < 32; i++) vo[i] = mainb;
        float maxdiff = 0, maxlev = 0;
        for (int blk = 0; blk < 300; blk++) {
            if (blk % 50 == 0) for (int n = 36; n < 40; n++) { dr32_kit_note_on(&a, n, 100); dr32_kit_note_on(&b, n, 100); }
            dr32_kit_render(&a, fo, FR);
            memset(mainb, 0, sizeof mainb);
            dr32_kit_render_split(&b, vo, 32, mainb, FR);
            for (int i = 0; i < 2 * FR; i++) {
                float x = fo[i] > 1 ? 1 : (fo[i] < -1 ? -1 : fo[i]);
                float d = fabsf(x * 32767.0f - mainb[i]);
                if (d > maxdiff) maxdiff = d;
                if (fabsf(x) > maxlev) maxlev = fabsf(x);
            }
        }
        CHECK(maxlev > 0.05f, "the mix is not silent (%g)", maxlev);
        CHECK(maxdiff <= 2.0f, "split path matches render_block (max diff %g lsb)", maxdiff);

        /* Routed out: the synth pad lands in ITS buffer and not in main.
         * Let the earlier hits ring out first so main hears only leftovers. */
        static int16_t bus[2 * FR], main2[2 * FR];
        for (int i = 0; i < 32; i++) vo[i] = mainb;
        for (int blk = 0; blk < 6000 && dr32_kit_active_voices(&b); blk++)
            dr32_kit_render_split(&b, vo, 32, mainb, FR);
        for (int i = 0; i < 32; i++) vo[i] = main2;
        vo[1] = bus;                                     /* pad2 = urchin/kick */
        dr32_kit_note_on(&b, 37, 120);
        int bus_peak = 0, main_peak = 0;
        for (int blk = 0; blk < 20; blk++) {
            memset(bus, 0, sizeof bus); memset(main2, 0, sizeof main2);
            dr32_kit_render_split(&b, vo, 32, main2, FR);
            for (int i = 0; i < 2 * FR; i++) {
                if (abs(bus[i]) > bus_peak) bus_peak = abs(bus[i]);
                if (abs(main2[i]) > main_peak) main_peak = abs(main2[i]);
            }
        }
        CHECK(bus_peak > 500, "routed synth pad reaches its bus (%d)", bus_peak);
        CHECK(main_peak < bus_peak / 4, "and mostly not main (%d vs %d)", main_peak, bus_peak);
        dr32_kit_free(&a); dr32_kit_free(&b); dr32_kit_free(&k);
    }

    /* ---- level and pan come from DR32's stage -------------------------- */
    {
        dr32_kit_init(&k);
        set(&k, "pad1_model", "urchin/low_tom");
        set(&k, "pad1_volume", "0"); set(&k, "pad1_pan", "0");
        dr32_kit_note_on(&k, 36, 100);
        float p0 = render_peak(&k, 30);
        /* Ring out fully: a retrigger over a ringing model is a different hit
         * (URCHIN tracks strike energy), which is not what this measures. */
        for (int i = 0; i < 6000 && dr32_kit_active_voices(&k); i++) render_peak(&k, 1);
        set(&k, "pad1_volume", "-12");
        dr32_kit_note_on(&k, 36, 100);
        float p12 = render_peak(&k, 30);
        CHECK(p0 > 0 && fabsf(20.0f * log10f(p12 / p0) + 12.0f) < 1.5f,
              "Volume -12 dB is -12 dB (%g -> %g)", p0, p12);

        /* hard left: right channel is the floored gain */
        for (int i = 0; i < 6000 && dr32_kit_active_voices(&k); i++) render_peak(&k, 1);
        set(&k, "pad1_volume", "0"); set(&k, "pad1_pan", "-50");
        dr32_kit_note_on(&k, 36, 100);
        static float out[2 * FR]; float l = 0, r = 0;
        for (int b = 0; b < 20; b++) {
            dr32_kit_render(&k, out, FR);
            for (int i = 0; i < FR; i++) { if (fabsf(out[2*i]) > l) l = fabsf(out[2*i]); if (fabsf(out[2*i+1]) > r) r = fabsf(out[2*i+1]); }
        }
        CHECK(l > 0.05f && r < l * 1e-3f, "pan hard left (l %g r %g)", l, r);
        dr32_kit_free(&k);
    }

    /* ---- URCHIN's Media stage: neutral is a true bypass; each knob acts --- */
    {
        static dr32_kit a, b;
        static float oa[2 * FR], ob[2 * FR];
        #define HIT_DIFF(setup) ({ \
            dr32_kit_init(&a); dr32_kit_init(&b); \
            set(&a, "pad1_model", "urchin/kick"); set(&b, "pad1_model", "urchin/kick"); \
            setup; \
            dr32_kit_note_on(&a, 36, 110); dr32_kit_note_on(&b, 36, 110); \
            double d = 0; \
            for (int blk = 0; blk < 40; blk++) { \
                dr32_kit_render(&a, oa, FR); dr32_kit_render(&b, ob, FR); \
                for (int i = 0; i < 2 * FR; i++) d += fabs(oa[i] - ob[i]); \
            } \
            dr32_kit_free(&a); dr32_kit_free(&b); d; })
        CHECK(HIT_DIFF((void)0) == 0.0, "two neutral URCHIN kicks render identically");
        /* Writing the neutral values explicitly still bypasses. */
        CHECK(HIT_DIFF((set(&b, "pad1_ud_noise", "0"), set(&b, "pad1_ud_bits", "16"),
                        set(&b, "pad1_ud_rate", "44100"), set(&b, "pad1_ud_sat", "0"))) == 0.0,
              "neutral Media is a bypass");
        CHECK(HIT_DIFF(set(&b, "pad1_ud_bits", "4")) > 1.0, "Bits crushes");
        CHECK(HIT_DIFF(set(&b, "pad1_ud_rate", "11025")) > 1.0, "Rate downsamples");
        CHECK(HIT_DIFF(set(&b, "pad1_ud_sat", "100")) > 1.0, "Sat saturates");
        CHECK(HIT_DIFF(set(&b, "pad1_ud_noise", "100")) > 1.0, "Noise adds the record");
        CHECK(HIT_DIFF((set(&a, "pad1_ud_noise", "100"), set(&b, "pad1_ud_noise", "100"),
                        set(&b, "pad1_ud_noise_type", "Tape"))) > 1.0, "Vinyl and Tape differ");

        /* The noise type is an enum: it reads and writes by NAME. */
        dr32_kit_init(&k);
        set(&k, "pad2_model", "urchin/hat_open");
        CHECK(!strcmp(get(&k, "pad2_uc_noise_type"), "Vinyl"), "reads Vinyl (%s)", get(&k, "pad2_uc_noise_type"));
        set(&k, "pad2_uc_noise_type", "Tape");
        CHECK(!strcmp(get(&k, "pad2_uc_noise_type"), "Tape"), "Tape by name (%s)", get(&k, "pad2_uc_noise_type"));
        set(&k, "pad2_uc_noise_type", "0");
        CHECK(!strcmp(get(&k, "pad2_uc_noise_type"), "Vinyl"), "and by index");
        set(&k, "pad2_uc_noise_type", "Tape");
        set(&k, "pad2_uc_noise_type", "Cassette");
        CHECK(!strcmp(get(&k, "pad2_uc_noise_type"), "Tape"), "an unknown name changes nothing");
        dr32_kit_free(&k);
    }

    /* ---- panic mutes a synth pad at once, and the next hit is whole ----- */
    {
        dr32_kit_init(&k);
        set(&k, "pad1_model", "urchin/cymbal");
        dr32_kit_note_on(&k, 36, 120);
        CHECK(render_peak(&k, 10) > 0.02f, "cymbal sounds");
        set(&k, "panic", "1");
        CHECK(render_peak(&k, 5) == 0.0f, "panic silences it at once");
        dr32_kit_note_on(&k, 36, 120);
        CHECK(render_peak(&k, 10) > 0.02f, "and it plays again after");
        dr32_kit_free(&k);
    }

    /* ---- choke groups, across kinds ------------------------------------ */
    {
        dr32_kit_init(&k);
        set(&k, "pad1_model", "urchin/hat_open");
        set(&k, "pad2_model", "simian/hat_closed");
        set(&k, "pad3_sample", wav);
        set(&k, "pad1_choke", "1"); set(&k, "pad2_choke", "1"); set(&k, "pad3_choke", "1");
        set(&k, "pad1_volume", "0");
        dr32_kit_note_on(&k, 36, 120);
        render_peak(&k, 10);
        /* A sample pad chokes a synth pad. Render pad1 alone by muting pad3. */
        set(&k, "pad3_speaker_on", "0");
        dr32_kit_note_on(&k, 38, 120);
        render_peak(&k, 3);                         /* 3 ms ramp */
        float after = render_peak(&k, 10);
        CHECK(after < 1e-3f, "a sample pad chokes a synth pad (%g)", after);

        /* A synth pad chokes a sample pad. */
        set(&k, "pad3_speaker_on", "1");
        set(&k, "pad2_speaker_on", "0");
        dr32_kit_note_on(&k, 38, 120);
        render_peak(&k, 5);
        dr32_kit_note_on(&k, 37, 120);
        render_peak(&k, 3);
        after = render_peak(&k, 10);
        CHECK(after < 1e-3f, "a synth pad chokes a sample pad (%g)", after);

        /* A choked synth pad hits again at full level. */
        set(&k, "pad2_speaker_on", "1");
        dr32_kit_note_on(&k, 36, 120);
        CHECK(render_peak(&k, 10) > 0.02f, "a choked synth pad plays again");
        dr32_kit_free(&k);
    }

    /* ---- back to a sample, and a kit load drops engines ------------------ */
    {
        dr32_kit_init(&k);
        set(&k, "pad1_model", "simian/clap");
        set(&k, "pad1_choke", "2");
        set(&k, "pad1_send_a", "-10");
        /* The name coming back through the cell is NOT a path. */
        set(&k, "pad1_sample", "Clap");
        CHECK(k.pads[0].engine == DR32_ENG_SIMIAN, "writing the model's own name back is ignored");
        set(&k, "pad1_sample", wav);
        CHECK(k.pads[0].engine == 0 && k.pads[0].sample, "loading a sample makes it a sample pad");
        CHECK(!strcmp(get(&k, "pad1_vel_vol"), "0.35"), "with a sample pad's defaults (vel_vol %s)", get(&k, "pad1_vel_vol"));
        CHECK(!strcmp(get(&k, "pad1_choke"), "2") && !strcmp(get(&k, "pad1_send_a"), "-10"),
              "keeping its choke group and sends");
        CHECK(!strcmp(get(&k, "pad1_model"), ""), "model reads empty on a sample pad");

        set(&k, "pad5_model", "urchin/snare");
        dr32_preset_report rep;
        dr32_preset_load(&k, "tests/fixtures/native-16pad.ablpreset", &rep);
        int any = 0;
        for (int i = 0; i < DR32_PADS; i++) any |= k.pads[i].engine;
        CHECK(!any, "a Move kit load leaves no synth pads");
        set(&k, "pad6_model", "simian/kick");
        set(&k, "clear", "1");
        CHECK(k.pads[5].engine == 0, "clear drops engines");
        dr32_kit_free(&k);
    }

    /* ---- LINK spreads an engine param to the same engine only ---------- */
    {
        dr32_kit_init(&k);
        set(&k, "pad1_model", "simian/kick");
        set(&k, "pad2_model", "simian/snare");
        set(&k, "pad3_model", "urchin/kick");
        set(&k, "pad4_sample", wav);
        set(&k, "link", "All");
        set(&k, "pad1_sm_decay", "777");
        CHECK(!strcmp(get(&k, "pad2_sm_decay"), "777"), "same engine follows (%s)", get(&k, "pad2_sm_decay"));
        CHECK(get(&k, "pad3_sm_decay")[0] == '\0' && !strcmp(get(&k, "pad3_ud_decay"), "300"),
              "another engine is untouched (%s)", get(&k, "pad3_ud_decay"));
        CHECK(k.pads[3].engine == 0 && k.pads[3].sample, "a sample pad is untouched");
        /* model is never linked: picking one drum does not make 32 of them */
        set(&k, "link", "All");
        set(&k, "pad1_model", "simian/clap");
        CHECK(!strcmp(get(&k, "pad2_model"), "simian/snare"), "model does not fan out");
        CHECK(!k.link_all, "and it ends the mode");
        dr32_kit_free(&k);
    }

    /* ---- state round trip -------------------------------------------- */
    {
        static dr32_kit a, b;
        dr32_kit_init(&a); dr32_kit_init(&b);
        set(&a, "pad1_model", "simian/kick");
        set(&a, "pad1_sm_decay", "444");
        set(&a, "pad1_transpose", "-3");
        set(&a, "pad1_volume", "-4.5");
        set(&a, "pad2_model", "urchin/rimshot");
        set(&a, "pad2_us_rim", "60");
        set(&a, "pad3_sample", wav);
        set(&a, "pad3_cutoff", "900");
        static char blob[65536];
        int n = dr32_state_write(&a, "", blob, sizeof blob, NULL);
        CHECK(n > 0, "state written");
        CHECK(!strstr(blob, "\"pad1_sample\""), "a synth pad persists no sample path");
        CHECK(strstr(blob, "\"pad1_model\":\"simian/kick\"") != NULL, "model persisted");
        CHECK(strstr(blob, "\"pad2_us_rim\":\"60\"") != NULL, "engine params persisted");
        dr32_state_read(&b, blob, NULL, NULL);
        const char *keys[] = {"pad1_model", "pad1_sm_decay", "pad1_transpose", "pad1_volume",
                              "pad1_vel_vol", "pad2_model", "pad2_us_rim", "pad2_us_pitch",
                              "pad3_sample", "pad3_cutoff", "pad2_pan", NULL};
        for (int i = 0; keys[i]; i++)
            CHECK(!strcmp(get(&a, keys[i]), get(&b, keys[i])), "%s: %s vs %s",
                  keys[i], get(&a, keys[i]), get(&b, keys[i]));
        /* and they sound the same */
        static float oa[2 * FR], ob[2 * FR];
        int same = 1;
        dr32_kit_note_on(&a, 36, 99); dr32_kit_note_on(&b, 36, 99);
        dr32_kit_note_on(&a, 37, 99); dr32_kit_note_on(&b, 37, 99);
        for (int blk = 0; blk < 100; blk++) {
            dr32_kit_render(&a, oa, FR); dr32_kit_render(&b, ob, FR);
            if (memcmp(oa, ob, sizeof oa)) same = 0;
        }
        CHECK(same, "a restored kit renders identically");
        dr32_kit_free(&a); dr32_kit_free(&b);
    }

    /* ---- the kit ports: one engine per lane, one page set per machine ---- */
    {
        static dr32_kit a, b;
        dr32_engines_set_module_dir("src");     /* 9W9's cymbal WAVs, src/samples/9w9/ */
        dr32_kit_init(&a); dr32_kit_init(&b);
        set(&a, "pad1_model", "9w9/kick");
        set(&a, "pad2_model", "6w6/snare");
        set(&a, "pad3_model", "cw78/guiro");
        set(&a, "pad4_model", "8w8/cowbell");
        set(&a, "pad5_model", "9w9/ride");
        CHECK(a.pads[0].engine == DR32_ENG_9W9_BASE && a.pads[2].engine == DR32_ENG_CW78_BASE + 12,
              "lane engines %d %d", a.pads[0].engine, a.pads[2].engine);
        CHECK(!strcmp(get(&a, "pad1_sample"), "Bass Drum"), "the engine cell names the lane: %s", get(&a, "pad1_sample"));

        /* THE TWO GATES follow focus: ui_family picks the machine's page set,
         * ui_engine the lane's own knob inside it. Both refuse writes. */
        set(&a, "ui_current_pad", "1");
        CHECK(!strcmp(get(&a, "ui_family"), "3") && !strcmp(get(&a, "ui_engine"), "5"),
              "9W9 kick: family %s engine %s", get(&a, "ui_family"), get(&a, "ui_engine"));
        set(&a, "ui_family", "0");
        CHECK(!strcmp(get(&a, "ui_family"), "3"), "ui_family is read-only");
        set(&a, "ui_current_pad", "2");
        CHECK(!strcmp(get(&a, "ui_family"), "4"), "6W6 snare: family %s", get(&a, "ui_family"));
        set(&a, "ui_current_pad", "3");
        CHECK(!strcmp(get(&a, "ui_family"), "6"), "CW-78 guiro: family %s", get(&a, "ui_family"));
        set(&a, "ui_current_pad", "9");
        CHECK(!strcmp(get(&a, "ui_family"), "0"), "an empty pad is family 0 (%s)", get(&a, "ui_family"));
        set(&a, "pad9_model", "simian/kick");
        CHECK(!strcmp(get(&a, "ui_family"), "1"), "SIMIAN is family 1 (%s)", get(&a, "ui_family"));

        /* Params: an enum by name, a lane's extra, and a SHARED key that lands
         * only on pads whose lane has it. */
        set(&a, "pad1_n9_dist", "SAT");
        CHECK(!strcmp(get(&a, "pad1_n9_dist"), "SAT"), "enum by name: %s", get(&a, "pad1_n9_dist"));
        set(&a, "pad1_n9_bd_attack", "90");
        CHECK(!strcmp(get(&a, "pad1_n9_bd_attack"), "90"), "a lane's own knob: %s", get(&a, "pad1_n9_bd_attack"));
        set(&a, "pad5_n9_bd_attack", "90");
        CHECK(get(&a, "pad5_n9_bd_attack")[0] == 0, "the kick's knob does not exist on the ride (%s)",
              get(&a, "pad5_n9_bd_attack"));
        set(&a, "pad3_c7_gu_rate", "10");
        set(&a, "pad3_c7_decay", "70");
        set(&a, "pad2_s6_sd_snappy", "100");
        set(&a, "pad4_e8_tune", "80");
        set(&a, "pad4_transpose", "5");
        CHECK(!strcmp(get(&a, "pad4_e8_tune"), "80"), "shared key: %s", get(&a, "pad4_e8_tune"));
        CHECK(get(&a, "pad2_e8_tune")[0] == 0, "an 8W8 key is not a 6W6 pad's");

        /* State: the pads come back as the same lanes with the same knobs,
         * and play the same. */
        static char blob[65536];
        int n = dr32_state_write(&a, "", blob, sizeof blob, NULL);
        CHECK(n > 0 && strstr(blob, "\"pad3_model\":\"cw78/guiro\"") && strstr(blob, "\"pad3_c7_gu_rate\":\"10\""),
              "kit-port pads persisted");
        dr32_state_read(&b, blob, NULL, NULL);
        const char *keys[] = {"pad1_model", "pad1_n9_dist", "pad1_n9_bd_attack", "pad2_s6_sd_snappy",
                              "pad3_c7_gu_rate", "pad3_c7_decay", "pad4_e8_tune", "pad4_transpose",
                              "pad5_model", NULL};
        for (int i = 0; keys[i]; i++)
            CHECK(!strcmp(get(&a, keys[i]), get(&b, keys[i])), "%s: %s vs %s", keys[i], get(&a, keys[i]), get(&b, keys[i]));
        static float oa[2 * FR], ob[2 * FR];
        int same = 1;
        float pk = 0;
        for (int note = 36; note <= 40; note++) { dr32_kit_note_on(&a, note, 110); dr32_kit_note_on(&b, note, 110); }
        for (int blk = 0; blk < 200; blk++) {
            dr32_kit_render(&a, oa, FR); dr32_kit_render(&b, ob, FR);
            if (memcmp(oa, ob, sizeof oa)) same = 0;
            for (int i = 0; i < 2 * FR; i++) if (fabsf(oa[i]) > pk) pk = fabsf(oa[i]);
        }
        CHECK(same, "a restored kit-port kit renders identically");
        CHECK(pk > 0.05f, "and it sounds (peak %g)", (double)pk);

        /* The ride is a SAMPLE lane: it plays only if the WAVs were found. */
        static dr32_kit c;
        dr32_kit_init(&c);
        set(&c, "pad1_model", "9w9/ride");
        dr32_kit_note_on(&c, 36, 120);
        pk = 0;
        for (int blk = 0; blk < 100; blk++) {
            dr32_kit_render(&c, oa, FR);
            for (int i = 0; i < 2 * FR; i++) if (fabsf(oa[i]) > pk) pk = fabsf(oa[i]);
        }
        CHECK(pk > 0.02f, "9W9 ride plays its PCM (peak %g)", (double)pk);
        dr32_kit_free(&a); dr32_kit_free(&b); dr32_kit_free(&c);
    }

    /* ---- the plugin: names, the is_loading pulse, split_voices ---------- */
    {
        plugin_api_v2_t *api = move_plugin_init_v2(NULL);
        dr32_engines_set_module_dir(NULL);
        void *inst = api ? api->create_instance("src", NULL) : NULL;
        CHECK(inst != NULL, "create_instance");
        /* where 9W9's cymbal WAVs are found comes from the instance */
        CHECK(dr32_engines_module_dir() && !strcmp(dr32_engines_module_dir(), "src"),
              "create_instance hands the engines its module dir (%s)",
              dr32_engines_module_dir() ? dr32_engines_module_dir() : "NULL");
        if (inst) {
            static char buf[65536];
            api->set_param(inst, "pad7_model", "urchin/hat_open");
            buf[0] = 0; api->get_param(inst, "is_loading", buf, sizeof buf);
            CHECK(!strcmp(buf, "1"), "a model change pulses is_loading (%s)", buf);
            api->get_param(inst, "split_voices", buf, sizeof buf);
            CHECK(strstr(buf, "{\"id\":\"pad7\",\"label\":\"Open Hat\"}") != NULL, "split_voices names the model");
            api->get_param(inst, "ui_hierarchy", buf, sizeof buf);
            CHECK(strstr(buf, "\"Open Hat\"") != NULL, "child_names name the model");
            /* state through the plugin keeps it */
            static char st[65536];
            api->get_param(inst, "state", st, sizeof st);
            void *inst2 = api->create_instance("src", NULL);
            api->set_param(inst2, "state", st);
            buf[0] = 0; api->get_param(inst2, "pad7_model", buf, sizeof buf);
            CHECK(!strcmp(buf, "urchin/hat_open"), "plugin state round trip (%s)", buf);
            /* the split entry point renders a synth pad */
            static int16_t mainb[2 * FR]; int16_t *vo[32];
            for (int i = 0; i < 32; i++) vo[i] = mainb;
            uint8_t on[3] = {0x90, 42, 120};
            api->on_midi(inst2, on, 3, 0);
            int pk = 0;
            for (int blk = 0; blk < 10; blk++) {
                memset(mainb, 0, sizeof mainb);
                move_plugin_render_split(inst2, vo, 32, mainb, FR);
                for (int i = 0; i < 2 * FR; i++) if (abs(mainb[i]) > pk) pk = abs(mainb[i]);
            }
            CHECK(pk > 100, "plugin split render plays the synth pad (%d)", pk);
            api->destroy_instance(inst2);
            api->destroy_instance(inst);
        }
    }

    remove(wav);
    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
