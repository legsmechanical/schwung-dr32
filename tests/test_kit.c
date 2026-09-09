// Kit-layer tests: note map, choke groups, sample swap safety, 32-pad range.

#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_params.h"

#include <sys/stat.h>
#include <fcntl.h>       /* AT_FDCWD, for utimensat in the decode-memo test */
#include <unistd.h>      /* rmdir */

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

    // ---- folder browse: walk the samples beside the pad's own
    //
    // Exercises real filesystem behaviour, which is where this breaks quietly:
    // sort order, extension filtering, clamping, and the empty-pad case.
    {
        const char *dir = "/tmp/dr32_browse";
        mkdir(dir, 0755);
        // Created OUT of alphabetical order — the listing must sort, not echo
        // readdir order, which is arbitrary.
        make_wav("/tmp/dr32_browse/c.wav", 0.5f);
        make_wav("/tmp/dr32_browse/a.wav", 0.5f);
        make_wav("/tmp/dr32_browse/B.wav", 0.5f);      // case-insensitive sort
        FILE *junk = fopen("/tmp/dr32_browse/notes.txt", "w");
        if (junk) { fputs("not audio", junk); fclose(junk); }

        dr32_kit b;
        dr32_kit_init(&b);
        CHECK(dr32_kit_load_sample(&b, 0, "/tmp/dr32_browse/B.wav") == DR32_WAV_OK,
              "browse fixture failed to load");

        // notes.txt must not be listed: selecting it would fail to load and the
        // indices would no longer line up with what the picker shows.
        CHECK(dr32_kit_browse_count(&b, 0) == 3, "browse counted %d, want 3 (.txt filtered?)",
              dr32_kit_browse_count(&b, 0));
        CHECK(dr32_kit_browse_index(&b, 0) == 1, "B.wav should sort to index 1, got %d",
              dr32_kit_browse_index(&b, 0));

        CHECK(dr32_kit_browse_select(&b, 0, 0) == 0, "select(0) failed");
        CHECK(dr32_kit_browse_index(&b, 0) == 0, "select(0) did not move focus");
        CHECK(strstr(b.pads[0].path, "a.wav") != NULL, "select(0) loaded %s, want a.wav",
              b.pads[0].path);

        // Past either end must CLAMP, not wrap: a knob should stop at the edge
        // of the folder rather than jumping to the far end.
        CHECK(dr32_kit_browse_select(&b, 0, 99) == 2, "select past the end did not clamp");
        CHECK(strstr(b.pads[0].path, "c.wav") != NULL, "clamped select loaded %s, want c.wav",
              b.pads[0].path);
        CHECK(dr32_kit_browse_select(&b, 0, -5) == 0, "select below zero did not clamp");

        // An empty pad has no folder at all.
        CHECK(dr32_kit_browse_count(&b, 7) == 0, "empty pad reported a folder");
        CHECK(dr32_kit_browse_index(&b, 7) == -1, "empty pad reported an index");
        CHECK(dr32_kit_browse_select(&b, 7, 0) == -1, "empty pad accepted a selection");

        dr32_kit_free(&b);
        remove("/tmp/dr32_browse/a.wav"); remove("/tmp/dr32_browse/B.wav");
        remove("/tmp/dr32_browse/c.wav"); remove("/tmp/dr32_browse/notes.txt");
        rmdir(dir);
    }

    // ---- live-press correlation: focus follows a VOUCHED note, in either order
    //
    // The canvas ("a finger pressed a pad") and the note ("which pad") arrive
    // from different processes, so neither order can be assumed. Both must land
    // on the same pad, and an unvouched note must not move focus at all — that
    // last one is the whole reason the DSP stopped following notes.
    {
        dr32_kit t;
        dr32_kit_init(&t);
        // Everything in this block is the TRANSPORT-RUNNING regime, where a
        // note cannot be trusted on its own. dr32_kit_init leaves the flag 0
        // (= stopped), which is the follow-freely regime tested further down.
        t.transport_running = 1;

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

    // ---- transport STOPPED: a note IS a hand, so focus follows it with no vouch
    //
    // This is what makes pad-follow work on a host that has no vouch mechanism
    // at all (upstream Schwung 1.2, whose param pages never tell a synth about a
    // physical press). The running-transport rules above are unchanged.
    {
        dr32_kit t;
        dr32_kit_init(&t);
        CHECK(t.transport_running == 0, "kit_init must start STOPPED (got %d)", t.transport_running);

        t.ui_current_pad = 3;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 9, 100);
        CHECK(t.ui_current_pad == 9, "stopped: bare note did not move focus (%d)", t.ui_current_pad);

        // Reaches the upper bank too.
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 29, 100);
        CHECK(t.ui_current_pad == 29, "stopped: upper-bank note focused %d, want 29", t.ui_current_pad);

        // An unmapped note is not a pad and moves nothing.
        dr32_kit_note_on(&t, 100, 100);
        CHECK(t.ui_current_pad == 29, "stopped: unmapped note moved focus to %d", t.ui_current_pad);

        // A vouch that arrived for this same press is consumed, not left armed:
        // otherwise, the moment the transport started, that stale arm would
        // claim the first SEQUENCED note inside the window.
        dr32_apply_param(&t, "ui_live_press", "1");
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 2, 100);
        CHECK(t.ui_current_pad == 2 && t.live_armed == 0,
              "stopped: vouch not consumed (pad %d, armed %d)", t.ui_current_pad, t.live_armed);
        t.transport_running = 1;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 11, 100);
        CHECK(t.ui_current_pad == 2, "stale arm claimed a sequenced note (%d)", t.ui_current_pad);
        t.transport_running = 0;

        // Follow Pads off still means off.
        t.ui_auto_select_pad = 0;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 20, 100);
        CHECK(t.ui_current_pad == 2, "stopped: focus moved while auto-select off (%d)", t.ui_current_pad);

        dr32_kit_free(&t);
    }

    // ---- a host that VOUCHES owns liveness: bare notes never follow again
    //
    // dAVEBOx vouches for its live presses and runs its own sequencer, and it
    // may never tell us its transport is running -- so "stopped" followed every
    // sequenced note and the sequencer dragged the editor. Once a vouch has
    // been seen, the transport flag no longer buys a bare note anything.
    {
        dr32_kit t;
        dr32_kit_init(&t);
        CHECK(t.host_vouches == 0, "kit_init must start with no host vouch");

        // Before any vouch, stopped-regime follow works as above.
        t.ui_current_pad = 0;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 6, 100);
        CHECK(t.ui_current_pad == 6, "pre-vouch stopped follow broken (%d)", t.ui_current_pad);

        // The host vouches once (davebox's press path, or the grid's).
        dr32_apply_param(&t, "ui_live_press", "1");
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 9, 100);
        CHECK(t.ui_current_pad == 9, "vouched press did not focus (%d)", t.ui_current_pad);

        // From now on a bare note -- the sequencer -- moves nothing, even with
        // the transport reported stopped.
        t.block += DR32_LIVE_MATCH_BLOCKS + 1;
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 14, 100);
        CHECK(t.ui_current_pad == 9, "sequenced note dragged focus after a vouch (%d)", t.ui_current_pad);

        // ...while a vouched press, and a named note, still do.
        dr32_apply_param(&t, "ui_live_note", "40");
        CHECK(t.ui_current_pad == 4, "ui_live_note ignored after vouch mode (%d)", t.ui_current_pad);
        dr32_apply_param(&t, "ui_live_press", "1");
        dr32_kit_note_on(&t, DR32_FIRST_NOTE + 22, 100);
        CHECK(t.ui_current_pad == 22, "vouched press ignored after vouch mode (%d)", t.ui_current_pad);

        dr32_kit_free(&t);
    }

    // ---- `end` is an alias of length (start + length), for the host's trim editor
    {
        dr32_kit t;
        dr32_kit_init(&t);
        char buf[32];
        dr32_apply_param(&t, "pad5_start", "0.25");
        dr32_apply_param(&t, "pad5_length", "0.5");
        dr32_read_param(&t, "pad5_end", buf, sizeof(buf));
        CHECK(!strcmp(buf, "0.75"), "end read %s, want 0.75", buf);

        dr32_apply_param(&t, "pad5_end", "0.6");
        dr32_read_param(&t, "pad5_length", buf, sizeof(buf));
        CHECK(!strcmp(buf, "0.35"), "length after end write %s, want 0.35", buf);

        // The alias follows the focused pad too.
        t.ui_current_pad = 4;
        dr32_read_param(&t, "pad_end", buf, sizeof(buf));
        CHECK(!strcmp(buf, "0.6"), "aliased end read %s, want 0.6", buf);

        // Clamped: an end before the start is an empty region, never negative,
        // and start + length never reads past the file.
        dr32_apply_param(&t, "pad5_end", "0.1");
        dr32_read_param(&t, "pad5_length", buf, sizeof(buf));
        CHECK(!strcmp(buf, "0"), "end before start gave length %s, want 0", buf);
        dr32_apply_param(&t, "pad5_length", "1");
        dr32_read_param(&t, "pad5_end", buf, sizeof(buf));
        CHECK(!strcmp(buf, "1"), "overlong region read end %s, want 1", buf);

        dr32_kit_free(&t);
    }

    /* ---- the decode memo: a reload of the SAME file must not re-decode.
     *
     * Motivating measurement (device, 2026-09-08, via the newly ported
     * param-slow): a state recall re-reads all 16 pads and the WAV decode was
     * ~3.5 ms of it, ON the SPI callback, against a 2.9 ms block period —
     * every recall blew a frame.
     *
     * ⚠ The memo is at the DECODE, never at the reload. dr32_state_read loads
     * the kit first because it replaces every pad wholesale and the saved blob
     * carries only the user's deltas from that baseline; skipping the reload
     * would apply deltas to whatever was edited since, turning a restore into
     * a merge. Nothing below may be "simplified" into a path check in
     * dr32_state_read. */
    {
        dr32_kit m;
        dr32_kit_init(&m);
        const char *wm = "/tmp/dr32_kit_memo.wav";
        make_wav(wm, 0.5f);

        CHECK(dr32_kit_load_sample(&m, 0, wm) == DR32_WAV_OK, "memo: first load");
        const float *first = m.pads[0].sample;
        CHECK(first != NULL, "memo: first load produced a buffer");

        /* Same file, unchanged: the SAME buffer survives. Pointer identity is
         * the observable — a re-decode necessarily allocates a new one and
         * retires this. */
        CHECK(dr32_kit_load_sample(&m, 0, wm) == DR32_WAV_OK, "memo: reload same");
        CHECK(m.pads[0].sample == first, "memo: same file re-decoded (buffer changed)");
        CHECK(m.pads[0].retired == NULL, "memo: same file retired a buffer — it took the reload path");

        /* Edited in place must still reload, or "reload the kit" becomes the
         * one gesture that cannot pick up an edited sample.
         * ⚠ st_mtime is SECONDS. A rewrite in the same second AND at the same
         * size is genuinely not detected — see the note in dr32_kit.c. The
         * test forces a distinct mtime rather than pretending otherwise;
         * writing a different LEVEL alone would not be enough.
         * → [[mutate-restore-same-mtime-second]] is this same hazard. */
        make_wav(wm, 0.25f);
        {
            struct stat st;
            CHECK(stat(wm, &st) == 0, "memo: stat after rewrite");
            struct timespec times[2];
            times[0].tv_sec = st.st_atime; times[0].tv_nsec = 0;
            times[1].tv_sec = st.st_mtime + 5; times[1].tv_nsec = 0;
            CHECK(utimensat(AT_FDCWD, wm, times, 0) == 0, "memo: bump mtime");
        }
        CHECK(dr32_kit_load_sample(&m, 0, wm) == DR32_WAV_OK, "memo: reload edited");
        CHECK(m.pads[0].sample != first, "memo: an EDITED file was not reloaded");

        /* A different path always reloads. */
        const float *second = m.pads[0].sample;
        CHECK(dr32_kit_load_sample(&m, 0, wa) == DR32_WAV_OK, "memo: load other path");
        CHECK(m.pads[0].sample != second, "memo: a DIFFERENT path was not reloaded");

        /* Clearing drops the stamp with the buffer, so the same path reloads
         * rather than matching a stamp that outlived what it described. */
        CHECK(dr32_kit_load_sample(&m, 0, "") == DR32_WAV_OK, "memo: clear pad");
        CHECK(m.pads[0].sample == NULL, "memo: clear left a buffer");
        CHECK(m.pads[0].src_size == 0 && m.pads[0].src_mtime == 0, "memo: clear left a stamp");
        CHECK(dr32_kit_load_sample(&m, 0, wa) == DR32_WAV_OK, "memo: reload after clear");
        CHECK(m.pads[0].sample != NULL, "memo: reload after clear produced no buffer");

        dr32_kit_free(&m);
        remove(wm);
    }

    dr32_kit_free(&k);
    remove(wa); remove(wb);

    /* ---- LINK: one turn sets that parameter on every pad ------------------
     *
     * ⭐ THE EXCLUSIONS ARE THE PART WORTH TESTING. Fanning out a value is easy
     * and obvious; the way this feature goes wrong is by fanning out the things
     * that make a pad a distinct pad. A test that only checked "attack reached
     * pad 7" would pass with `note` flattening the whole rack onto one MIDI
     * note, which takes a kit reload to undo. */
    {
        dr32_kit k; dr32_kit_init(&k);
        for (int i = 0; i < DR32_PADS; i++) k.pads[i].params.attack = 0.5f;

        /* OFF by default — a mode that armed itself would flatten a kit on the
         * next knob turn. */
        CHECK(k.link_all == 0, "link_all is not off at init");
        dr32_apply_param(&k, "pad4_attack", "0.25");
        CHECK(k.pads[3].params.attack == 0.25f, "the addressed pad did not take the value");
        CHECK(k.pads[7].params.attack == 0.5f, "link OFF still wrote to another pad");

        dr32_apply_param(&k, "link", "All");
        CHECK(k.link_all == 1, "link did not arm");
        dr32_apply_param(&k, "pad4_decay", "2.0");
        int all = 1;
        for (int i = 0; i < DR32_PADS; i++) if (k.pads[i].params.decay != 2.0f) all = 0;
        CHECK(all, "link ON did not reach every pad");

        /* The exclusions, each named because each has its own failure. */
        int base_note = k.pads[9].note;
        dr32_apply_param(&k, "pad4_note", "40");
        CHECK(k.pads[9].note == base_note,
              "LINK fanned out `note` — every pad would answer one MIDI note and the rack is dead");
        CHECK(k.pads[3].note == 40, "the addressed pad's note did not change");

        k.pads[5].params.sending_note = 61;
        dr32_apply_param(&k, "pad4_sending_note", "70");
        CHECK(k.pads[5].params.sending_note == 61, "LINK fanned out `sending_note`");

        /* `sample` is the destructive one: 32 pads on one sample is not a kit.
         * ⚠ A REAL file, deliberately. This check first used a nonexistent path
         * and was VACUOUS — the load failed for every pad, so it passed whether
         * sample fanned out or not. Mutation-testing is what exposed that. */
        {
            const char *wav = "/tmp/dr32_link_probe.wav";
            FILE *wf = fopen(wav, "wb");
            if (wf) {
                unsigned char hdr[] = {
                    'R','I','F','F', 44,0,0,0, 'W','A','V','E', 'f','m','t',' ',
                    16,0,0,0, 1,0, 1,0, 0x44,0xAC,0,0, 0x88,0x58,1,0, 2,0, 16,0,
                    'd','a','t','a', 8,0,0,0, 0,0,0,0,0,0,0,0 };
                fwrite(hdr, 1, sizeof hdr, wf);
                fclose(wf);
            }
            dr32_apply_param(&k, "pad4_sample", wav);
            CHECK(k.pads[3].path[0] != '\0',
                  "the probe WAV did not load at all — this check would be vacuous");
            CHECK(k.pads[11].path[0] == '\0',
                  "LINK fanned out `sample` — that puts one sample on all 32 pads");
            remove(wav);
        }

        /* ---- ONE-SHOT PER PARAMETER (Josh, 2026-09-09) --------------------
         * Arm, sweep one knob, reach for a different one: the second knob is
         * yours alone and the mode is gone. */
        {
            dr32_kit t; dr32_kit_init(&t);
            dr32_apply_param(&t, "link", "All");

            /* Sweeping the SAME field keeps linking — a knob emits many writes. */
            dr32_apply_param(&t, "pad4_transpose", "5");
            dr32_apply_param(&t, "pad4_transpose", "7");
            int all = 1;
            for (int i = 0; i < DR32_PADS; i++) if (t.pads[i].params.transpose != 7.0f) all = 0;
            CHECK(all, "a second write of the SAME field stopped linking mid-sweep");
            CHECK(t.link_all == 1, "link released while still on the same field");

            /* A DIFFERENT field releases, and is NOT itself linked. */
            dr32_apply_param(&t, "pad4_send_a", "-6");
            CHECK(t.link_all == 0, "link did not release on a different parameter");
            CHECK(t.pads[3].params.send_db[0] == -6.0f, "the releasing write did not reach its own pad");
            CHECK(t.pads[9].params.send_db[0] != -6.0f,
                  "the write that ENDED the mode was itself linked — reaching for another knob "
                  "is the signal you are done, not a last instruction");

            /* And it stays off until armed again. */
            dr32_apply_param(&t, "pad4_pan", "20");
            CHECK(t.pads[9].params.pan != 20.0f, "still linking after release");

            /* ui_* must not count as "a different parameter" — focus following a
             * hit while you sweep would otherwise disarm mid-gesture. */
            dr32_kit t2; dr32_kit_init(&t2);
            dr32_apply_param(&t2, "link", "All");
            dr32_apply_param(&t2, "pad4_decay", "3.0");
            dr32_apply_param(&t2, "ui_current_pad", "5");
            dr32_apply_param(&t2, "pad4_decay", "4.0");
            int still = 1;
            for (int i = 0; i < DR32_PADS; i++) if (t2.pads[i].params.decay != 4.0f) still = 0;
            CHECK(still, "a ui_ write released the mode mid-sweep");
            dr32_kit_free(&t2);
            dr32_kit_free(&t);
        }

        /* Disarming must actually disarm. */
        dr32_apply_param(&k, "link", "One");
        CHECK(k.link_all == 0, "link did not disarm");
        dr32_apply_param(&k, "pad4_hold", "0.9");
        CHECK(k.pads[7].params.hold != 0.9f, "link stayed armed after being turned off");

        dr32_kit_free(&k);
    }

    /* ---- BROWSE: the knob is echoed, the delta is what moves ---------------
     *
     * ⭐ THE PROPERTY IS THAT DR32 AND THE HOST NEVER DISAGREE. The host keeps
     * its own persistent knob value and carries it forward; whatever we hand
     * back on a read is where it starts from. So we hand back exactly what it
     * wrote. Report anything else — the folder index, say — and the two drift,
     * with the gap landing on the next detent as a jump. That reached the
     * device three times before the design was cut down to this.
     *
     * What moves the selection is the DELTA between successive writes, so the
     * knob's declared range never has to match the folder's size.
     */
    {
        system("rm -rf /tmp/dr32_br && mkdir -p /tmp/dr32_br");
        for (int i = 0; i < 3; i++) {
            char p2[128];
            snprintf(p2, sizeof p2, "/tmp/dr32_br/s%d.wav", i);
            make_wav(p2, 0.5f);
        }
        dr32_kit b; dr32_kit_init(&b);
        dr32_kit_load_sample(&b, 0, "/tmp/dr32_br/s0.wav");
        CHECK(dr32_kit_browse_count(&b, 0) == 3, "fixture folder is not 3 files");

        char v[32];
        dr32_read_param(&b, "pad1_browse", v, sizeof v);
        CHECK(atoi(v) == 0, "browse starts at %s, want 0 — the host seeds from this", v);

        dr32_apply_param(&b, "pad1_browse", "1");
        CHECK(dr32_kit_browse_index(&b, 0) == 1, "a +1 detent did not advance one file");
        dr32_apply_param(&b, "pad1_browse", "2");
        CHECK(dr32_kit_browse_index(&b, 0) == 2, "a second detent did not advance");

        /* Reads change nothing, however many land between detents. This is the
         * whole point, and it is what the device reports were about. */
        for (int r = 0; r < 5; r++) dr32_read_param(&b, "pad1_browse", v, sizeof v);
        CHECK(atoi(v) == 2, "read back %s, want the knob's own value 2", v);
        dr32_apply_param(&b, "pad1_browse", "3");
        CHECK(dr32_kit_browse_index(&b, 0) == 2, "past the end it must stop, not wrap");
        /* ⚠ HERE the echo and the index DISAGREE — the knob is at 3, the folder
         * stopped at 2 — so this is the only place a read can prove which one
         * is being reported. Asserting it earlier passed either way, which
         * mutation-testing is how I found out. */
        dr32_read_param(&b, "pad1_browse", v, sizeof v);
        CHECK(atoi(v) == 3,
              "read back %s after overshooting; the knob is at 3 and the index at 2, and "
              "reporting the index is exactly what made it jump", v);
        dr32_apply_param(&b, "pad1_browse", "2");
        CHECK(dr32_kit_browse_index(&b, 0) == 1,
              "after overshooting, one detent back landed on %d, want 1",
              dr32_kit_browse_index(&b, 0));

        CHECK(strstr(b.pads[0].path, "/tmp/dr32_br/") != NULL, "browse left the pad's folder");

        /* ⚠ EACH PAD HAS ITS OWN COUNTER — its knob is a separate key with its
         * own state in the host, so one shared counter made switching pad look
         * like an enormous turn. */
        system("mkdir -p /tmp/dr32_br2");
        for (int i = 0; i < 5; i++) {
            char p3[128];
            snprintf(p3, sizeof p3, "/tmp/dr32_br2/t%d.wav", i);
            make_wav(p3, 0.5f);
        }
        dr32_kit_load_sample(&b, 1, "/tmp/dr32_br2/t0.wav");
        dr32_read_param(&b, "pad2_browse", v, sizeof v);
        CHECK(atoi(v) == 0, "a second pad's browse starts at %s, want its own 0", v);
        dr32_apply_param(&b, "pad2_browse", "1");
        CHECK(dr32_kit_browse_index(&b, 1) == 1, "the second pad did not step");
        CHECK(dr32_kit_browse_index(&b, 0) == 1, "stepping one pad moved another");
        system("rm -rf /tmp/dr32_br2");

        dr32_kit_free(&b);
        system("rm -rf /tmp/dr32_br");
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
