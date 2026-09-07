// dr32_kit.h — the 32-pad kit: note map, choke groups, per-pad voice and sample
// ownership.
//
// Threading: set_param / sample loading run on the HOST thread; render runs on
// the AUDIO thread. The only shared mutable state is each pad's sample pointer,
// handled by the retire scheme documented in dr32_kit_load_sample().

#ifndef DR32_KIT_H
#define DR32_KIT_H

#include "dr32_voice.h"
#include "dr32_fxbus.h"   /* dr32_efx_type, for the retired-field readers */
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
    float         bpm;                // last tempo seen
    // ── RETIRED: the two internal send buses and the always-on Drum Bus ──
    //
    // Both are the host's now: the Drum Bus is a declared voice bus of four
    // `dr32-fx` inserts (capabilities.default_buses), and the pads' send1 /
    // send2 feed the host's two GLOBAL sends through voice_send_params. The
    // effects themselves did not go anywhere -- `dr32-fx` hosts all eight send
    // types as presets.
    //
    // These fields stay ONLY as a place to park the values 137 factory kits and
    // every saved state still carry. A DSP cannot migrate them: it does not
    // know what is in the host's buses, and inventing inserts on load would
    // overwrite whatever the user had put there. So they are accepted, stored,
    // read back, and NOT connected to any audio -- which is a knob that does
    // nothing, and is why they are also off every page in module.json. Dropping
    // them instead would make a saved slot fail to restore rather than restore
    // quietly.
    float         send_p[2][DR32_SEND_PARAMS];
    float         bus_p[5];
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

// ---------------------------------------------------------------- split render
//
// The three calls below are the same block of audio as dr32_kit_render, taken
// apart: one buffer per pad, plus one for everything that belongs to no pad.
// They exist for Schwung's per-voice render contract (move_plugin_render_split
// in dsp/dr32.c), which hands the module one int16 destination per voice and
// may hand the SAME destination to several of them.
//
// Nothing here writes to a shared destination — each call works on a
// caller-owned float scratch buffer that the caller then converts and
// accumulates. That is what keeps the aliasing rule the plugin layer's problem
// rather than the kit's.
//
// Per block, in this order: begin_block, then render_voice for EVERY pad, then
// dr32_kit_finish_main. The order is load-bearing twice over:
//
//   - it used to be, when the sends and the Drum Bus lived in here. They are
//     the host's now, so dr32_kit_finish_main has nothing left to do and is
//     kept as a no-op: the ORDER is no longer load-bearing, but the call is
//     still in every host that drives the split path and removing it would be
//     an ABI break for no gain.

/** One block boundary (the choke-simultaneity counter). Call once per block,
 *  before any dr32_kit_render_voice. dr32_kit_render does this itself. */
void dr32_kit_begin_block(dr32_kit *k);

/** Render pad `pad` alone into `dst` (2 * frames interleaved floats) and feed
 *  its post-fader share to whichever send buses it is on.
 *
 *  `dst` is OVERWRITTEN, which is safe only because it is the caller's private
 *  scratch — never one of the split contract's shared destinations.
 *
 *  Returns 1 if the pad produced audio, 0 if it is silent, in which case `dst`
 *  is left untouched and the caller must not accumulate it. */
int dr32_kit_render_voice(dr32_kit *k, int pad, float *dst, int frames);

/** Finish the kit's MAIN output, IN PLACE: add the two send returns to `mix`
 *  and then run the always-on Drum Bus over the result.
 *
 *  `mix` is NOT cleared. On entry it must already hold every pad that has no
 *  destination of its own — the unassigned voices — at the same post-master-
 *  gain level dr32_kit_render_voice produced them at. So:
 *
 *      A VOICE YOU ROUTE TO A BUS LEAVES THE KIT'S DRUM BUS.
 *
 *  Exactly as routing a channel to a subgroup takes it out of the main mix on
 *  a desk. What is left on main — the unrouted pads plus the returns — is
 *  glued as it always was; the routed pads never meet the glue, because you
 *  routed them elsewhere. Glue over everything including the buses is the
 *  chain host's job one tier up: it sums buses into main BEFORE the slot's own
 *  FX, "so a slot compressor sees the whole kit".
 *
 *  Call it once per block whether or not anyone wants the audio: this is what
 *  drains the send buses, and skipping it replays a block's sends on top of
 *  the next.
 *
 *  The returns are added at master gain, and the glue is run in the PRE-gain
 *  domain (the buffer is scaled down, processed and scaled back) so the
 *  compressor sees the same level dr32_kit_render's does. That keeps the two
 *  entry points' only difference the one sentence above. */
void dr32_kit_finish_main(dr32_kit *k, float *mix, int frames);

/** How long a voice label may be, in bytes, before it is truncated.
 *
 *  32 entries have to fit the 4096-byte buffer the chain host parses the id
 *  table out of, and a truncated read is silently short — so this is a budget,
 *  not a style choice. See the static assert in dr32_split_voices_json. */
#define DR32_SPLIT_LABEL_MAX 24

/** Publish the flat ordered voice list Schwung's bus routing is built on:
 *
 *      [{"id":"pad1","label":"Kick 707"}, ...]      32 entries, always
 *
 *  ENTRY i IS BUFFER i in move_plugin_render_split, so the order is the pad
 *  order and never changes. The ids are POSITIONAL for the same reason: a kit
 *  change swaps every pad's sample but no pad's position, and an id keyed to a
 *  sample name would orphan every bus assignment on the next kit. A bus on a
 *  drum rack means "the third pad".
 *
 *  ⚠ These ids are 1-BASED (`pad1`..`pad32`) while dr32_params' keys are
 *  0-based (`pad0_attack`). They are not the same namespace and never meet —
 *  the host treats an id as opaque — and 1-based matches what the user is shown
 *  ("Pad 1"). Do not "fix" one to match the other.
 *
 *  Returns bytes written, or 0 (and an empty buffer) if it would not fit. */
int dr32_split_voices_json(const dr32_kit *k, char *buf, int buf_len);

/** Number of currently sounding voices — for the CPU/debug readout. */
int dr32_kit_active_voices(const dr32_kit *k);

#endif
