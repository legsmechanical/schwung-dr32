// dr32_plugin_validate.h — the checks a plugin must pass (dr32_engine_api.h).
// Shared by DR32's loader and tools/plugin_check.c.

#ifndef DR32_PLUGIN_VALIDATE_H
#define DR32_PLUGIN_VALIDATE_H

#include "dr32_engine_api.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** NULL if the plugin keeps every rule; otherwise the reason, in a static
 *  buffer (not thread-safe: one caller at a time). */
const char *dr32_plugin_validate(const dr32x_plugin *pl);

/** Option `idx` of "A|B|C" into `out`; 0 if there is none or it does not fit. */
int dr32x_option_at(const char *opts, int idx, char *out, size_t cap);
int dr32x_option_count(const char *opts);
/** Is `f` a whole number? What makes a knob an int rather than a float. */
int dr32x_whole(float f);

#ifdef __cplusplus
}
#endif

#endif
