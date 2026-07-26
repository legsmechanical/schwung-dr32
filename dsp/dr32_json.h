// dr32_json.h — a small read-only JSON parser.
//
// Why this exists: kit loading CANNOT live in the module's ui.js. That file is
// the standalone module UI; when DR32 runs as a chainable sound_generator in a
// Schwung slot, the Shadow UI drives it through params and the module's own
// tick() never runs. So the DSP has to parse .ablpreset itself.
//
// Scope is deliberately minimal — enough to walk a device preset, no more.
// Parsing happens on the HOST thread at kit-load time, never on the audio
// thread.

#ifndef DR32_JSON_H
#define DR32_JSON_H

#include <stddef.h>

typedef enum {
    DR32_JSON_NULL = 0,
    DR32_JSON_BOOL,
    DR32_JSON_NUMBER,
    DR32_JSON_STRING,
    DR32_JSON_ARRAY,
    DR32_JSON_OBJECT,
} dr32_json_type;

typedef struct dr32_json dr32_json;

struct dr32_json {
    dr32_json_type type;
    /* object/array children, singly linked */
    dr32_json *first;
    dr32_json *next;
    char      *key;      /* object members only */
    char      *str;      /* strings (unescaped) */
    double     num;
    int        boolean;
};

/** Parse `text` into a tree. Returns NULL on malformed input.
 *  Free with dr32_json_free(). */
dr32_json *dr32_json_parse(const char *text);
void dr32_json_free(dr32_json *v);

/** Object member lookup; NULL if absent or not an object. */
const dr32_json *dr32_json_get(const dr32_json *obj, const char *key);
/** Array element; NULL if out of range or not an array. */
const dr32_json *dr32_json_at(const dr32_json *arr, int index);
int dr32_json_count(const dr32_json *v);

/* Typed accessors with defaults, so callers stay branch-light. */
double      dr32_json_num(const dr32_json *v, const char *key, double fallback);
const char *dr32_json_str(const dr32_json *v, const char *key, const char *fallback);
int         dr32_json_bool(const dr32_json *v, const char *key, int fallback);

/** Depth-first search for the first object whose "kind" member equals `kind`. */
const dr32_json *dr32_json_find_kind(const dr32_json *v, const char *kind);

#endif
