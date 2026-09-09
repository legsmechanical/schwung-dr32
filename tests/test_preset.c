/* ⚠ THE TESTS BUILD -std=c11, AND glibc HIDES EVERYTHING NOT IN IT.
 * mkdtemp, setenv, utimensat and struct timespec are POSIX; M_PI is XSI.
 * macOS headers expose all of them regardless, so an omission here is
 * invisible locally and a hard -Werror failure on Linux — which is where
 * the module is actually built.
 *
 * ⚠ _XOPEN_SOURCE 700, not _POSIX_C_SOURCE 200809L. The POSIX macro alone
 * implies the same POSIX level but SUPPRESSES the XSI additions, so it
 * fixes mkdtemp and then takes M_PI away — asking for less, not more.
 * One macro, uniform across the suite, so a new test file cannot pick the
 * one that happens not to cover what it uses. */
#define _XOPEN_SOURCE 700

// Preset loading tests — the C path that actually runs on the device.
//
// This is the test that was missing: everything about kit loading used to live
// in ui.js, which never runs in a chain slot, so the module loaded a kit and
// stayed silent with nothing failing anywhere.

#include "../dsp/dr32_preset.h"
#include "../dsp/dr32_json.h"
#include "../dsp/dr32_params.h"

#include "../dsp/dr32_kit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* A 1 s 16-bit mono WAV, for the stale-audio check below. Same shape as
 * test_kit.c's helper; duplicated rather than shared because these two suites
 * are separate translation units with no common test header. */
#define PRESET_TEST_SR 44100
static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }
static void make_wav(const char *path, float level) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = PRESET_TEST_SR * 2;
    fputs("RIFF", f); w32(f, 4 + 24 + 8 + data); fputs("WAVE", f);
    fputs("fmt ", f); w32(f, 16); w16(f, 1); w16(f, 1); w32(f, PRESET_TEST_SR);
    w32(f, PRESET_TEST_SR * 2); w16(f, 2); w16(f, 16);
    fputs("data", f); w32(f, data);
    for (int i = 0; i < PRESET_TEST_SR; i++) w16(f, (uint16_t)(int16_t)(level * 32767.0f));
    fclose(f);
}

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

    // ---- a preset load must leave NO stale audio on a pad it does not fill.
    //
    // dr32_preset_load used to clear every pad's sample up front. It no longer
    // does: clearing first frees the buffers, so dr32_kit_load_sample's decode
    // memo had nothing to compare against and could never hit — the reason the
    // first version of that memo was a NO-OP on device while its own unit test
    // passed (it called load_sample directly and never went through here).
    //
    // The clear is now deferred to the end and applies to every pad the preset
    // did not fill. THIS is the check that keeps that safe: an absent,
    // unresolvable or failed sampleUri, and any pad past the chain count, must
    // all end EMPTY rather than keeping whatever the pad held before.
    {
        dr32_kit kit;
        dr32_kit_init(&kit);

        const char *wp = "/tmp/dr32_preset_stale.wav";
        make_wav(wp, 0.5f);
        // Pads across the range, including ones this fixture cannot fill.
        CHECK(dr32_kit_load_sample(&kit, 0,  wp) == DR32_WAV_OK, "stale: preload pad 0");
        CHECK(dr32_kit_load_sample(&kit, 5,  wp) == DR32_WAV_OK, "stale: preload pad 5");
        CHECK(dr32_kit_load_sample(&kit, 31, wp) == DR32_WAV_OK, "stale: preload pad 31");
        CHECK(kit.pads[31].sample != NULL, "stale: preload took");

        dr32_preset_report rep2;
        CHECK(dr32_preset_load(&kit, fixture, &rep2), "stale: fixture loaded");

        // Off-device the fixture's sampleUris resolve to /data/... which does
        // not exist, so every pad FAILS to load — precisely the case that must
        // not leave the old audio behind.
        for (int i = 0; i < DR32_PADS; i++) {
            if (kit.pads[i].sample != NULL) {
                CHECK(0, "stale: pad %d kept audio the preset did not give it", i);
                break;
            }
        }
        CHECK(kit.pads[31].sample == NULL, "stale: pad past the chain count kept audio");

        dr32_kit_free(&kit);
        remove(wp);
    }

    // ---- a missing / wrong file must fail cleanly
    {
        dr32_kit kit;
        dr32_kit_init(&kit);
        CHECK(!dr32_preset_load(&kit, "/nonexistent/x.ablpreset", NULL), "missing file");
        CHECK(!dr32_preset_load(&kit, "/etc/hostname", NULL), "non-JSON file");
        dr32_kit_free(&kit);
    }

    // ---- assigning a sample must NEVER sound on its own, and playing a pad
    //      must use the incoming velocity (not a fixed audition level).
    {
        const char *wav = "/tmp/dr32_preview.wav";
        FILE *f = fopen(wav, "wb");
        if (f) {
            int n = 4410;                       /* 0.1 s of 16-bit mono DC */
            unsigned char hdr[44] = {0};
            memcpy(hdr, "RIFF", 4); memcpy(hdr + 8, "WAVEfmt ", 8);
            unsigned fmtsz = 16, rate = 44100, brate = 88200, data = (unsigned)n * 2, riff = 36 + data;
            memcpy(hdr + 4, &riff, 4); memcpy(hdr + 16, &fmtsz, 4);
            hdr[20] = 1; hdr[22] = 1;
            memcpy(hdr + 24, &rate, 4); memcpy(hdr + 28, &brate, 4);
            hdr[32] = 2; hdr[34] = 16;
            memcpy(hdr + 36, "data", 4); memcpy(hdr + 40, &data, 4);
            fwrite(hdr, 1, 44, f);
            for (int i = 0; i < n; i++) { unsigned char s16[2] = {0x00, 0x40}; fwrite(s16, 1, 2, f); }
            fclose(f);

            dr32_kit kit;
            dr32_kit_init(&kit);
            dr32_apply_param(&kit, "pad1_sample", wav);
            CHECK(!kit.pads[0].voice.active, "assigning a sample must not sound by itself");

            /* velocity reaches the voice: the engine's dB law is centred on 70,
             * so a hard hit is louder than a soft one for the same sample. */
            kit.pads[0].params.vel_to_volume = 0.5f;
            dr32_kit_note_on(&kit, kit.pads[0].note, 20);
            float soft = kit.pads[0].voice.amp;
            dr32_kit_note_on(&kit, kit.pads[0].note, 120);
            float hard = kit.pads[0].voice.amp;
            CHECK(hard > soft * 2.0f, "velocity must scale playback: soft %.4f vs hard %.4f", soft, hard);

            dr32_kit_free(&kit);
            remove(wav);
        }
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
