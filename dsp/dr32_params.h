// dr32_params.h — flat string param -> kit mapping, shared by the plugin and
// the offline null-test renderer. See dr32_params.c.

#ifndef DR32_PARAMS_H
#define DR32_PARAMS_H

#include "dr32_kit.h"

/** Apply one `key = val` to the kit. Keys are either `pad<N>_<field>` or a
 *  global (`master`, `panic`, `clear`). Returns 1 if the key was recognised. */
int dr32_apply_param(dr32_kit *kit, const char *key, const char *val);

#endif
