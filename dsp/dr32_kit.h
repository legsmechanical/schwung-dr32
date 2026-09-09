// dr32_kit.h — the 32-pad kit: note map, choke groups, per-pad voice and sample
// ownership.
//
// Threading: set_param / sample loading run on the HOST thread; render runs on
// the AUDIO thread. The only shared mutable state is each pad's sample pointer,
// handled by the retire scheme documented in dr32_kit_load_sample().

#ifndef DR32_KIT_H
#define DR32_KIT_H

#include <stdint.h>
#include "dr32_voice.h"
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
    /* On-disk identity of what `sample` holds, so a reload of the SAME file can
     * skip the decode. Zero when nothing is loaded. See dr32_kit_load_sample:
     * a kit recall re-reads all 16 pads, and the decode was ~3.5 ms of it on
     * the SPI callback. Size+mtime rather than path alone, so editing a sample
     * in place still reloads it. */
    long    src_size;
    long    src_mtime;

    int     note;            // receivingNote from drumZoneSettings
    int     choke_group;     // 0 = none
} dr32_pad_slot;

typedef struct {
    dr32_pad_slot pads[DR32_PADS];
    signed char   note_to_pad[128];   // -1 = unmapped
    float         master_gain;        // linear
    unsigned      block;              // render-block counter (choke simultaneity)
    /* Per-pad render buffer. Used only by the split render, for a pad that is
     * routed out to a host bus: that pad has to be converted to int16 and
     * ACCUMULATED into a buffer it may share with another pad, so it cannot
     * render straight into the destination. Per-instance, never a static —
     * dr32 is multi-instance and two slots would share one buffer on the audio
     * thread. */
    float         scratch[2 * DR32_KIT_MAX_BLOCK];
    /* The KIT MIX under a per-voice render: every pad NOT routed out to a host
     * bus, summed. Same buffer role `out` has in dr32_kit_render. */
    float         split_dry[2 * DR32_KIT_MAX_BLOCK];
    // Which pad the UI is editing, and whether playing a pad moves that focus.
    int           ui_current_pad;
    int           ui_auto_select_pad;
    /* LINK: while set, a per-pad write is applied to EVERY pad. Editor state,
     * not sound — deliberately NOT persisted, so a reload never comes back with
     * it silently on and the next knob turn flattening the kit.
     *
     * ⭑ It is ONE-SHOT PER PARAMETER (Josh, 2026-09-09). Arming it does not
     * link everything from then on: the FIRST fan-out-eligible field written
     * after arming is latched into `link_sub`, and the moment a DIFFERENT field
     * is written the mode releases and that write lands on the focused pad
     * alone. So "link, sweep transpose, then reach for Send A" does the obvious
     * thing without a second gesture to turn it off — and a mode that cannot be
     * left on by accident is a mode that cannot flatten a kit by accident. */
    int           link_all;
    char          link_sub[32];   /* "" = armed, nothing latched yet */

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
    /* The last value the HOST wrote to `browse`, so a turn can be read as a
     * DELTA. See dr32_kit_browse_step. */
    int           browse_wire;
    int           browse_wire_seen;            // 0 until the host has written once
    char        **browse;                      // browse_n entries, owned
    int           browse_n;
} dr32_kit;

// A single folder's worth. Move's factory sample folders are far below this;
// the cap only stops a pathological directory from allocating without bound.
/* ⚠⚠ NOT paired with `browse`'s declared range, and pairing them was a bug.
 *
 * They were briefly tied together (both 256) when browse used the knob's value
 * ABSOLUTELY, because then the knob had to be able to address every entry. Once
 * browse became a DELTA stepper the knob's range stopped mattering at all — it
 * only sets the per-detent step — while this cap still has to cover real
 * folders. It did not: the user library's "Preset Samples" holds 1339 files, so
 * a 256 cap truncated the listing in arbitrary readdir order, the pad's own
 * sample was usually NOT in what survived, browse_index returned -1, and every
 * detent restarted from zero. Reported from the device as the knob "jumping
 * around".
 *
 * So this covers the largest folder anyone plausibly has; the knob's range is a
 * separate decision and stays small. */
#define DR32_BROWSE_MAX 2048

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

/** The index for DISPLAY, clamped to >= 0.
 *
 * ⚠⚠ IT MUST NOT TOUCH THE DELTA BASELINE. Making it do so caused the jumping
 * it was meant to cure: the host does not adopt a readback into its knob, it
 * keeps a persistent knobStates[key] seeded once and stepped per detent, and
 * reads only feed the display. A baseline resynced to the index therefore makes
 * every delta `hostValue - index`. Only a WRITE may move the baseline — it is
 * the one event that reports what the host actually holds. */
int dr32_kit_browse_index_sync(dr32_kit *k, int pad);
/** Load the idx'th neighbour into the pad. Clamps. Returns the index used. */
int dr32_kit_browse_select(dr32_kit *k, int pad, int idx);

/** Move by the DELTA implied by the knob's new absolute value.
 *
 * ⚠⚠ WHY NOT JUST USE THE VALUE. `browse` is declared int 0..255 because a knob
 * needs a static range, but a folder has however many files it has — usually a
 * handful. Used absolutely, a 3-file folder left 253 of the knob's positions
 * doing nothing: you turn right, nothing happens, and you have to wind all the
 * way back before it responds. Reported from the device as browse "not being
 * bounded to the folder".
 *
 * The clamped index IS read back, but that does not rescue it while you turn:
 * the host sets a settle window after every knob write and SKIPS reads inside
 * it, so it does not adopt the clamp until you stop.
 *
 * So the value is read as a delta from whatever the host last wrote, and the
 * step is clamped to the folder. The knob's own range then stops mattering, and
 * reporting the true index keeps the two converging whenever the host does
 * read. */
int dr32_kit_browse_step(dr32_kit *k, int pad, int wire);

void dr32_kit_note_on(dr32_kit *k, int note, int velocity);
void dr32_kit_note_off(dr32_kit *k, int note);

/** Silence everything immediately (kit change, panic). */
void dr32_kit_all_off(dr32_kit *k);

/** Render `frames` of interleaved stereo. Overwrites `out` (does not add). */
void dr32_kit_render(dr32_kit *k, float *out, int frames);

/*
 * The PER-VOICE render: each pad into its own destination, the kit's own FX
 * return into main.
 *
 * ⭐ THE HOST OWNS THE BUFFERS AND THEY ALIAS. Two pads the user put on the
 * same host bus are handed the SAME pointer, so every write here ACCUMULATES
 * and nothing is ever cleared — the host clears the distinct destinations
 * before the call and a memset here would erase another pad's audio.
 *
 * int16 destinations, because that is the host's audio type; the kit is float
 * internally, so each pad is rendered into the float scratch and converted on
 * the way out. `main_out` carries what belongs to NO pad — the send-FX return
 * and the drum-bus stage.
 *
 * ⚠ STATE-COMPATIBLE WITH dr32_kit_render BY CONSTRUCTION: same voices, same
 * loop, same choke/block counter. The host switches between the two AT RUNTIME,
 * PER FRAME, by whether any voice is on a bus — so they must not diverge.
 */
void dr32_kit_render_split(dr32_kit *k, int16_t *const *voice_out, int n_voices,
                           int16_t *main_out, int frames);

/** Number of currently sounding voices — for the CPU/debug readout. */
int dr32_kit_active_voices(const dr32_kit *k);

#endif
