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

int main(void) {
    printf("test_state\n");

    /* ---- 1. A written blob restores every edited value ------------------ */
    {
        dr32_kit a; dr32_kit_init(&a);
        occupy(&a, 0, "/data/UserData/Samples/kick.wav");
        occupy(&a, 3, "/data/UserData/Samples/snare.wav");

        /* Spread across the kinds of field that exist: a bipolar int, a float,
         * an enum-ish, a global, and a send. */
        dr32_apply_param(&a, "pad0_transpose", "-5");
        dr32_apply_param(&a, "pad0_decay",     "0.25");
        dr32_apply_param(&a, "pad0_choke",     "3");
        dr32_apply_param(&a, "pad3_pan",       "-20");
        dr32_apply_param(&a, "master",         "0.5");
        dr32_apply_param(&a, "bus_crunch",     "0.7");

        int n = dr32_state_write(&a, "/data/UserData/Kits/MyKit.ablpreset",
                                 blob, (int)sizeof(blob), NULL);
        CHECK(n > 0, "state_write returned %d (expected > 0)", n);
        CHECK(strstr(blob, "\"kit\":\"/data/UserData/Kits/MyKit.ablpreset\"") != NULL,
              "blob does not carry the kit path");
        CHECK(strstr(blob, "pad0_transpose") != NULL, "blob omits an edited pad field");

        /* A fresh kit, restored from the blob. No load_kit callback: this test
         * is about the params, and a real preset load needs files on disk. */
        dr32_kit b; dr32_kit_init(&b);
        occupy(&b, 0, "x"); occupy(&b, 3, "x");
        CHECK(dr32_state_read(&b, blob, NULL, NULL) == 1, "state_read rejected its own output");

        char va[64], vb[64];
        const char *keys[] = { "pad0_transpose", "pad0_decay", "pad0_choke",
                               "pad3_pan", "master", "bus_crunch", NULL };
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
        dr32_apply_param(&a, "pad1_transpose", "-5");
        CHECK(dr32_state_write(&a, "", blob, (int)sizeof(blob), NULL) > 0, "write failed");

        dr32_kit b; dr32_kit_init(&b);
        occupy(&b, 1, "/s.wav");
        dr32_state_read(&b, blob, NULL, NULL);
        char v[64]; rd(&b, "pad1_transpose", v, sizeof(v));
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
        CHECK(dr32_state_read(&b, "{\"v\":1,\"params\":{\"pad0_nosuchfield\":\"1\"}}",
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
        dr32_apply_param(&a, "pad0_transpose", "-5");
        int n = dr32_state_write(&a, "/kit.ablpreset", blob, (int)sizeof(blob), base);
        CHECK(n > 0, "delta write failed");
        CHECK(strstr(blob, "pad0_transpose") != NULL, "delta blob omits the edit");
        CHECK(strstr(blob, "pad1_") == NULL,
              "delta blob carries unedited pad1 fields: %.200s", blob);
        CHECK(n < 512, "delta blob is %d bytes — not a delta", n);

        /* An edit reverted to its baseline value drops back out. */
        char orig[64]; rd(&a, "pad0_transpose", orig, sizeof(orig));
        (void)orig;
        dr32_kit c; dr32_kit_init(&c);
        occupy(&c, 0, "/s0.wav");
        char base_v[64]; rd(&c, "pad0_transpose", base_v, sizeof(base_v));
        dr32_apply_param(&a, "pad0_transpose", base_v);
        n = dr32_state_write(&a, "/kit.ablpreset", blob, (int)sizeof(blob), base);
        CHECK(n > 0 && strstr(blob, "pad0_transpose") == NULL,
              "a reverted edit still appears in the delta blob");

        /* A baseline for a DIFFERENT kit path must be IGNORED — comparing
         * against another kit's values would silently drop real edits. */
        dr32_apply_param(&a, "pad0_transpose", "-5");
        n = dr32_state_write(&a, "/OTHER.ablpreset", blob, (int)sizeof(blob), base);
        CHECK(n > 0 && strstr(blob, "pad1_") != NULL,
              "a mismatched-kit baseline was honoured (blob still a delta)");

        /* A garbage baseline degrades to the full dump, never an error. */
        n = dr32_state_write(&a, "/kit.ablpreset", blob, (int)sizeof(blob), "not json");
        CHECK(n > 0 && strstr(blob, "pad1_") != NULL,
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

    printf("%s  (%d checks, %d failures)\n", failures ? "FAILED" : "ok", checks, failures);
    return failures ? 1 : 0;
}
