// Kit-layer tests: note map, choke groups, sample swap safety, 32-pad range.

#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_params.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define SR 44100
static float out[2 * 512];

static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }

/** A 1 s 16-bit mono WAV of constant `level`. */
static void make_wav(const char *path, float level) {
    FILE *f = fopen(path, "wb");
    uint32_t data = SR * 2;
    fputs("RIFF", f); w32(f, 4 + 24 + 8 + data); fputs("WAVE", f);
    fputs("fmt ", f); w32(f, 16); w16(f, 1); w16(f, 1); w32(f, SR);
    w32(f, SR * 2); w16(f, 2); w16(f, 16);
    fputs("data", f); w32(f, data);
    for (int i = 0; i < SR; i++) w16(f, (uint16_t)(int16_t)(level * 32767.0f));
    fclose(f);
}

static float peak(dr32_kit *k, int n) {
    dr32_kit_render(k, out, n);
    float p = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(out[2 * i]); if (a > p) p = a; }
    return p;
}

int main(void) {
    const char *wa = "/tmp/dr32_kit_a.wav", *wb = "/tmp/dr32_kit_b.wav";
    make_wav(wa, 0.5f);
    make_wav(wb, 0.25f);

    printf("kit layer\n");

    dr32_kit k;
    dr32_kit_init(&k);

    // ---- default note map covers 36..67 across 32 pads
    CHECK(k.note_to_pad[36] == 0, "note 36 -> pad %d", k.note_to_pad[36]);
    CHECK(k.note_to_pad[67] == 31, "note 67 -> pad %d", k.note_to_pad[67]);
    CHECK(k.note_to_pad[35] == -1, "note 35 should be unmapped");
    CHECK(k.note_to_pad[68] == -1, "note 68 should be unmapped");

    // ---- the upper bank (17-32) is real: note 67 must sound
    CHECK(dr32_kit_load_sample(&k, 31, wa) == DR32_WAV_OK, "load into pad 31");
    for (int i = 0; i < DR32_PADS; i++) {
        k.pads[i].params.hold = DR32_HOLD_INFINITE;
        k.pads[i].params.attack = 0.0001f;
        k.pads[i].params.filter_on = 0;
        k.pads[i].params.vel_to_volume = 0.0f;
    }
    dr32_kit_note_on(&k, 67, 127);
    CHECK(peak(&k, 512) > 0.4f, "note 67 (pad 32) produced no sound");
    dr32_kit_all_off(&k);

    // ---- unmapped note is silent, not a crash
    dr32_kit_note_on(&k, 100, 127);
    CHECK(peak(&k, 512) == 0.0f, "unmapped note produced sound");

    // ---- remapping a note moves the pad and frees the old slot
    dr32_kit_set_note(&k, 31, 80);
    CHECK(k.note_to_pad[80] == 31, "remap failed");
    CHECK(k.note_to_pad[67] == -1, "old note still mapped after remap");
    dr32_kit_note_on(&k, 80, 127);
    CHECK(peak(&k, 512) > 0.4f, "remapped note silent");
    dr32_kit_all_off(&k);
    dr32_kit_set_note(&k, 31, 67);

    // ---- choke: same group cuts, different group does not
    CHECK(dr32_kit_load_sample(&k, 0, wa) == DR32_WAV_OK, "load pad 0");
    CHECK(dr32_kit_load_sample(&k, 1, wa) == DR32_WAV_OK, "load pad 1");
    CHECK(dr32_kit_load_sample(&k, 2, wa) == DR32_WAV_OK, "load pad 2");
    k.pads[0].params.choke_group = 1;
    k.pads[1].params.choke_group = 1;
    k.pads[2].params.choke_group = 2;

    dr32_kit_note_on(&k, 36, 127);          // pad 0
    peak(&k, 512);
    CHECK(k.pads[0].voice.active, "pad 0 should be sounding");
    dr32_kit_note_on(&k, 37, 127);          // pad 1, same group -> chokes pad 0
    for (int b = 0; b < 20; b++) peak(&k, 512);   // ~230 ms, past the 3 ms fade
    CHECK(!k.pads[0].voice.active, "pad 0 was not choked by its group-mate");
    CHECK(k.pads[1].voice.active, "pad 1 choked itself");

    dr32_kit_note_on(&k, 38, 127);          // pad 2, different group
    peak(&k, 512);
    CHECK(k.pads[1].voice.active, "pad 1 choked by a DIFFERENT group");
    dr32_kit_all_off(&k);
    k.pads[0].params.choke_group = k.pads[1].params.choke_group = k.pads[2].params.choke_group = 0;

    // ---- swapping a pad's sample mid-flight must not use freed memory
    dr32_kit_note_on(&k, 36, 127);
    peak(&k, 256);
    CHECK(dr32_kit_load_sample(&k, 0, wb) == DR32_WAV_OK, "hot swap failed");
    CHECK(!k.pads[0].voice.active, "voice should stop when its sample is swapped");
    CHECK(peak(&k, 512) == 0.0f, "swapped pad still sounding");
    dr32_kit_note_on(&k, 36, 127);
    float lvl = peak(&k, 512);
    CHECK(lvl > 0.2f && lvl < 0.3f, "after swap expected ~0.25, got %.3f", lvl);

    // ---- clearing a pad
    CHECK(dr32_kit_load_sample(&k, 0, NULL) == DR32_WAV_OK, "clear pad");
    dr32_kit_note_on(&k, 36, 127);
    CHECK(peak(&k, 512) == 0.0f, "cleared pad still sounds");

    // ---- missing file is an error, not a crash, and leaves the pad empty
    CHECK(dr32_kit_load_sample(&k, 5, "/nope/missing.wav") == DR32_WAV_ERR_OPEN, "missing file");
    dr32_kit_note_on(&k, 41, 127);
    CHECK(peak(&k, 512) == 0.0f, "pad with failed load produced sound");

    // ---- all 32 pads at once: mix stays finite and voices are counted
    for (int i = 0; i < DR32_PADS; i++) dr32_kit_load_sample(&k, i, wb);
    for (int i = 0; i < DR32_PADS; i++) dr32_kit_note_on(&k, DR32_FIRST_NOTE + i, 127);
    CHECK(dr32_kit_active_voices(&k) == DR32_PADS, "expected 32 active voices, got %d",
          dr32_kit_active_voices(&k));
    float full = peak(&k, 512);
    CHECK(isfinite(full), "32-voice mix went non-finite");

    // ---- live-press correlation: focus follows a VOUCHED note, in either order
    //
    // The canvas ("a finger pressed a pad") and the note ("which pad") arrive
    // from different processes, so neither order can be assumed. Both must land
    // on the same pad, and an unvouched note must not move focus at all — that
    // last one is the whole reason the DSP stopped following notes.
    {
        dr32_kit t;
        dr32_kit_init(&t);

        // Sequenced note alone: no press signal, focus must not move.
        t.ui_current_pad = 3;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 9, 100);
        CHECK(t.ui_current_pad == 3, "unvouched note moved focus to %d", t.ui_current_pad);

        // Note first, then the press signal catches up (the common order).
        t.ui_current_pad = 0;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 7, 100);
        dr32_apply_param(&t, "ui_live_press", "1");
        CHECK(t.ui_current_pad == 7, "note-then-press focused %d, want 7", t.ui_current_pad);

        // Press signal first, note arrives after.
        t.ui_current_pad = 0;
        dr32_apply_param(&t, "ui_live_press", "1");
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 21, 100);
        CHECK(t.ui_current_pad == 21, "press-then-note focused %d, want 21", t.ui_current_pad);

        // Reaches the upper 16 — the case a grid-note mapping could never
        // address, since a transposed pad sends the identical grid note.
        t.ui_current_pad = 0;
        dr32_apply_param(&t, "ui_live_press", "1");
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 31, 100);
        CHECK(t.ui_current_pad == 31, "upper-bank press focused %d, want 31", t.ui_current_pad);

        // One press signal vouches for ONE note; the next note is not carried.
        t.ui_current_pad = 0;
        dr32_apply_param(&t, "ui_live_press", "1");
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 5, 100);
        t.block += DR32_LIVE_MATCH_BLOCKS + 1;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 12, 100);
        CHECK(t.ui_current_pad == 5, "second note stole focus to %d, want 5", t.ui_current_pad);

        // A stale note outside the window must not be claimed by a later press.
        t.ui_current_pad = 2;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 17, 100);
        t.block += DR32_LIVE_MATCH_BLOCKS + 1;
        dr32_apply_param(&t, "ui_live_press", "1");
        CHECK(t.ui_current_pad == 2, "stale note claimed by press: %d", t.ui_current_pad);

        // ...but that press stays armed and takes the NEXT note.
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 4, 100);
        CHECK(t.ui_current_pad == 4, "armed press missed its note: %d", t.ui_current_pad);

        // auto-select off (a browser is open) suspends the whole mechanism.
        t.ui_auto_select_pad = 0;
        t.ui_current_pad = 1;
        dr32_apply_param(&t, "ui_live_press", "1");
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 30, 100);
        CHECK(t.ui_current_pad == 1, "focus moved while auto-select off: %d", t.ui_current_pad);

        dr32_kit_free(&t);
    }

    dr32_kit_free(&k);
    remove(wa); remove(wb);

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
