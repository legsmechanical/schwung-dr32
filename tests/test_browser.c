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

        /* 2. Choose the category. Two kits, and the decoy is NOT one of them. */
        api->set_param(inst, "kit_cat", "0");
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

        #undef GET
        api->destroy_instance(inst);
    }

    unsetenv("DR32_KIT_ROOTS");
    rm_rf(dir);
    printf("%s  (%d checks, %d failures)\n",
           failures ? "FAILED" : "kit browser PASSED", checks, failures);
    return failures ? 1 : 0;
}
