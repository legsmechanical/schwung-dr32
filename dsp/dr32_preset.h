// dr32_preset.h — load a Move `.ablpreset` drum kit straight into the engine.
//
// This is the C counterpart of lib/ablpreset.mjs. It exists because kit loading
// cannot live in ui.js: that is the module's STANDALONE UI, and in a Schwung
// chain slot the Shadow UI drives the module through params while the module's
// own tick() never runs. Anything the engine needs at load time must be done
// here.
//
// Host thread only — reads files and allocates.

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

/** Load `path` into `kit`, replacing every pad. Returns 1 on success. */
int dr32_preset_load(dr32_kit *kit, const char *path, dr32_preset_report *rep);

/** Resolve an `ableton:` sample URI to an absolute path (percent-decoded).
 *  Returns 0 for an unknown root. */
int dr32_resolve_uri(const char *uri, char *out, int out_len);

#endif
