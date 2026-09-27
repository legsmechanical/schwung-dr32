// dr32_preset.h — load a Move `.ablpreset` drum kit straight into the engine.
//
// This is the C counterpart of lib/ablpreset.mjs. It exists because kit loading
// cannot live in ui.js: that is the module's STANDALONE UI, and in a Schwung
// chain slot the Shadow UI drives the module through params while the module's
// own tick() never runs. Anything the engine needs at load time must be done
// here.
//
// dr32_preset_load and dr32_preset_prepare read files and allocate; apply does not.

#ifndef DR32_PRESET_H
#define DR32_PRESET_H

#include "dr32_kit.h"

typedef struct {
    int pads;          // pad chains seen
    int loaded;        // samples loaded
    int empty;         // pads with no sampleUri (normal — 112 in the corpus)
    int unresolved;    // sampleUri with an unknown root
    int failed;        // sample file present but unreadable
} dr32_preset_report;

/** Load `path` into `kit`, replacing every pad. Returns 1 on success.
 *  Synchronous: prepare + apply below, back to back, on the calling thread. */
int dr32_preset_load(dr32_kit *kit, const char *path, dr32_preset_report *rep);

/* ---- The same load in two halves, so the slow one can run elsewhere ----
 *
 * prepare: read and parse the preset, decode its WAVs. Touches NO kit — only
 *          the `held` snapshot — so it runs on any thread. Slow.
 * apply:   write the plan into the kit and adopt its buffers. No file is read.
 *          On the thread that owns the kit (the audio thread), between blocks.
 */
typedef struct dr32_kit_plan dr32_kit_plan;

/** What each pad holds now, for the decode memo: a pad already holding the
 *  same file (same size+mtime) keeps its buffer instead of decoding again. */
typedef struct {
    int  has;
    char path[DR32_MAX_PATH];
    long size, mtime;
} dr32_pad_stamp;

/** Snapshot `kit`'s pads into `held[DR32_PADS]`. Copies only; no I/O. */
void dr32_preset_stamps(const dr32_kit *kit, dr32_pad_stamp *held);

/** NULL if the file is not a readable drum rack, or if `stop` (checked between
 *  WAVs, may be NULL) says the result is no longer wanted. */
dr32_kit_plan *dr32_preset_prepare(const char *path, const dr32_pad_stamp *held,
                                   int (*stop)(void *), void *ctx);

/** Replace every pad of `kit` with the plan. The plan keeps no buffer the kit
 *  took; free it afterwards with dr32_kit_plan_free. */
void dr32_preset_apply(dr32_kit *kit, dr32_kit_plan *plan, dr32_preset_report *rep);

void dr32_kit_plan_free(dr32_kit_plan *plan);

/** Resolve an `ableton:` sample URI to an absolute path (percent-decoded).
 *  Returns 0 for an unknown root. */
int dr32_resolve_uri(const char *uri, char *out, int out_len);

#endif
