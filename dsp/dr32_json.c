#include "dr32_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    const char *p;
    int failed;
} jctx;

static dr32_json *parse_value(jctx *c);

static void skip_ws(jctx *c) {
    while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r') c->p++;
}

static dr32_json *node(dr32_json_type t) {
    dr32_json *v = (dr32_json *)calloc(1, sizeof(dr32_json));
    if (v) v->type = t;
    return v;
}

/** Parse a JSON string literal, resolving escapes. Returns malloc'd UTF-8. */
static char *parse_string_raw(jctx *c) {
    if (*c->p != '"') { c->failed = 1; return NULL; }
    c->p++;
    /* worst case: output no longer than input */
    const char *start = c->p;
    size_t cap = 0;
    for (const char *q = start; *q && *q != '"'; q++) {
        if (*q == '\\' && q[1]) q++;
        cap++;
    }
    char *out = (char *)malloc(cap + 1);
    if (!out) { c->failed = 1; return NULL; }

    size_t n = 0;
    while (*c->p && *c->p != '"') {
        if (*c->p == '\\') {
            c->p++;
            switch (*c->p) {
                case 'n': out[n++] = '\n'; c->p++; break;
                case 't': out[n++] = '\t'; c->p++; break;
                case 'r': out[n++] = '\r'; c->p++; break;
                case 'b': out[n++] = '\b'; c->p++; break;
                case 'f': out[n++] = '\f'; c->p++; break;
                case 'u': {
                    /* \uXXXX -> UTF-8. Surrogate pairs are not expected in
                     * device presets; a lone surrogate is emitted as-is. */
                    unsigned code = 0;
                    c->p++;
                    for (int i = 0; i < 4 && *c->p; i++) {
                        char h = *c->p++;
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                    }
                    if (code < 0x80) {
                        out[n++] = (char)code;
                    } else if (code < 0x800) {
                        out[n++] = (char)(0xC0 | (code >> 6));
                        out[n++] = (char)(0x80 | (code & 0x3F));
                    } else {
                        out[n++] = (char)(0xE0 | (code >> 12));
                        out[n++] = (char)(0x80 | ((code >> 6) & 0x3F));
                        out[n++] = (char)(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: out[n++] = *c->p ? *c->p++ : '\0'; break;
            }
        } else {
            out[n++] = *c->p++;
        }
    }
    if (*c->p != '"') { free(out); c->failed = 1; return NULL; }
    c->p++;
    out[n] = '\0';
    return out;
}

static dr32_json *parse_object(jctx *c) {
    dr32_json *obj = node(DR32_JSON_OBJECT);
    if (!obj) { c->failed = 1; return NULL; }
    c->p++;                              /* '{' */
    skip_ws(c);
    if (*c->p == '}') { c->p++; return obj; }

    dr32_json *tail = NULL;
    for (;;) {
        skip_ws(c);
        char *key = parse_string_raw(c);
        if (c->failed) { dr32_json_free(obj); return NULL; }
        skip_ws(c);
        if (*c->p != ':') { free(key); c->failed = 1; dr32_json_free(obj); return NULL; }
        c->p++;
        dr32_json *val = parse_value(c);
        if (c->failed || !val) { free(key); dr32_json_free(obj); return NULL; }
        val->key = key;
        if (tail) tail->next = val; else obj->first = val;
        tail = val;

        skip_ws(c);
        if (*c->p == ',') { c->p++; continue; }
        if (*c->p == '}') { c->p++; return obj; }
        c->failed = 1;
        dr32_json_free(obj);
        return NULL;
    }
}

static dr32_json *parse_array(jctx *c) {
    dr32_json *arr = node(DR32_JSON_ARRAY);
    if (!arr) { c->failed = 1; return NULL; }
    c->p++;                              /* '[' */
    skip_ws(c);
    if (*c->p == ']') { c->p++; return arr; }

    dr32_json *tail = NULL;
    for (;;) {
        dr32_json *val = parse_value(c);
        if (c->failed || !val) { dr32_json_free(arr); return NULL; }
        if (tail) tail->next = val; else arr->first = val;
        tail = val;

        skip_ws(c);
        if (*c->p == ',') { c->p++; continue; }
        if (*c->p == ']') { c->p++; return arr; }
        c->failed = 1;
        dr32_json_free(arr);
        return NULL;
    }
}

static dr32_json *parse_value(jctx *c) {
    skip_ws(c);
    switch (*c->p) {
        case '{': return parse_object(c);
        case '[': return parse_array(c);
        case '"': {
            dr32_json *v = node(DR32_JSON_STRING);
            if (!v) { c->failed = 1; return NULL; }
            v->str = parse_string_raw(c);
            if (c->failed) { dr32_json_free(v); return NULL; }
            return v;
        }
        case 't':
            if (!strncmp(c->p, "true", 4)) {
                c->p += 4;
                dr32_json *v = node(DR32_JSON_BOOL);
                if (v) v->boolean = 1;
                return v;
            }
            break;
        case 'f':
            if (!strncmp(c->p, "false", 5)) {
                c->p += 5;
                return node(DR32_JSON_BOOL);
            }
            break;
        case 'n':
            if (!strncmp(c->p, "null", 4)) {
                c->p += 4;
                return node(DR32_JSON_NULL);
            }
            break;
        default: break;
    }
    /* number */
    {
        char *end = NULL;
        double d = strtod(c->p, &end);
        if (end == c->p) { c->failed = 1; return NULL; }
        c->p = end;
        dr32_json *v = node(DR32_JSON_NUMBER);
        if (v) v->num = d;
        return v;
    }
}

dr32_json *dr32_json_parse(const char *text) {
    if (!text) return NULL;
    jctx c = { text, 0 };
    dr32_json *v = parse_value(&c);
    if (c.failed) { dr32_json_free(v); return NULL; }
    return v;
}

void dr32_json_free(dr32_json *v) {
    while (v) {
        dr32_json *next = v->next;
        dr32_json_free(v->first);
        free(v->key);
        free(v->str);
        free(v);
        v = next;
    }
}

const dr32_json *dr32_json_get(const dr32_json *obj, const char *key) {
    if (!obj || obj->type != DR32_JSON_OBJECT || !key) return NULL;
    for (const dr32_json *m = obj->first; m; m = m->next) {
        if (m->key && !strcmp(m->key, key)) return m;
    }
    return NULL;
}

const dr32_json *dr32_json_at(const dr32_json *arr, int index) {
    if (!arr || arr->type != DR32_JSON_ARRAY || index < 0) return NULL;
    int i = 0;
    for (const dr32_json *m = arr->first; m; m = m->next, i++) {
        if (i == index) return m;
    }
    return NULL;
}

int dr32_json_count(const dr32_json *v) {
    if (!v) return 0;
    int n = 0;
    for (const dr32_json *m = v->first; m; m = m->next) n++;
    return n;
}

double dr32_json_num(const dr32_json *v, const char *key, double fallback) {
    const dr32_json *m = dr32_json_get(v, key);
    if (!m) return fallback;
    if (m->type == DR32_JSON_NUMBER) return m->num;
    if (m->type == DR32_JSON_BOOL) return m->boolean ? 1.0 : 0.0;
    return fallback;
}

const char *dr32_json_str(const dr32_json *v, const char *key, const char *fallback) {
    const dr32_json *m = dr32_json_get(v, key);
    if (!m || m->type != DR32_JSON_STRING || !m->str) return fallback;
    return m->str;
}

int dr32_json_bool(const dr32_json *v, const char *key, int fallback) {
    const dr32_json *m = dr32_json_get(v, key);
    if (!m) return fallback;
    if (m->type == DR32_JSON_BOOL) return m->boolean;
    if (m->type == DR32_JSON_NUMBER) return m->num != 0.0;
    return fallback;
}

const dr32_json *dr32_json_find_kind(const dr32_json *v, const char *kind) {
    if (!v || !kind) return NULL;
    if (v->type == DR32_JSON_OBJECT) {
        const char *k = dr32_json_str(v, "kind", NULL);
        if (k && !strcmp(k, kind)) return v;
    }
    for (const dr32_json *m = v->first; m; m = m->next) {
        const dr32_json *hit = dr32_json_find_kind(m, kind);
        if (hit) return hit;
    }
    return NULL;
}
