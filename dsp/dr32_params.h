// dr32_params.h — flat string param -> kit mapping, shared by the plugin and
// the offline null-test renderer. See dr32_params.c.

#ifndef DR32_PARAMS_H
#define DR32_PARAMS_H

#include "dr32_kit.h"

/** Apply one `key = val` to the kit. Keys are either `pad<N>_<field>` or a
 *  global (`master`, `panic`, `clear`). Returns 1 if the key was recognised.
 *
 *  The `pad<N>_<field>` shape is not arbitrary: the Shadow UI's child-selector
 *  (`child_prefix: "pad"`, `child_count: 32`) generates exactly these keys, so
 *  the pad browser drives the engine with no translation layer. */
int dr32_apply_param(dr32_kit *kit, const char *key, const char *val);

/** Read one param back as text. The Shadow UI calls this to DISPLAY values —
 *  without it every knob reads zero and editing appears to do nothing.
 *  Returns the number of bytes written, or 0 if the key is unknown. */
int dr32_read_param(const dr32_kit *kit, const char *key, char *buf, int buf_len);

#endif
