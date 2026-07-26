// dr32_kit.h — the 32-pad kit: note map, choke groups, per-pad voice and sample
// ownership.
//
// Threading: set_param / sample loading run on the HOST thread; render runs on
// the AUDIO thread. The only shared mutable state is each pad's sample pointer,
// handled by the retire scheme documented in dr32_kit_load_sample().

#ifndef DR32_KIT_H
#define DR32_KIT_H

#include "dr32_voice.h"
#include "wav.h"

#define DR32_PADS 32
#define DR32_FIRST_NOTE 36          // pad 0; pads run 36..67
#define DR32_MAX_PATH 512

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
    // Set while the Shadow UI's sample browser is open (browser_hooks on_open /
    // on_cancel / on_commit). While it is on, assigning a pad's sample also
    // auditions that pad, which is what makes live_preview audible: the browser
    // sets pad<N>_sample as the cursor moves, and the host restores the old
    // value if the user backs out.
    int           preview_mode;
} dr32_kit;

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
