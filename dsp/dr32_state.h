// dr32_state.h — the `state` blob Schwung uses to persist a chain slot.
// See dr32_state.c for the shape and the reasoning.

#ifndef DR32_STATE_H
#define DR32_STATE_H

#include "dr32_kit.h"

/** Serialize kit + kit path into buf. Returns bytes written, 0 on failure
 *  (including truncation — a half blob is worse than none). */
int dr32_state_write(const dr32_kit *kit, const char *kit_path,
                     char *buf, int buf_len);

/** Restore a blob. `load_kit` is called for the stored preset path BEFORE any
 *  params are applied, since loading a kit replaces every pad. Returns 1 if the
 *  blob parsed. */
int dr32_state_read(dr32_kit *kit, const char *json,
                    void (*load_kit)(void *ctx, const char *path), void *ctx);

#endif
