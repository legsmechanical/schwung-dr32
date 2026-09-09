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

// State round-trip: what Schwung stores in a set's slot_N.json.
//
// DR32 answered individual params but never get_param("state"), so the host had
// nothing to persist — every slot file held `"config": {}` and NOTHING about a
// DR32 slot survived a reboot. Found 2026-08-05; the host's autosave had been
// logging `chain=false` for exactly this reason.
//
// These tests are about the property that failed in the field: an edit made,
// written out, and read back must come back as the same value. A test that only
// checked the blob "looks like JSON" would have passed against the broken build.

#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_params.h"
#include "../dsp/dr32_state.h"
#include "../dsp/host/plugin_api_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Exported by dsp/dr32.c; the plugin headers describe the struct, not the
 * module's own entry point. */
extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ⚠ `system` is `warn_unused_result` on a hardened glibc (Ubuntu defaults
 * -D_FORTIFY_SOURCE at -O2; Debian does not), so ignoring it is a -Werror
 * failure on some builders and silence on others. Checking it is the right
 * thing anyway: a fixture directory that failed to appear makes every check
 * below it pass or fail for a reason that has nothing to do with the code. */
static void sh(const char *cmd) {
    int rc = system(cmd);
    if (rc != 0) { failures++; printf("  FAIL setup command failed (%d): %s\n", rc, cmd); }
}

static char blob[65536];

/* Read one param back as text, for comparing before/after. */
static void rd(const dr32_kit *k, const char *key, char *out, int cap) {
    out[0] = '\0';
    dr32_read_param(k, key, out, cap);
}

/* The pads a real kit would have loaded. dr32_state_write deliberately skips
 * pads with no sample AND no path — a silent pad carries no sound the user
 * could have shaped — so a test kit must look occupied or nothing is emitted. */
static void occupy(dr32_kit *k, int pad, const char *path) {
    snprintf(k->pads[pad].path, sizeof(k->pads[pad].path), "%s", path);
}

/* One second of silence, 16-bit mono 44.1 kHz — enough for a pad to count as
 * loaded, which is all the hierarchy test needs. */
static void put16(FILE *f, unsigned v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xffff); put16(f, v >> 16); }
static void make_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const unsigned sr = 44100, data = sr * 2;
    fputs("RIFF", f); put32(f, 4 + 24 + 8 + data); fputs("WAVE", f);
    fputs("fmt ", f); put32(f, 16); put16(f, 1); put16(f, 1); put32(f, sr);
    put32(f, sr * 2); put16(f, 2); put16(f, 16);
    fputs("data", f); put32(f, data);
    for (unsigned i = 0; i < sr; i++) put16(f, 0);
    fclose(f);
}

int main(void) {
    printf("test_state\n");

    /* ---- 1. A written blob restores every edited value ------------------ */
    {
        dr32_kit a; dr32_kit_init(&a);
        occupy(&a, 0, "/data/UserData/Samples/kick.wav");
        occupy(&a, 3, "/data/UserData/Samples/snare.wav");

        /* Spread across the kinds of field that exist: a bipolar int, a float,
         * an enum-ish, a global, and a send. */
        dr32_apply_param(&a, "pad1_transpose", "-5");
        dr32_apply_param(&a, "pad1_decay",     "0.25");
        dr32_apply_param(&a, "pad1_choke",     "3");
        dr32_apply_param(&a, "pad4_pan",       "-20");
        dr32_apply_param(&a, "master",         "0.5");

        int n = dr32_state_write(&a, "/data/UserData/Kits/MyKit.ablpreset",
                                 blob, (int)sizeof(blob), NULL);
        CHECK(n > 0, "state_write returned %d (expected > 0)", n);
        CHECK(strstr(blob, "\"kit\":\"/data/UserData/Kits/MyKit.ablpreset\"") != NULL,
              "blob does not carry the kit path");
        CHECK(strstr(blob, "pad1_transpose") != NULL, "blob omits an edited pad field");

        /* A fresh kit, restored from the blob. No load_kit callback: this test
         * is about the params, and a real preset load needs files on disk. */
        dr32_kit b; dr32_kit_init(&b);
        occupy(&b, 0, "x"); occupy(&b, 3, "x");
        CHECK(dr32_state_read(&b, blob, NULL, NULL) == 1, "state_read rejected its own output");

        char va[64], vb[64];
        const char *keys[] = { "pad1_transpose", "pad1_decay", "pad1_choke",
                               "pad4_pan", "master", NULL };
        for (int i = 0; keys[i]; i++) {
            rd(&a, keys[i], va, sizeof(va));
            rd(&b, keys[i], vb, sizeof(vb));
            CHECK(strcmp(va, vb) == 0, "%s did not round-trip: wrote '%s', read back '%s'",
                  keys[i], va, vb);
        }
    }

    /* ---- 2. The specific field that started all this -------------------- */
    {
        dr32_kit a; dr32_kit_init(&a);
        occupy(&a, 1, "/s.wav");
        dr32_apply_param(&a, "pad2_transpose", "-5");
        CHECK(dr32_state_write(&a, "", blob, (int)sizeof(blob), NULL) > 0, "write failed");

        dr32_kit b; dr32_kit_init(&b);
        occupy(&b, 1, "/s.wav");
        dr32_state_read(&b, blob, NULL, NULL);
        char v[64]; rd(&b, "pad2_transpose", v, sizeof(v));
        CHECK(atof(v) == -5.0, "transpose came back as '%s', expected -5", v);
    }

    /* ---- 3. Paths with characters that would break naive JSON ---------- */
    {
        dr32_kit a; dr32_kit_init(&a);
        occupy(&a, 0, "/s.wav");
        /* A quote in a kit path made the whole blob unparseable before escaping
         * existed — and the slot then failed to restore in SILENCE. */
        CHECK(dr32_state_write(&a, "/kits/He said \"hi\"/k.ablpreset",
                               blob, (int)sizeof(blob), NULL) > 0, "write failed on quoted path");
        dr32_kit b; dr32_kit_init(&b);
        CHECK(dr32_state_read(&b, blob, NULL, NULL) == 1,
              "a quote in the kit path made the blob unparseable");
    }

    /* ---- 4. Robustness the field will demand --------------------------- */
    {
        dr32_kit b; dr32_kit_init(&b);
        CHECK(dr32_state_read(&b, "not json at all", NULL, NULL) == 0,
              "malformed blob was accepted");
        CHECK(dr32_state_read(&b, "", NULL, NULL) == 0, "empty blob was accepted");
        /* Unknown keys must be ignored, not fatal: a blob from a newer build
         * has to load on an older one rather than failing whole. */
        CHECK(dr32_state_read(&b, "{\"v\":1,\"params\":{\"pad1_nosuchfield\":\"1\"}}",
                              NULL, NULL) == 1, "an unknown key made the whole restore fail");
        /* A truncating buffer must yield nothing rather than half a blob. */
        char tiny[80];
        dr32_kit a; dr32_kit_init(&a);
        for (int p = 0; p < DR32_PADS; p++) occupy(&a, p, "/some/long/sample/path.wav");
        CHECK(dr32_state_write(&a, "/kit.ablpreset", tiny, (int)sizeof(tiny), NULL) == 0,
              "a truncated blob was returned instead of failing");
    }

    /* ---- 4b. Baseline delta: the blob carries ONLY the user's edits ----- */
    {
        /* Why size matters: the host chain caps the state it reads back, and a
         * full dump PRETTY-PRINTED into the set file crossed a host's cap in
         * the field (2026-08-06) — the kit then restored at defaults. */
        dr32_kit a; dr32_kit_init(&a);
        occupy(&a, 0, "/s0.wav"); occupy(&a, 1, "/s1.wav");

        /* Baseline = the kit as loaded, before any edit. */
        static char base[65536];
        CHECK(dr32_state_write(&a, "/kit.ablpreset", base, (int)sizeof(base), NULL) > 0,
              "baseline write failed");

        /* One edit; a delta blob must carry it and no unedited siblings. */
        dr32_apply_param(&a, "pad1_transpose", "-5");
        int n = dr32_state_write(&a, "/kit.ablpreset", blob, (int)sizeof(blob), base);
        CHECK(n > 0, "delta write failed");
        CHECK(strstr(blob, "pad1_transpose") != NULL, "delta blob omits the edit");
        CHECK(strstr(blob, "pad2_") == NULL,
              "delta blob carries unedited pad1 fields: %.200s", blob);
        CHECK(n < 512, "delta blob is %d bytes — not a delta", n);

        /* An edit reverted to its baseline value drops back out. */
        char orig[64]; rd(&a, "pad1_transpose", orig, sizeof(orig));
        (void)orig;
        dr32_kit c; dr32_kit_init(&c);
        occupy(&c, 0, "/s0.wav");
        char base_v[64]; rd(&c, "pad1_transpose", base_v, sizeof(base_v));
        dr32_apply_param(&a, "pad1_transpose", base_v);
        n = dr32_state_write(&a, "/kit.ablpreset", blob, (int)sizeof(blob), base);
        CHECK(n > 0 && strstr(blob, "pad1_transpose") == NULL,
              "a reverted edit still appears in the delta blob");

        /* A baseline for a DIFFERENT kit path must be IGNORED — comparing
         * against another kit's values would silently drop real edits. */
        dr32_apply_param(&a, "pad1_transpose", "-5");
        n = dr32_state_write(&a, "/OTHER.ablpreset", blob, (int)sizeof(blob), base);
        CHECK(n > 0 && strstr(blob, "pad2_") != NULL,
              "a mismatched-kit baseline was honoured (blob still a delta)");

        /* A garbage baseline degrades to the full dump, never an error. */
        n = dr32_state_write(&a, "/kit.ablpreset", blob, (int)sizeof(blob), "not json");
        CHECK(n > 0 && strstr(blob, "pad2_") != NULL,
              "a malformed baseline did not fall back to the full dump");
    }

    /* ---- 5. The plugin must actually EXPOSE state ---------------------- */
    {
        /* This is the regression itself. The serializer above can be perfect
         * and the slot still persists nothing if get_param never answers
         * "state" — which is exactly the shape the bug had: every other key
         * worked, so the module looked healthy. Drive it through the plugin
         * API, the way the host does. */
        plugin_api_v2_t *api = move_plugin_init_v2(NULL);
        CHECK(api != NULL, "move_plugin_init_v2 returned NULL");
        if (api) {
            void *inst = api->create_instance(".", NULL);
            CHECK(inst != NULL, "create_instance returned NULL");
            if (inst) {
                /* A GLOBAL param, not a pad one. Pad state is only emitted for
                 * pads that actually hold a sample (an unloaded pad makes no
                 * sound, and a kit load resets pad params anyway) — and this
                 * test has no WAV on disk to load, so a pad edit here would be
                 * skipped by design. Pad round-tripping is covered above, where
                 * the kit can be populated directly. What THIS test is for is
                 * the wiring: that the plugin answers "state" at all and takes
                 * it back. That is what was missing. */
                api->set_param(inst, "master", "0.5");
                char out[65536] = "";
                int n = api->get_param(inst, "state", out, (int)sizeof(out));
                CHECK(n > 0, "get_param(\"state\") returned %d — the host would have "
                             "nothing to persist and the slot dies on reboot", n);
                CHECK(strstr(out, "\"params\"") != NULL,
                      "get_param(\"state\") did not return a state blob: '%.60s'", out);

                /* And it must be accepted back. */
                void *inst2 = api->create_instance(".", NULL);
                if (inst2 && n > 0) {
                    api->set_param(inst2, "state", out);
                    char v[64] = "";
                    api->get_param(inst2, "master", v, (int)sizeof(v));
                    CHECK(fabs(atof(v) - 0.5) < 1e-6,
                          "state did not restore through the plugin API: got '%s'", v);
                    api->destroy_instance(inst2);
                }
                api->destroy_instance(inst);
            }
        }
    }

    /* ---- 6. The served hierarchy carries the kit's pad names ------------ */
    {
        /* The host plans every page from get_param("ui_hierarchy"), which is
         * module.json's object with `child_names` spliced in (dr32.c). Drive it
         * from src/ so the real file is what gets spliced, load two samples so
         * two names are real, and hand the result to tests/run.sh, which
         * JSON-parses it and runs upstream's own voice resolver over it. A
         * splice that breaks the JSON would otherwise show up as "the module
         * has no pages", with nothing logged. */
        plugin_api_v2_t *api = move_plugin_init_v2(NULL);
        void *inst = api ? api->create_instance("src", NULL) : NULL;
        CHECK(inst != NULL, "create_instance(\"src\") returned NULL");
        if (inst) {
            const char *wa = "/tmp/dr32_state_kick.wav", *wb = "/tmp/dr32_state_snare.wav";
            make_wav(wa);
            make_wav(wb);
            api->set_param(inst, "pad1_sample", wa);
            api->set_param(inst, "pad2_sample", wb);
            static char h[65536];
            int n = api->get_param(inst, "ui_hierarchy", h, (int)sizeof(h));
            CHECK(n > 2, "ui_hierarchy not served (%d bytes)", n);
            CHECK(strstr(h, "\"pad_layout\": \"drums\"") != NULL, "pad_layout missing");
            CHECK(strstr(h, "\"child_index_param\": \"ui_current_pad\"") != NULL,
                  "child_index_param missing — the splice anchor is gone");
            /* ⭑ ONCE PER PAD BANK. Sample / Shape / Mix are three sibling
             * child levels, each naming the pads it draws, and a splice that
             * stopped at the first anchor left two of the three banks reading
             * "Pad 7" — a page that does not look broken, just like a
             * different pad. So count the anchors in the SOURCE and require
             * the same number of name arrays out. */
            int anchors = 0, spliced = 0;
            for (const char *q = h; (q = strstr(q, "\"child_index_param\"")); q++) anchors++;
            for (const char *q = h; (q = strstr(q, "\"child_names\": [")); q++) {
                spliced++;
                CHECK(strstr(q, "\"dr32_state_kick\", \"dr32_state_snare\", \"\"") == q + 16,
                      "pad names wrong at splice %d: %.80s", spliced, q);
            }
            CHECK(anchors >= 3, "only %d child_index_param anchors — the three pad banks are gone", anchors);
            CHECK(spliced == anchors,
                  "child_names spliced %d times for %d anchors — every pad bank needs its own names",
                  spliced, anchors);
            FILE *f = fopen("dist/tests/served_hierarchy.json", "w");
            if (f) { fputs(h, f); fclose(f); }
            api->destroy_instance(inst);
            remove(wa);
            remove(wb);
        }
    }

    /* ---- 6b. is_loading: the pulse that makes the host RE-READ the names -- */
    {
        /*
         * ⭑ SPLICING THE NAMES IS ONLY HALF THE JOB, AND THE OTHER HALF IS
         * THIS KEY.
         *
         * Check 6 above proves the served hierarchy carries the new pad names
         * the instant a sample changes — and on the device the header did not
         * change, because the host had no reason to read it again.
         * `armContractSettle` is called for a SELECTION (an items row, a preset
         * step); a knob turn and a filepath commit arm nothing. The one lever a
         * module has is `is_loading`: the shadow grid polls it and re-plans on
         * the loading -> ready EDGE. So a sample swap has to produce an edge.
         *
         * ⚠ Asserting only "1 after a swap" would pass with the disarm broken,
         * and asserting only the "0" would pass with the pulse never armed at
         * all. The edge is the property, so both ends are walked, plus the
         * re-arm (a browse SWEEP must cost one re-read, not one per detent)
         * and the idle case (never "1" when nothing changed).
         */
        plugin_api_v2_t *api = move_plugin_init_v2(NULL);
        void *inst = api ? api->create_instance("src", NULL) : NULL;
        CHECK(inst != NULL, "create_instance(\"src\") returned NULL");
        if (inst) {
            static int16_t sink[2 * 128];
            char v[32];
            #define ISLOAD() (api->get_param(inst, "is_loading", v, (int)sizeof v), v)
            #define BLOCKS(n) do { for (int i = 0; i < (n); i++) api->render_block(inst, sink, 128); } while (0)

            /* Idle: never "1", and never "" — an unserved key answers "" and
             * the host then stops asking FOR THE LIFE OF THE COMPONENT. */
            CHECK(!strcmp(ISLOAD(), "0"), "idle is_loading is '%s', want \"0\"", v);
            BLOCKS(400);
            CHECK(!strcmp(ISLOAD(), "0"), "is_loading went to '%s' with nothing changed", v);

            const char *wa = "/tmp/dr32_isload_a.wav", *wb = "/tmp/dr32_isload_b.wav";
            make_wav(wa);
            make_wav(wb);

            api->set_param(inst, "pad1_sample", wa);
            CHECK(!strcmp(ISLOAD(), "1"), "after a sample swap is_loading is '%s', want \"1\"", v);

            /* Part-way through, a SECOND swap re-arms: the window restarts, so
             * a sweep of the browse knob still produces exactly one edge. */
            BLOCKS(90);
            CHECK(!strcmp(ISLOAD(), "1"), "pulse ended early — is_loading '%s' at 90 blocks", v);
            api->set_param(inst, "pad1_sample", wb);
            BLOCKS(90);
            CHECK(!strcmp(ISLOAD(), "1"), "the second swap did not re-arm the window (is_loading '%s')", v);

            /* Left alone, it falls back to ready — that transition IS the edge. */
            BLOCKS(60);
            CHECK(!strcmp(ISLOAD(), "0"), "is_loading never returned to '0' (got '%s')", v);
            /* And it stays there: a pulse that re-armed itself would re-plan
             * the page forever. */
            BLOCKS(400);
            CHECK(!strcmp(ISLOAD(), "0"), "is_loading re-armed itself (got '%s')", v);

            #undef ISLOAD
            #undef BLOCKS
            api->destroy_instance(inst);
            remove(wa);
            remove(wb);
        }
    }

    {
        /*
         * THE MODULE-BUS SEND CONTRACT, WALKED END TO END.
         *
         * `voice_send_params` publishes a key TEMPLATE ("{id}_send_a") and the
         * host substitutes each `split_voices` id into it VERBATIM to read a
         * real parameter off this module. Nothing checks that the result
         * resolves: a key that addresses nothing is not an error anywhere in
         * the host, it is simply a send that never moves.
         *
         * That failed silently until 2026-09-08. split_voices published
         * `pad1`..`pad32` while dr32_params.c parses `pad<N>_` from ZERO, so
         * every level landed on the pad NEXT DOOR and pad32_send_a addressed
         * nothing at all.
         *
         * So this does not compare the two strings to constants — a test that
         * asserted "the ids are pad0..pad31" would have passed just as happily
         * with the templates changed instead. It SUBSTITUTES and then drives
         * the resulting key through set_param/get_param, which is the only
         * thing the host will actually do with them.
         */
        plugin_api_v2_t *api = move_plugin_init_v2(NULL);
        void *inst = api ? api->create_instance(".", NULL) : NULL;
        CHECK(inst != NULL, "create_instance returned NULL");
        if (inst) {
            char tmpl[256] = {0}, ids[8192] = {0};
            int nt = api->get_param(inst, "voice_send_params", tmpl, (int)sizeof(tmpl));
            int ni = api->get_param(inst, "split_voices", ids, (int)sizeof(ids));
            CHECK(nt > 0, "voice_send_params not served");
            CHECK(ni > 0, "split_voices not served");

            /* Array POSITION is the send index: [0] is Send A, [1] is Send B.
             * More than two is refused outright by the host. */
            CHECK(strcmp(tmpl, "[\"{id}_send_a\",\"{id}_send_b\"]") == 0,
                  "voice_send_params is '%s'", tmpl);

            /* Every entry must contain {id}; one without it would be a single
             * key shared by all 32 pads, i.e. one level for the whole kit. */
            int braces = 0;
            for (const char *q = tmpl; (q = strstr(q, "{id}")) != NULL; q += 4) braces++;
            CHECK(braces == 2, "%d of 2 templates carry {id}", braces);

            /* Walk the ids in order and drive each substituted key. */
            int n_ids = 0;
            for (const char *q = ids; (q = strstr(q, "\"id\":\"")) != NULL; ) {
                q += 6;
                char id[64]; size_t k = 0;
                while (*q && *q != '"' && k + 1 < sizeof id) id[k++] = *q++;
                id[k] = '\0';

                if (n_ids == 0)  CHECK(strcmp(id, "pad1") == 0,  "first id is '%s'", id);
                if (n_ids == 31) CHECK(strcmp(id, "pad32") == 0, "last id is '%s'", id);

                static const char *const suffix[2] = { "_send_a", "_send_b" };
                for (int sd = 0; sd < 2; sd++) {
                    char key[128], got[64];
                    snprintf(key, sizeof key, "%s%s", id, suffix[sd]);
                    /* A distinct value per pad and per send, so a key that
                     * resolves to the WRONG pad reads back somebody else's
                     * number rather than coincidentally matching. */
                    char want[32];
                    snprintf(want, sizeof want, "%d", -1 - n_ids - 40 * sd);
                    api->set_param(inst, key, want);
                    /* Cleared first: get_param leaves the buffer untouched when
                     * it answers nothing, so without this a FAILING check
                     * reports the PREVIOUS pad's value as what it read — a
                     * message that misdirects whoever is reading it. */
                    got[0] = '\0';
                    int g = api->get_param(inst, key, got, (int)sizeof(got));
                    CHECK(g > 0, "'%s' addresses nothing — the host would read no send here", key);
                    CHECK(g > 0 && strcmp(got, want) == 0,
                          "'%s' read back '%s', want '%s' (an id off by one reads the wrong pad)",
                          key, got, want);
                }
                n_ids++;
            }
            CHECK(n_ids == 32, "split_voices published %d ids, want 32 (the index IS the "
                               "voice_out[] index, so empties must be listed too)", n_ids);

            /* THE PROBE MUST BE ABLE TO FAIL. `pad32_*` is exactly the key the
             * old 1-based ids produced for the last pad; if this reads back a
             * value, the check above proves nothing. */
            char dead[64];
            CHECK(api->get_param(inst, "pad33_send_a", dead, (int)sizeof(dead)) == 0,
                  "pad33_send_a resolved — the negative control is broken, so the "
                  "substitution checks above cannot be trusted");

            api->destroy_instance(inst);
        }
    }

    {
        /*
         * DEFERRED AUDITION: a detent must move the cursor WITHOUT loading.
         *
         * Loading on every detent measured 5.3-10.1 ms per step on the SPI
         * callback against a 2.9 ms block — dropped frames for as long as you
         * scroll. The load is owed until the cursor has been still, and the only
         * thing that runs on a clock is render_block, so that is what pays it.
         *
         * ⚠ Driven through the PLUGIN API, not the helper. The whole point is
         * WHERE the load happens; a direct-call test would pass with the defer
         * wired to nothing. See test-the-path-not-the-function.
         */
        /* A REAL catalogue, or the assertions below are vacuous: with an empty
         * one the kit_index write returns early, nothing is ever pending, and
         * "a detent did not load" is true for the wrong reason. */
        sh("rm -rf /tmp/dr32_kr && mkdir -p /tmp/dr32_kr/core/Electronic /tmp/dr32_kr/user");
        for (int i = 0; i < 4; i++) {
            char pth[128];
            snprintf(pth, sizeof pth, "/tmp/dr32_kr/core/Electronic/k%d.json", i);
            FILE *kf = fopen(pth, "wb");
            if (kf) {
                fputs("{\n  \"kind\": \"instrumentRack\",\n", kf);
                for (int j = 0; j < 30; j++) fputs("  \"pad\": 0,\n", kf);
                fputs("  \"drumZoneSettings\": { \"receivingNote\": 36 }\n}\n", kf);
                fclose(kf);
            }
        }
        setenv("DR32_KIT_ROOTS", "/tmp/dr32_kr/core:/tmp/dr32_kr/user", 1);

        plugin_api_v2_t *api = move_plugin_init_v2(NULL);
        void *inst = api ? api->create_instance(".", NULL) : NULL;
        CHECK(inst != NULL, "create_instance returned NULL");
        if (inst) {
            char before[512] = {0}, after[512] = {0};
            /* Drive the catalogue to completion first. */
            for (int i = 0; i < 200; i++) {
                char c[64]; api->get_param(inst, "kit_count", c, (int)sizeof c);
            }
            char cnt[16] = {0};
            api->get_param(inst, "kit_count", cnt, (int)sizeof cnt);
            CHECK(atoi(cnt) == 4, "fixture catalogue has %s kits, want 4 — the checks below "
                                  "would be vacuous", cnt);
            api->get_param(inst, "kit", before, (int)sizeof before);

            /* Sweep the cursor. None of these may load. */
            for (int i = 0; i < 4; i++) {
                char v[16]; snprintf(v, sizeof v, "%d", i);
                api->set_param(inst, "kit_index", v);
            }
            api->get_param(inst, "kit", after, (int)sizeof after);
            CHECK(!strcmp(before, after),
                  "a detent loaded a kit — scrolling would drop frames on every step");

            /* The cursor still moved, or the page would look dead. */
            char idx[16] = {0};
            api->get_param(inst, "kit_index", idx, (int)sizeof idx);
            CHECK(!strcmp(idx, "3"), "the cursor did not follow the detents (read '%s')", idx);

            /* And rendering long enough must pay it. On a build host the
             * catalogue is empty, so nothing can actually load — what is
             * asserted is that the DEBT is cleared rather than owed forever. */
            static int16_t out[2 * 128];
            for (int b = 0; b < 200; b++) api->render_block(inst, out, 128);
            char loaded[512] = {0};
            api->get_param(inst, "kit", loaded, (int)sizeof loaded);
            CHECK(strstr(loaded, "/tmp/dr32_kr/") != NULL,
                  "rendering past the settle window did not pay the deferred load (kit '%s')",
                  loaded);

            api->destroy_instance(inst);
        }
        unsetenv("DR32_KIT_ROOTS");
        sh("rm -rf /tmp/dr32_kr");
    }

    printf("%s  (%d checks, %d failures)\n", failures ? "FAILED" : "ok", checks, failures);
    return failures ? 1 : 0;
}
