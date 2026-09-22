/* ⚠ THE TESTS BUILD -std=c11, AND THE TWO LIBCS DISAGREE ABOUT WHAT THAT HIDES.
 * glibc declares nothing outside the standard unless asked, so mkdtemp,
 * setenv, utimensat, struct timespec and M_PI all vanish. Darwin exposes
 * everything by DEFAULT and only starts restricting once you name a
 * standard — so _XOPEN_SOURCE fixes Linux and then hides mkdtemp on macOS,
 * which is the second failure caused by the fix for the first.
 *
 * _GNU_SOURCE is the one that asks for MORE on glibc and is simply not
 * consulted on Darwin, so it is additive on both. Verified by running the
 * suite on macOS, debian:bookworm and ubuntu:24.04 — not by reasoning about
 * headers. Keep it uniform across the suite: a new test file must not have
 * to pick, and picking wrong is invisible on whichever platform you use. */
#define _GNU_SOURCE

/*
 * THE KIT BROWSER, DRIVEN THE WAY THE HOST DRIVES IT.
 *
 * ⭐ tests/test_kits.c covers the CATALOGUE (dsp/dr32_kits.c) against its
 * accessors. Nothing covered the PATH: items page -> kit_cat -> kit_count ->
 * kit_index -> settle -> the kit is actually loaded. Every piece of that passed
 * its own test while "kits suddenly aren't loading" was reported from the
 * device, which is the definition of a gap between the units and the thing.
 *
 * ⚠ `DR32_KIT_ROOTS` is what makes this reachable off-device — the two real
 * roots are absolute /data paths. Every fixture below deliberately mixes drum
 * racks with same-extension NON-drum presets, because the whole reason the
 * catalogue exists is that it filters by CONTENT.
 */

#include "../dsp/host/plugin_api_v1.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
/* The bus-aware entry point. The host dlsym's it and switches to it PER FRAME
 * the moment a pad is assigned to a bus — so anything render_block does on a
 * clock has to happen here too. */
extern void move_plugin_render_split(void *instance, int16_t *const *voice_out,
                                     int n_voices, int16_t *main_out, int frames);

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void rm_rf(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char p[1024];
            snprintf(p, sizeof p, "%s/%s", path, e->d_name);
            rm_rf(p);
        }
        closedir(d);
        rmdir(path);
    } else unlink(path);
}

/* Copy the real 16-pad fixture, so what the browser loads is a preset the
 * loader genuinely parses rather than a stub it happens to accept. */
static int copy_file(const char *from, const char *to) {
    FILE *a = fopen(from, "rb"); if (!a) return 0;
    FILE *b = fopen(to, "wb");   if (!b) { fclose(a); return 0; }
    char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a); fclose(b);
    return 1;
}

/* A same-extension preset that is NOT a drum rack, with its marker-free window
 * where a real one's marker would be (bytes 452-581). */
/* A short 16-bit mono WAV, so a pad can hold a real sample. */
static void write_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const unsigned n = 441, data = n * 2;
    const unsigned char hdr[44] = {
        'R','I','F','F', (unsigned char)(36 + data), (unsigned char)((36 + data) >> 8), 0, 0,
        'W','A','V','E','f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        0x44,0xAC,0,0, 0x88,0x58,0x01,0, 2,0, 16,0,
        'd','a','t','a', (unsigned char)data, (unsigned char)(data >> 8), 0, 0 };
    fwrite(hdr, 1, sizeof hdr, f);
    for (unsigned i = 0; i < n; i++) { short v = (short)(i * 50); fwrite(&v, 2, 1, f); }
    fclose(f);
}

static void write_non_drum(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs("{\n  \"kind\": \"instrumentRack\",\n", f);
    long at = ftell(f);
    while (at < 700) { fputs("  \"pad\": 0,\n", f); at = ftell(f); }
    fputs("  \"oscillator\": { \"shape\": \"saw\" }\n}\n", f);
    fclose(f);
}

int main(void) {
    char tmpl[] = "/tmp/dr32browseXXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL, "mkdtemp failed");
    if (!dir) return 1;

    /* Sized so -Wformat-truncation can PROVE no path is cut short: each is
     * comfortably larger than the longest thing written into it. The real
     * values are ~20 characters — this is for the analyser, not the data. */
    char core[256], user[256], cat[512], roots[600], p[900];
    snprintf(core, sizeof core, "%s/core", dir);
    snprintf(user, sizeof user, "%s/user", dir);
    snprintf(cat,  sizeof cat,  "%s/Hybrid", core);
    mkdir(core, 0755); mkdir(user, 0755); mkdir(cat, 0755);

    /* Two real drum racks in one category, and a decoy that must not appear. */
    const char *SRC = "tests/fixtures/native-16pad.ablpreset";
    char kit_a[600], kit_b[600];
    snprintf(kit_a, sizeof kit_a, "%s/Alpha Kit.json", cat);
    snprintf(kit_b, sizeof kit_b, "%s/Beta Kit.json", cat);
    CHECK(copy_file(SRC, kit_a), "could not copy %s — run from the repo root", SRC);
    CHECK(copy_file(SRC, kit_b), "could not copy %s", SRC);
    snprintf(p, sizeof p, "%s/Not A Kit.json", cat);
    write_non_drum(p);

    snprintf(roots, sizeof roots, "%s:%s", core, user);
    setenv("DR32_KIT_ROOTS", roots, 1);

    plugin_api_v2_t *api = move_plugin_init_v2(NULL);
    void *inst = api ? api->create_instance("src", NULL) : NULL;
    CHECK(inst != NULL, "create_instance returned NULL");
    if (inst) {
        static int16_t sink[2 * 128];
        char v[8192];
        #define GET(k) (api->get_param(inst, (k), v, (int)sizeof v), v)

        /* 1. The catalogue is INCREMENTAL and pumped from these very reads, so
         *    the host's own shape — keep reading the page's keys — is what
         *    finishes it. A bounded loop, because a walk that never terminates
         *    is the failure this must not hang on. */
        int spins = 0;
        while (strcmp(GET("kit_cat_items"), "[]") == 0 && ++spins < 100000) { }
        CHECK(spins < 100000, "the category list never filled — the catalogue walk did not finish");
        CHECK(strstr(GET("kit_cat_items"), "\"label\":\"Hybrid\"") != NULL,
              "category list has no Hybrid: %.200s", v);

        /* 2. Choose the category. Two kits, and the decoy is NOT one of them.
         *    (Category 0 is Init — see 7.) */
        api->set_param(inst, "kit_cat", "1");
        CHECK(atoi(GET("kit_count")) == 2,
              "kit_count is %s, want 2 (the non-drum preset must be filtered out)", v);
        CHECK(!strcmp(GET("kit_name"), "Alpha Kit"), "kit_name at index 0 is '%s'", v);

        /* 3. A detent moves the CURSOR. ⚠ It must NOT have loaded yet — that is
         *    the deferred audition, and a test that checked only the end state
         *    would pass with the deferral removed and every detent loading. */
        api->set_param(inst, "kit_index", "1");
        CHECK(!strcmp(GET("kit_name"), "Beta Kit"), "cursor did not move: kit_name '%s'", v);
        CHECK(strstr(GET("kit"), "Beta Kit") == NULL,
              "the kit loaded on the DETENT — the audition is meant to wait for the cursor to settle");

        /* 4. ...and once the cursor settles, it loads. This is the whole path. */
        for (int i = 0; i < 90; i++) api->render_block(inst, sink, 128);
        CHECK(strstr(GET("kit"), "Beta Kit") != NULL,
              "after the settle the loaded kit is '%s' — the browser did not load anything", v);

        /* 5. The path is the EXACT file the cursor was on, not merely a path
         *    containing the name. ⚠ It cannot assert loaded SAMPLES: the
         *    fixture references WAVs that exist only on the device, so all 16
         *    legitimately fail here and every pad is unnamed. Asserting names
         *    made this fail for a reason that has nothing to do with the
         *    browser — the trap being that it looked like a real failure. */
        CHECK(!strcmp(GET("kit"), kit_b), "loaded kit is '%s', want '%s'", v, kit_b);

        /*
         * 6. 🔴 AND AGAIN ON THE SPLIT RENDER PATH — the one the host switches
         *    to, mid-stream and with no reload, as soon as a single pad is
         *    assigned to a bus.
         *
         *    This is the bug reported from the device as "I can load 1 kit
         *    after launch, but can't change it after that": the deferred load
         *    was serviced from render_block alone, so on the split path the
         *    browser moved the cursor, changed the name on screen, and loaded
         *    nothing. Forever. Nothing logged.
         *
         *    ⚠ Checks 1-5 above all passed throughout, because they drive
         *    render_block — which is exactly why this check exists separately
         *    rather than as one more block loop.
         */
        api->set_param(inst, "kit_index", "0");
        CHECK(!strcmp(GET("kit_name"), "Alpha Kit"), "cursor did not move back: '%s'", v);
        {
            static int16_t va[2 * 128], vb[2 * 128];
            int16_t *outs[2] = { va, vb };
            for (int i = 0; i < 90; i++)
                move_plugin_render_split(inst, outs, 2, sink, 128);
        }
        CHECK(!strcmp(GET("kit"), kit_a),
              "on the SPLIT render path the kit is still '%s' — the deferred load "
              "is not serviced there, so a pad on a bus stops the browser dead", v);

        /*
         * 7. INIT (Josh, 2026-09-22: "an 'Init' category that has one preset —
         *    'Init' basically puts the module in the state it's in when you
         *    first load it"). FIRST in the list, one kit, and loading it leaves
         *    the instance as create_instance left it: every pad empty at its
         *    defaults and its own note, master at unity.
         */
        GET("kit_cat_items");
        {
            const char *first = strstr(v, "\"label\":");
            CHECK(first && !strncmp(first, "\"label\":\"Init\"", 14),
                  "Init is not the first category: %.120s", v);
        }
        api->set_param(inst, "kit_cat", "0");
        CHECK(atoi(GET("kit_count")) == 1, "the Init category holds %s kits, want 1", v);
        CHECK(!strcmp(GET("kit_name"), "Init"), "the Init kit is called '%s'", v);

        /* Dirty the instance first, on everything Init must put back: a
         * synth pad, a sample pad's edits, Wide, the master, the note map. */
        api->set_param(inst, "pad2_model", "fm/kick");
        api->set_param(inst, "pad3_volume", "-12");
        api->set_param(inst, "pad3_wide", "40");
        api->set_param(inst, "pad4_note", "80");
        api->set_param(inst, "master", "0.5");
        api->set_param(inst, "kit_index", "0");
        for (int i = 0; i < 90; i++) api->render_block(inst, sink, 128);
        CHECK(!strcmp(GET("kit"), "dr32:init"), "after Init the kit is '%s'", v);
        CHECK(!strcmp(GET("pad2_model"), ""), "Init left pad 2's engine: '%s'", v);
        CHECK(!strcmp(GET("pad1_loaded"), "0"), "Init left pad 1 loaded");
        CHECK(!strcmp(GET("pad3_volume"), "0"), "Init left pad 3's volume at %s", v);
        CHECK(!strcmp(GET("pad3_wide"), "0"), "Init left pad 3's Wide at %s", v);
        CHECK(!strcmp(GET("pad4_note"), "39"), "Init left pad 4 on note %s, want 39", v);
        CHECK(atof(GET("master")) == 1.0, "Init left the master at %s", v);

        /* Init AGAIN, while Init is loaded, still empties a pad filled since:
         * the same-kit shortcut replays a baseline, and a baseline cannot
         * empty a pad (an empty pad writes no `sample` into it). */
        {
            char wav[600];
            snprintf(wav, sizeof wav, "%s/one.wav", dir);
            write_wav(wav);
            api->set_param(inst, "pad6_sample", wav);
            CHECK(!strcmp(GET("pad6_loaded"), "1"), "the test sample did not load on pad 6");
            api->set_param(inst, "kit", "dr32:init");
            CHECK(!strcmp(GET("pad6_loaded"), "0"), "Init over Init kept pad 6's sample");
        }

        /* A cancelled preview returns to Init: open the browser on it, audition
         * a real kit, back out. */
        api->set_param(inst, "kit_mark", "1");
        api->set_param(inst, "kit", kit_a);
        CHECK(!strcmp(GET("kit"), kit_a), "the preview did not load kit A: '%s'", v);
        api->set_param(inst, "kit_restore", "1");
        CHECK(!strcmp(GET("kit"), "dr32:init"), "a cancelled preview returned to '%s', not Init", v);
        CHECK(!strcmp(GET("pad1_loaded"), "0"), "a cancelled preview left kit A's pad 1");

        /* A fresh instance, compared key for key over every pad: Init IS the
         * state a new instance starts in, not an approximation of it. */
        {
            void *fresh = api->create_instance("src", NULL);
            static const char *const KEYS[] = {"loaded", "model", "note", "choke", "volume", "pan",
                "vel_vol", "transpose", "detune", "attack", "decay", "hold", "cutoff", "resonance",
                "filter_type", "punch", "send_a", "send_b", "wide", "wide_freq", NULL};
            char w[256];
            int same = 1;
            for (int pad = 1; pad <= 32 && same; pad++)
                for (int k = 0; KEYS[k]; k++) {
                    char key[64];
                    snprintf(key, sizeof key, "pad%d_%s", pad, KEYS[k]);
                    api->get_param(fresh, key, w, (int)sizeof w);
                    GET(key);
                    if (strcmp(v, w)) {
                        CHECK(0, "after Init %s is '%s', a new instance has '%s'", key, v, w);
                        same = 0;
                        break;
                    }
                }
            api->destroy_instance(fresh);
        }

        /* Saved and restored: a set that holds Init restores to Init, over
         * whatever kit the slot had. */
        {
            static char blob[65536];
            /* On a pad with something on it: the blob skips empty pads. */
            api->set_param(inst, "pad5_model", "fm/snare");
            api->set_param(inst, "pad5_volume", "-6");
            api->get_param(inst, "state", blob, (int)sizeof blob);
            void *other = api->create_instance("src", NULL);
            api->set_param(other, "kit", kit_a);
            api->set_param(other, "state", blob);
            char w[256];
            api->get_param(other, "kit", w, (int)sizeof w);
            CHECK(!strcmp(w, "dr32:init"), "a restored Init set reads kit '%s'", w);
            api->get_param(other, "pad1_loaded", w, (int)sizeof w);
            CHECK(!strcmp(w, "0"), "a restored Init set kept kit A's pad 1");
            api->get_param(other, "pad5_volume", w, (int)sizeof w);
            CHECK(!strcmp(w, "-6"), "a restored Init set lost its edit: pad5_volume %s", w);
            api->destroy_instance(other);
        }

        #undef GET
        api->destroy_instance(inst);
    }

    unsetenv("DR32_KIT_ROOTS");
    rm_rf(dir);
    printf("%s  (%d checks, %d failures)\n",
           failures ? "FAILED" : "kit browser PASSED", checks, failures);
    return failures ? 1 : 0;
}
