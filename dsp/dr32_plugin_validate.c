// dr32_plugin_validate.c — is this what dr32_engine_api.h allows?
//
// Everything a plugin can get wrong that can be told from what it RETURNS,
// with no knowledge of DR32's state: names, ranges, pages, models. One file
// with no dependency but the API header, because it has two users that must
// never disagree — DR32's loader (dsp/dr32_plugins.c), which refuses a plugin
// that fails, and tools/plugin_check.c, which tells a developer so on their
// own machine before a device is involved.

#include "dr32_plugin_validate.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* DR32's own engine families: a plugin id is a picker section and the first
 * half of a model slug, so it cannot be one of these. tests/test_plugins.c
 * holds this list to the built-in models' real slugs. */
static const char *const DR32_FAMILIES[] = {
    "sample", "simian", "urchin", "9w9", "6w6", "8w8", "cw78", "chowkick", "fm",
};

static int is_ident(const char *s, size_t max, int underscore) {
    if (!s || !s[0]) return 0;
    size_t n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (underscore && c == '_');
        if (!ok || n >= max) return 0;
    }
    return 1;
}
static int is_text(const char *s, size_t max) {
    if (!s || !s[0]) return 0;
    size_t n = 0;
    for (; s[n]; n++) if ((unsigned char)s[n] < 0x20 || (unsigned char)s[n] > 0x7e || n >= max) return 0;
    return 1;
}
int dr32x_whole(float f) { return f == (float)(long)f; }

/* One option of "A|B|C" by index, into `out`. */
int dr32x_option_at(const char *opts, int idx, char *out, size_t cap) {
    const char *p = opts;
    for (int i = 0; p; i++) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (i == idx) {
            if (!len || len >= cap) return 0;
            memcpy(out, p, len);
            out[len] = '\0';
            return 1;
        }
        p = bar ? bar + 1 : NULL;
    }
    return 0;
}
int dr32x_option_count(const char *opts) {
    int n = 1;
    for (const char *p = opts; *p; p++) if (*p == '|') n++;
    return n;
}

const char *dr32_plugin_validate(const dr32x_plugin *pl) {
    static char why[200];
    if (pl->api_version != DR32X_API_VERSION) {
        snprintf(why, sizeof(why), "speaks API %u, DR32 speaks %u", pl->api_version, (unsigned)DR32X_API_VERSION);
        return why;
    }
    if (pl->struct_size < sizeof(dr32x_plugin)) return "struct_size is too small";
    if (!is_ident(pl->id, 12, 0)) return "id must be [a-z0-9], 1-12 chars";
    if (!is_text(pl->name, 24)) return "name must be 1-24 printable chars";
    for (size_t r = 0; r < sizeof(DR32_FAMILIES) / sizeof(DR32_FAMILIES[0]); r++)
        if (!strcmp(pl->id, DR32_FAMILIES[r])) return "id is one of DR32's own engine families";
    if (pl->nengines < 1 || !pl->engines) return "no engines";
    if (pl->nmodels < 1 || !pl->models) return "no models";
    for (int e = 0; e < pl->nengines; e++) {
        const dr32x_engine *x = &pl->engines[e];
        if (!is_ident(x->slug, 12, 1) || !is_text(x->name, 24)) return "an engine's slug or name is malformed";
        for (int j = 0; j < e; j++)
            if (!strcmp(pl->engines[j].slug, x->slug)) return "two engines share a slug";
        /* `choke` may be NULL: DR32 fades a choked pad itself. */
        if (!x->create || !x->destroy || !x->set || !x->note_on || !x->render)
            return "an engine is missing a function";
        if (x->nparams < 1 || x->nparams > DR32X_MAX_PARAMS || !x->params) return "an engine needs 1-32 params";
        for (int i = 0; i < x->nparams; i++) {
            const dr32x_param *p = &x->params[i];
            snprintf(why, sizeof(why), "engine %s param %d", x->slug, i);
            if (!is_ident(p->key, 16, 1) || !is_text(p->name, 32) || !is_text(p->short_name, 8) ||
                !is_text(p->page, 16) || (p->unit && !is_text(p->unit, 8)))
                return strncat(why, ": key, name, short_name, page or unit is malformed", sizeof(why) - strlen(why) - 1);
            for (int j = 0; j < i; j++)
                if (!strcmp(x->params[j].key, p->key)) return strncat(why, ": key is repeated", sizeof(why) - strlen(why) - 1);
            if (p->options) {
                if (!is_text(p->options, 255)) return strncat(why, ": options malformed", sizeof(why) - strlen(why) - 1);
                int n = dr32x_option_count(p->options);
                char opt[64];
                for (int k = 0; k < n; k++)
                    if (!dr32x_option_at(p->options, k, opt, sizeof(opt))) return strncat(why, ": an option is empty or too long", sizeof(why) - strlen(why) - 1);
                if (p->min != 0.0f || p->max != (float)(n - 1) || p->def < 0.0f || p->def > p->max || !dr32x_whole(p->def))
                    return strncat(why, ": an enum runs 0..count-1", sizeof(why) - strlen(why) - 1);
            } else if (!(p->min < p->max) || p->def < p->min || p->def > p->max || !(p->step > 0.0f)) {
                return strncat(why, ": needs min < max, def inside, step > 0", sizeof(why) - strlen(why) - 1);
            }
            /* DR32's own pages are in the same list; a second "Mix" is two pages
             * the host tells apart by name. */
            static const char *const OURS[] = { "Pad", "Shape", "Mix", "Stereo", "Master", "Resample", "Category", "Kit" };
            for (size_t r = 0; r < sizeof(OURS) / sizeof(OURS[0]); r++)
                if (!strcasecmp(p->page, OURS[r])) return strncat(why, ": its page has the name of one of DR32's own", sizeof(why) - strlen(why) - 1);
            /* A bank holds eight knobs. */
            int on_page = 0;
            for (int j = 0; j < x->nparams; j++) if (!strcmp(x->params[j].page, p->page)) on_page++;
            if (on_page > DR32X_PAGE_KNOBS) return strncat(why, ": its page has more than 8 knobs", sizeof(why) - strlen(why) - 1);
        }
    }
    for (int m = 0; m < pl->nmodels; m++) {
        const dr32x_model *x = &pl->models[m];
        if (!is_ident(x->slug, 24, 1) || !is_text(x->name, 24)) return "a model's slug or name is malformed";
        for (int j = 0; j < m; j++)
            if (!strcmp(pl->models[j].slug, x->slug)) return "two models share a slug";
        if (x->engine < 0 || x->engine >= pl->nengines || !x->values) return "a model names no engine or has no values";
        const dr32x_engine *e = &pl->engines[x->engine];
        for (int i = 0; i < e->nparams; i++)
            if (x->values[i] < e->params[i].min || x->values[i] > e->params[i].max) return "a model's value is outside its param's range";
    }
    return NULL;
}

