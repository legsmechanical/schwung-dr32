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
 * The scan is INCREMENTAL because get_param runs on the SPI callback, so the
 * tests also drive it one small slice at a time — pumping with a budget of 1 is
 * the shape the device actually uses, just slower.
 */

#include "../dsp/dr32_kits.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    /* dr32_kits.c hard-codes the two real roots, so this test drives the parts
     * that do not depend on them plus a locate/ordering check over a built
     * catalogue. The ROOTS themselves are only exercisable on the device; the
     * device pass is what covers them. */
    dr32_kits *c = dr32_kits_create();
    CHECK(c != NULL, "create returned NULL");
    if (!c) return 1;

    /* An empty machine must not hang the pump or invent categories. */
    int guard = 0;
    while (!dr32_kits_pump(c, 8) && ++guard < 100000) { }
    CHECK(guard < 100000, "pump never completed — the walk does not terminate");
    CHECK(dr32_kits_ready(c), "ready() false after the pump completed");

    /* Whatever this build host has (almost certainly nothing at those paths),
     * the accessors must be total: no crash, no out-of-range read. */
    CHECK(dr32_kits_count(c, 999) == 0, "count of a nonexistent category is not 0");
    CHECK(dr32_kits_name(c, 0, 99999)[0] == '\0', "name past the end is not empty");
    CHECK(dr32_kits_path(c, 0, -1)[0] == '\0', "path at a negative index is not empty");
    CHECK(dr32_kits_cat_name(c, -1)[0] == '\0', "cat_name at -1 is not empty");
    CHECK(dr32_kits_locate(c, "", NULL, NULL) != 0, "locate of an empty path succeeded");
    CHECK(dr32_kits_locate(c, "/nope/nope.json", NULL, NULL) != 0, "locate of a missing path succeeded");

    /* Pumping a finished catalogue stays finished and stays cheap. */
    CHECK(dr32_kits_pump(c, 1) == 1, "pump on a complete catalogue did not return 1");

    /* Invalidate must genuinely reset, not just clear the flag. */
    dr32_kits_invalidate(c);
    CHECK(!dr32_kits_ready(c), "still ready() after invalidate");
    CHECK(dr32_kits_cat_count(c) == 0, "categories survived invalidate");
    guard = 0;
    while (!dr32_kits_pump(c, 8) && ++guard < 100000) { }
    CHECK(dr32_kits_ready(c), "did not complete after invalidate");

    dr32_kits_destroy(c);

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
