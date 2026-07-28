// dr32_kit.h — the 32-pad kit: note map, choke groups, per-pad voice and sample
// ownership.
//
// Threading: set_param / sample loading run on the HOST thread; render runs on
// the AUDIO thread. The only shared mutable state is each pad's sample pointer,
// handled by the retire scheme documented in dr32_kit_load_sample().

#ifndef DR32_KIT_H
#define DR32_KIT_H

#include "dr32_voice.h"
#include "dr32_fxbus.h"
#include "wav.h"

#define DR32_PADS 32
#define DR32_FIRST_NOTE 36          // pad 0; pads run 36..67
#define DR32_MAX_PATH 512
#define DR32_KIT_MAX_BLOCK 1024

typedef struct {
    dr32_pad  params;
    dr32_voice voice;

    float  *sample;          // owned, interleaved, may be NULL (empty pad)
    size_t  frames;
    int     channels;        // 1 or 2
    int     sample_rate;     // source rate of the loaded sample
    float  *retired;         // previous buffer, freed on the NEXT load
    char    path[DR32_MAX_PATH];

    int     note;            // receivingNote from drumZoneSettings
    int     choke_group;     // 0 = none
} dr32_pad_slot;

typedef struct {
    dr32_pad_slot pads[DR32_PADS];
    signed char   note_to_pad[128];   // -1 = unmapped
    float         master_gain;        // linear
    unsigned      block;              // render-block counter (choke simultaneity)
    dr32_fxbus   *fx;                 // 2 sends + 2 kit inserts (may be NULL)
    // Per-pad render buffer, used only when a pad actually feeds a send.
    float         scratch[2 * DR32_KIT_MAX_BLOCK];
    // Slot params are cached so the UI can set one at a time (the bus API takes
    // them together). [p1,p2,p3,mix].
    // [size, damp, decay, predelay, mix]
    float         send_p[2][5];
    float         insert_p[2][5];
    // Mirrors of slot state the UI reads back (the bus itself is write-only).
    dr32_efx_type send_type[2];
    dr32_efx_type insert_type[2];
    float         send_return_ui[2];
    // Which pad the UI is editing, and whether playing a pad moves that focus.
    int           ui_current_pad;
    int           ui_auto_select_pad;

    // Live-press correlation. Neither side can move focus alone: the canvas
    // knows a press was PHYSICAL (it gets the raw grid note, which the
    // sequencer cannot produce) but not which pad, because a grid position is
    // not a pad — only the right 4x4 plays, and a tool may transpose it to
    // reach the upper 16. The note knows which pad but not whether a finger
    // sent it. Here the two meet: the canvas sets ui_live_press, and the pad
    // comes from note_to_pad.
    //
    // The two arrive in either order (the canvas is a separate process), so the
    // match looks both ways within a short window rather than assuming one.
    int           live_armed;      // canvas signalled a press, awaiting its note
    unsigned      live_arm_block;  // block it was signalled on
    int           last_hit_pad;    // pad of the most recent note-on, -1 = none
    unsigned      last_hit_block;  // block that note-on landed on
} dr32_kit;

// How far apart the press signal and its note may land and still be considered
// the same event. Blocks are 128 frames @ 44.1 kHz = ~2.9 ms, so 20 blocks is
// ~58 ms — comfortably above canvas->DSP IPC latency, and short enough that an
// unrelated sequenced note is very unlikely to fall inside it. Even when one
// does, the cost is focusing a pad that genuinely just played.
#define DR32_LIVE_MATCH_BLOCKS 20u

void dr32_kit_init(dr32_kit *k);
void dr32_kit_free(dr32_kit *k);

/** Assign a pad's receiving note, rebuilding the note map. A note may map to
 *  only one pad; the later assignment wins (matches a rack with duplicates). */
void dr32_kit_set_note(dr32_kit *k, int pad, int note);

/** Load `path` into `pad`. Host thread only — does file I/O and allocates.
 *  Returns a dr32_wav_err. Passing NULL/"" clears the pad. */
int dr32_kit_load_sample(dr32_kit *k, int pad, const char *path);

void dr32_kit_note_on(dr32_kit *k, int note, int velocity);
void dr32_kit_note_off(dr32_kit *k, int note);

/** Silence everything immediately (kit change, panic). */
void dr32_kit_all_off(dr32_kit *k);

/** Render `frames` of interleaved stereo. Overwrites `out` (does not add). */
void dr32_kit_render(dr32_kit *k, float *out, int frames);

/** Number of currently sounding voices — for the CPU/debug readout. */
int dr32_kit_active_voices(const dr32_kit *k);

#endif
