// Preset loading tests — the C path that actually runs on the device.
//
// This is the test that was missing: everything about kit loading used to live
// in ui.js, which never runs in a chain slot, so the module loaded a kit and
// stayed silent with nothing failing anywhere.

#include "../dsp/dr32_preset.h"
#include "../dsp/dr32_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

int main(int argc, char **argv) {
    const char *fixture = (argc > 1) ? argv[1] : "tests/fixtures/native-16pad.ablpreset";
    printf("preset loader\n");

    // ---- JSON layer
    {
        dr32_json *v = dr32_json_parse("{\"a\":1.5,\"b\":\"x\\u0020y\",\"c\":[1,2,3],\"d\":true,\"e\":null}");
        CHECK(v != NULL, "parse failed");
        if (v) {
            CHECK(dr32_json_num(v, "a", 0) == 1.5, "number");
            CHECK(!strcmp(dr32_json_str(v, "b", ""), "x y"), "string escape: %s", dr32_json_str(v, "b", ""));
            CHECK(dr32_json_count(dr32_json_get(v, "c")) == 3, "array count");
            CHECK(dr32_json_num(dr32_json_get(v, "c"), NULL, 0) == 0, "no key on array");
            CHECK(dr32_json_at(dr32_json_get(v, "c"), 2)->num == 3, "array index");
            CHECK(dr32_json_bool(v, "d", 0) == 1, "bool");
            CHECK(dr32_json_get(v, "e")->type == DR32_JSON_NULL, "null");
            CHECK(dr32_json_get(v, "zz") == NULL, "missing key");
            dr32_json_free(v);
        }
        CHECK(dr32_json_parse("{unterminated") == NULL, "malformed input must not crash or succeed");
        CHECK(dr32_json_parse("") == NULL, "empty input");
    }

    // ---- URI resolution, including the factory Core Library root
    {
        char out[512];
        CHECK(dr32_resolve_uri("ableton:/user-library/Samples/Preset%20Samples/A.wav", out, sizeof(out)),
              "user-library resolve");
        CHECK(!strcmp(out, "/data/UserData/UserLibrary/Samples/Preset Samples/A.wav"),
              "percent-decode: %s", out);
        CHECK(dr32_resolve_uri("ableton:/packs/abl-core-library/Samples/Drums/Kick/K.wav", out, sizeof(out)),
              "core-library resolve");
        CHECK(!strcmp(out, "/data/CoreLibrary/Samples/Drums/Kick/K.wav"), "core path: %s", out);
        CHECK(!dr32_resolve_uri("ableton:/unknown-root/x.wav", out, sizeof(out)), "unknown root rejected");
    }

    // ---- a real device kit
    {
        dr32_kit kit;
        dr32_kit_init(&kit);
        dr32_preset_report rep;
        int ok = dr32_preset_load(&kit, fixture, &rep);
        CHECK(ok, "failed to load %s", fixture);
        if (ok) {
            printf("  %s: %d pads, %d samples, %d empty, %d unresolved, %d failed\n",
                   fixture, rep.pads, rep.loaded, rep.empty, rep.unresolved, rep.failed);
            CHECK(rep.pads >= 16, "expected >= 16 pad chains, got %d", rep.pads);
            CHECK(rep.unresolved == 0, "%d sample URIs had an unknown root", rep.unresolved);

            // Notes come from the chain, not the index — this kit routes 36..51.
            CHECK(kit.note_to_pad[36] >= 0, "note 36 unmapped");
            CHECK(kit.note_to_pad[51] >= 0, "note 51 unmapped");

            // Params must actually arrive, not sit at defaults.
            int nondefault = 0;
            for (int i = 0; i < rep.pads; i++) {
                const dr32_pad *p = &kit.pads[i].params;
                CHECK(p->sending_note > 0, "pad %d sending note %d", i, p->sending_note);
                CHECK(p->hold > 0.0f, "pad %d hold %f", i, p->hold);
                CHECK(p->cutoff >= 30.0f, "pad %d cutoff %f", i, p->cutoff);
                if (p->cell_volume_db != 0.0f || p->transpose != 0.0f) nondefault++;
            }
            CHECK(nondefault > 0, "no pad carried a non-default value — params are not being read");
        }
        dr32_kit_free(&kit);
    }

    // ---- a missing / wrong file must fail cleanly
    {
        dr32_kit kit;
        dr32_kit_init(&kit);
        CHECK(!dr32_preset_load(&kit, "/nonexistent/x.ablpreset", NULL), "missing file");
        CHECK(!dr32_preset_load(&kit, "/etc/hostname", NULL), "non-JSON file");
        dr32_kit_free(&kit);
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
