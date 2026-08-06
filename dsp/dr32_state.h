// dr32_state.h — the `state` blob Schwung uses to persist a chain slot.
// See dr32_state.c for the shape and the reasoning.

#ifndef DR32_STATE_H
#define DR32_STATE_H

#include "dr32_kit.h"

/** Serialize kit + kit path into buf. Returns bytes written, 0 on failure
 *  (including truncation — a half blob is worse than none).
 *
 *  `baseline_json` (may be NULL) is a blob previously written by this function
 *  right after a kit load. When given, params whose live value MATCHES the
 *  baseline are omitted: the kit path in the blob reproduces them on restore,
 *  so only the user's edits need carrying. This keeps a typical blob to a few
 *  hundred bytes instead of ~11 KB — which matters beyond tidiness: the host
 *  chain that persists this blob caps the state size it will read back, and a
 *  full dump PRETTY-PRINTED into the set file has already crossed a host's cap
 *  in the field, silently restoring the kit at defaults (2026-08-06). NULL
 *  falls back to the full dump. */
int dr32_state_write(const dr32_kit *kit, const char *kit_path,
                     char *buf, int buf_len, const char *baseline_json);

/** Restore a blob. `load_kit` is called for the stored preset path BEFORE any
 *  params are applied, since loading a kit replaces every pad. Returns 1 if the
 *  blob parsed. */
int dr32_state_read(dr32_kit *kit, const char *json,
                    void (*load_kit)(void *ctx, const char *path), void *ctx);

#endif
