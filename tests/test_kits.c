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
 * The kit CATALOGUE (dsp/dr32_kits.c) — the two-level Kits browser's data.
 *
 * ⭐ THE PROPERTY THAT MATTERS IS THAT IT FILTERS BY CONTENT. The whole reason
 * this exists rather than a `filepath` cell is that the host's file browser
 * filters by EXTENSION, and the user's Track Presets tree holds every
 * instrument's presets: measured on the device, 365 files of which only 75 are
 * drum racks, so the old cell offered 290 entries that could not load. A test
 * that only counted files would pass with that defect intact, so every fixture
 * below deliberately mixes drum racks with same-extension non-drum presets.
 *
 * The scan runs on its OWN THREAD because get_param runs on the SPI callback
 * (a per-call slice measured 10 ms on the device). So the tests poll, as the
 * browser's reads do, and wait for the worker between polls.
 */

#include "../dsp/dr32_kits.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* A preset file whose drum marker sits where the real ones do — measured
 * between bytes 452 and 581 on the device, which is why is_drum_rack reads
 * exactly 1 KB. A fixture with the marker at byte 10 would pass against a
 * reader that only looked at the first 32 bytes. */
static void write_preset(const char *path, int drum) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  FAIL cannot write %s\n", path); failures++; return; }
    fputs("{\n  \"$schema\": \"http://tech.ableton.com/schema/song/1.8.2/devicePreset.json\",\n", f);
    fputs("  \"kind\": \"instrumentRack\",\n", f);   /* same for BOTH — kind does not distinguish */
    for (int i = 0; i < 12; i++) fprintf(f, "  \"Macro%d\": 0.0,\n", i);
    long at = ftell(f);
    while (at < 460) { fputs("  \"pad\": 0,\n", f); at = ftell(f); }
    fputs(drum ? "  \"drumZoneSettings\": { \"receivingNote\": 36 },\n"
               : "  \"oscillator\": { \"shape\": \"saw\" },\n", f);
    /* Tail, so a non-drum file is the ~11 KB the real ones are and a reader
     * that scanned whole files would be measurably slower. */
    for (int i = 0; i < 400; i++) fputs("  \"filler\": 0,\n", f);
    fputs("  \"end\": true\n}\n", f);
    fclose(f);
}

/** Poll until the worker's catalogue is adopted, as the browser's reads do,
 *  sleeping between polls. Bounded: a walk that never finishes must FAIL. */
static int wait_ready(dr32_kits *c) {
    struct timespec ms = { 0, 1000000 };
    for (int i = 0; i < 10000; i++) {
        if (dr32_kits_poll(c)) return 1;
        nanosleep(&ms, NULL);
    }
    return 0;
}

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
    } else {
        unlink(path);
    }
}

int main(void) {
    /* The real roots (nothing at those paths off the device): the parts that
     * do not depend on what the tree holds. */
    unsetenv("DR32_KIT_ROOTS");
    dr32_kits *c = dr32_kits_create();
    CHECK(c != NULL, "create returned NULL");
    if (!c) return 1;

    /* Before any poll: Init alone, and not ready — "not found" must not be
     * mistaken for "not in the catalogue" yet. */
    CHECK(!dr32_kits_ready(c), "ready() before the scan was even started");
    CHECK(dr32_kits_cat_count(c) == 1 && !strcmp(dr32_kits_cat_name(c, 0), "Init"),
          "before the scan the categories are not exactly Init");
    CHECK(!strcmp(dr32_kits_path(c, 0, 0), DR32_KIT_INIT_PATH), "Init's path is '%s'",
          dr32_kits_path(c, 0, 0));

    /* An empty machine must not hang the worker or invent categories. */
    CHECK(wait_ready(c), "the scan never completed — the walk does not terminate");
    CHECK(dr32_kits_cat_count(c) == 1, "an empty tree produced %d categories, want 1 (Init)",
          dr32_kits_cat_count(c));

    /* The accessors must be total: no crash, no out-of-range read. */
    CHECK(dr32_kits_count(c, 999) == 0, "count of a nonexistent category is not 0");
    CHECK(dr32_kits_name(c, 0, 99999)[0] == '\0', "name past the end is not empty");
    CHECK(dr32_kits_path(c, 0, -1)[0] == '\0', "path at a negative index is not empty");
    CHECK(dr32_kits_cat_name(c, -1)[0] == '\0', "cat_name at -1 is not empty");
    CHECK(dr32_kits_locate(c, "", NULL, NULL) != 0, "locate of an empty path succeeded");
    CHECK(dr32_kits_locate(c, "/nope/nope.json", NULL, NULL) != 0, "locate of a missing path succeeded");
    CHECK(dr32_kits_poll(c) == 1, "poll on a complete catalogue did not return 1");
    dr32_kits_destroy(c);

    /* ---- a real tree: the reads never wait on the scan ---- */
    {
        char tmpl[] = "/tmp/dr32kitsXXXXXX";
        char *dir = mkdtemp(tmpl);
        CHECK(dir != NULL, "mkdtemp failed");
        if (dir) {
            char core[1024], user[1024], sub[1040], p[1100], roots[2100];
            snprintf(core, sizeof core, "%s/core", dir);
            snprintf(user, sizeof user, "%s/user", dir);
            snprintf(sub, sizeof sub, "%s/Acoustic", core);
            mkdir(core, 0755); mkdir(user, 0755); mkdir(sub, 0755);
            /* Enough files that the walk takes real time behind the reads. */
            for (int i = 0; i < 300; i++) {
                snprintf(p, sizeof p, "%s/%s %03d.json", i % 2 ? sub : user,
                         i % 3 ? "Kit" : "Bass", i);
                write_preset(p, i % 3 != 0);
            }
            snprintf(roots, sizeof roots, "%s:%s", core, user);
            setenv("DR32_KIT_ROOTS", roots, 1);

            c = dr32_kits_create();
            /* The first poll STARTS the worker and returns at once: whatever
             * the tree holds, this read sees Init alone. The old shape did the
             * walking right here, on the caller's thread. */
            CHECK(dr32_kits_poll(c) == 0, "the first poll reported a finished catalogue");
            CHECK(dr32_kits_cat_count(c) == 1, "mid-scan the reads see %d categories, want 1 (Init)",
                  dr32_kits_cat_count(c));
            CHECK(wait_ready(c), "the scan of the fixture tree never completed");
            int want_core = 0, want_user = 0;
            for (int i = 0; i < 300; i++)
                if (i % 3 != 0) { if (i % 2) want_core++; else want_user++; }
            CHECK(dr32_kits_cat_count(c) == 3, "%d categories, want 3 (Init, Acoustic, My Kits)",
                  dr32_kits_cat_count(c));
            CHECK(!strcmp(dr32_kits_cat_name(c, 0), "Init"), "Init is not first");
            CHECK(dr32_kits_count(c, 1) == want_core, "Acoustic holds %d, want %d",
                  dr32_kits_count(c, 1), want_core);
            CHECK(dr32_kits_count(c, 2) == want_user, "My Kits holds %d, want %d",
                  dr32_kits_count(c, 2), want_user);
            /* Sorted by name within the category, and locate agrees. */
            int sorted = 1;
            for (int i = 1; i < dr32_kits_count(c, 1); i++)
                if (strcasecmp(dr32_kits_name(c, 1, i - 1), dr32_kits_name(c, 1, i)) > 0) sorted = 0;
            CHECK(sorted, "a category is not sorted by name");
            int lc = -1, li = -1;
            CHECK(dr32_kits_locate(c, dr32_kits_path(c, 2, 5), &lc, &li) == 0 && lc == 2 && li == 5,
                  "locate of My Kits #5 gave %d/%d", lc, li);
            dr32_kits_destroy(c);

            /* Destroyed MID-SCAN: the worker is told to stop and joined, and
             * nothing it built leaks into a freed instance. */
            c = dr32_kits_create();
            dr32_kits_poll(c);
            dr32_kits_destroy(c);
            CHECK(1, "destroy mid-scan returned");

            unsetenv("DR32_KIT_ROOTS");
            rm_rf(dir);
        }
    }

    /* ---- the content filter, which is the point of the whole file ---- */
    char tmpl[] = "/tmp/dr32kitsXXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL, "mkdtemp failed");
    if (dir) {
        char a[1024], b[1024];
        snprintf(a, sizeof a, "%s/kit.json", dir);
        snprintf(b, sizeof b, "%s/bass.json", dir);
        write_preset(a, 1);
        write_preset(b, 0);

        /* is_drum_rack is static, so exercise the same rule the way the
         * catalogue does: read 1 KB and look for the marker. If this fixture
         * ever stops matching what dr32_kits.c looks for, the numbers above
         * stop meaning anything. */
        for (int i = 0; i < 2; i++) {
            FILE *f = fopen(i ? b : a, "rb");
            char buf[1025];
            size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
            if (f) fclose(f);
            buf[n] = '\0';
            int found = strstr(buf, "drumZoneSettings") != NULL || strstr(buf, "drumRack") != NULL;
            CHECK(found == (i ? 0 : 1),
                  "%s fixture: marker %sfound in the first 1 KB",
                  i ? "non-drum" : "drum", found ? "" : "NOT ");
        }
        /* And the marker must be where the device's really are — past 452 —
         * or the 1 KB read is not being tested at all. */
        FILE *f = fopen(a, "rb");
        char big[4096];
        size_t n = f ? fread(big, 1, sizeof big - 1, f) : 0;
        if (f) fclose(f);
        big[n] = '\0';
        const char *m = strstr(big, "drumZoneSettings");
        CHECK(m != NULL && (m - big) > 400,
              "drum marker at offset %ld — the fixture must put it where real ones are (452-581)",
              m ? (long)(m - big) : -1L);
        rm_rf(dir);
    }

    printf("%s  (%d checks, %d failures)\n", failures ? "FAILED" : "kit catalogue PASSED",
           checks, failures);
    return failures ? 1 : 0;
}
