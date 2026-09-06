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
    dr32_fxbus   *fx;                 // 2 sends (may be NULL)
    // Per-pad render buffer, used only when a pad actually feeds a send.
    float         scratch[2 * DR32_KIT_MAX_BLOCK];
    // Send params are cached so the UI can set one at a time (the bus API takes
    // them together).
    // See dr32_fxbus.h for the per-type slot table.
    // ⚠ Not everything here is normalised: the Delay's synced times are a count
    // of SIXTEENTHS (1..16) and its free times are MILLISECONDS. Both pairs are
    // stored at once and survive a flip of the sync flag, as they do on the
    // native device.
    float         send_p[2][DR32_SEND_PARAMS];
    // The always-on Drum Bus: [compress, crunch, attack, sustain, mix].
    // Attack and Sustain are BIPOLAR -1..+1 with neutral at 0 (the 0..1-about-
    // 0.5 form lives inside DrumBuss and nowhere else). Mix is the parallel
    // blend and defaults to 1 = fully processed, so it only ever takes the
    // stage away.
    float         bus_p[5];
    float         bpm;                // last tempo seen, for the synced Delay
    // Mirrors of slot state the UI reads back (the bus itself is write-only).
    dr32_efx_type send_type[2];
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

    // Is a transport running? Mirrored from the host every render block
    // (host_api get_beat_position / get_clock_status; dr32.c). While it is NOT,
    // every note-on is a hand on a pad or a key — there is nothing else that
    // could produce one — so focus follows the note without a vouch. While it
    // IS, a live hit and a sequenced one are indistinguishable here and only a
    // vouch (ui_live_press) or a host that names the note (ui_live_note) moves
    // focus. Upstream Schwung's own rule for the same reason: "a sequencer
    // plays notes", so following every note dragged the editor around the bar.
    int           transport_running;
    // A host has VOUCHED at least once (ui_live_press or ui_live_note), so it
    // owns liveness from here on and a bare note-on never moves focus again,
    // whatever the transport says. Needed because a host that vouches is a
    // host with its own sequencer -- dAVEBOx -- and such a host may not report
    // its transport to us at all: DR32 saw "stopped", followed every sequenced
    // note, and the sequencer dragged the editor around the bar. Under a host
    // that never vouches (stock without the live-press contract) the stopped
    // regime stays, which is the only follow such a host can offer.
    int           host_vouches;

    // Folder browse: the loadable samples sitting NEXT TO the focused pad's
    // sample, so a knob can walk them without opening the file browser. Only
    // the host does file I/O, and only one directory is ever held — the user
    // Samples tree is ~3.8 GB, so nothing scans it wholesale.
    char          browse_dir[DR32_MAX_PATH];   // "" = nothing cached
    char        **browse;                      // browse_n entries, owned
    int           browse_n;
} dr32_kit;

// A single folder's worth. Move's factory sample folders are far below this;
// the cap only stops a pathological directory from allocating without bound.
#define DR32_BROWSE_MAX 512

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

/** Folder browse. All three are HOST-THREAD ONLY — they read the filesystem,
 *  and _select() loads a sample. They operate on the directory the pad's
 *  current sample lives in; an empty pad has no directory and yields 0 / -1.
 *  The listing is cached and only re-read when that directory changes. */
int dr32_kit_browse_count(dr32_kit *k, int pad);
/** Position of the pad's current sample among its neighbours, -1 if unknown. */
int dr32_kit_browse_index(dr32_kit *k, int pad);
/** Load the idx'th neighbour into the pad. Clamps. Returns the index used. */
int dr32_kit_browse_select(dr32_kit *k, int pad, int idx);

void dr32_kit_note_on(dr32_kit *k, int note, int velocity);
void dr32_kit_note_off(dr32_kit *k, int note);

/** Silence everything immediately (kit change, panic). */
void dr32_kit_all_off(dr32_kit *k);

/** Host tempo, for the synced Delay send. Safe to call every block. */
void dr32_kit_set_bpm(dr32_kit *k, float bpm);

/** Render `frames` of interleaved stereo. Overwrites `out` (does not add). */
void dr32_kit_render(dr32_kit *k, float *out, int frames);

/** Number of currently sounding voices — for the CPU/debug readout. */
int dr32_kit_active_voices(const dr32_kit *k);

#endif
