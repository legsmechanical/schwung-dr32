// dr32_plugins.c — the engines OTHER MODULES bring (dr32_engine_api.h).
//
// A module that ships `dr32_engine.so` beside its own files offers DR32 its
// voices. This file finds those, loads them, and turns each into what the rest
// of DR32 already understands: a dr32_engine_ops, some dr32_models, and the
// pages a built-in engine gets from tools/gen_engine_ui.mjs — built here at
// run time, from the same template that tool writes (src/engine_tpl.json).
//
// ⭐ THE SCAN IS ON ITS OWN THREAD, started by the first create_instance.
// That call is on the SPI callback, and a directory walk plus a dlopen from
// cold storage is milliseconds there (the kit catalogue taught this,
// dr32_kits.h). Everything is filled in privately and PUBLISHED ONCE: until
// then every reader sees no plugins, and after it nothing here ever changes,
// so the audio thread reads it without a lock. Nothing is ever unloaded.
//
// ⭐ IDS ARE THIS PROCESS'S, KEYS AND SLUGS ARE FOR GOOD. A plugin engine's id
// (DR32_ENG_COUNT and up) is only what `ui_engine` and the served pages agree
// on today. What a kit SAVES is the model's slug ("omega/fm2_kick") and the
// params' full keys ("x_omega_fm2_pitch"), which do not depend on what else is
// installed or in what order it was found.
//
// A plugin that breaks a rule in dr32_engine_api.h is refused WHOLE, with the
// reason in the report dr32.c logs: half a plugin is a kit that half loads.

#define _GNU_SOURCE

#include "dr32_engine.h"
#include "dr32_engine_api.h"

#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define PL_MAX_ENGINES 64
#define PL_MAX_MODELS  256
#define PL_MAX_PAGES   192
#define PL_MAX_FOUND   64

static dr32_engine_ops  g_eng[PL_MAX_ENGINES];
static char            *g_eng_keys[PL_MAX_ENGINES];   /* ,"k1","k2" — for a copy list */
static dr32_model       g_mod[PL_MAX_MODELS];
static dr32_plugin_page g_pages[PL_MAX_PAGES];
static int   g_neng, g_nmod, g_npages;
static char *g_families;            /* the picker's sections, as JSON */
static char *g_report;              /* what happened, for the log     */
static atomic_int g_ready;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int  g_started, g_joined;
static char g_dir[1024];

/* ---- a growing string --------------------------------------------------- */
typedef struct { char *s; size_t n, cap; int bad; } sb;

static void sb_put(sb *b, const char *p, size_t len) {
    if (b->bad) return;
    if (b->n + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->n + len + 1) cap *= 2;
        char *s = (char *)realloc(b->s, cap);
        if (!s) { b->bad = 1; return; }
        b->s = s; b->cap = cap;
    }
    memcpy(b->s + b->n, p, len);
    b->n += len;
    b->s[b->n] = '\0';
}
static void sb_str(sb *b, const char *p) { sb_put(b, p, strlen(p)); }
static void sb_fmt(sb *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= sizeof(tmp)) { b->bad = 1; return; }
    sb_put(b, tmp, (size_t)w);
}
/* A JSON string. The text was checked printable by `is_text`. */
static void sb_json(sb *b, const char *p) {
    sb_put(b, "\"", 1);
    for (; *p; p++) {
        if (*p == '"' || *p == '\\') sb_put(b, "\\", 1);
        sb_put(b, p, 1);
    }
    sb_put(b, "\"", 1);
}
static char *sb_take(sb *b) {
    if (b->bad) { free(b->s); return NULL; }
    if (!b->s) sb_put(b, "", 0);
    return b->bad ? NULL : b->s;
}

static void report(const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    size_t have = g_report ? strlen(g_report) : 0, add = strlen(tmp);
    char *r = (char *)realloc(g_report, have + add + 2);
    if (!r) return;
    memcpy(r + have, tmp, add);
    r[have + add] = '\n';
    r[have + add + 1] = '\0';
    g_report = r;
}

/* ---- what a plugin may say ---------------------------------------------- */
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
static int whole(float f) { return f == (float)(long)f; }

/* One option of "A|B|C" by index, into `out`. */
static int option_at(const char *opts, int idx, char *out, size_t cap) {
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
static int option_count(const char *opts) {
    int n = 1;
    for (const char *p = opts; *p; p++) if (*p == '|') n++;
    return n;
}

/* `src` with every `tok` replaced by `with`. */
static char *replace_all(const char *src, const char *tok, const char *with) {
    sb b = {0};
    size_t tl = strlen(tok);
    for (const char *p = src;;) {
        const char *hit = strstr(p, tok);
        if (!hit) { sb_str(&b, p); break; }
        sb_put(&b, p, (size_t)(hit - p));
        sb_str(&b, with);
        p = hit + tl;
    }
    return sb_take(&b);
}

/* A param as the hierarchy declares it — tools/gen_engine_ui.mjs paramEntry. */
static int param_json(sb *b, const dr32_eparam *p) {
    sb_str(b, "{\"key\":");        sb_json(b, p->key);
    sb_str(b, ",\"name\":");       sb_json(b, p->name);
    sb_str(b, ",\"short_name\":"); sb_json(b, p->short_name);
    if (p->options) {
        char opt[64];
        sb_str(b, ",\"type\":\"enum\",\"options\":[");
        for (int i = 0; option_at(p->options, i, opt, sizeof(opt)); i++) {
            if (i) sb_str(b, ",");
            sb_json(b, opt);
        }
        if (!option_at(p->options, (int)(p->def + 0.5f), opt, sizeof(opt))) return 0;
        sb_str(b, "],\"default\":"); sb_json(b, opt);
    } else {
        int is_float = !whole(p->step) || !whole(p->min) || !whole(p->max) || !whole(p->def);
        sb_fmt(b, ",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"default\":%g",
               is_float ? "float" : "int", (double)p->min, (double)p->max, (double)p->def);
        if (is_float) sb_fmt(b, ",\"step\":%g", (double)p->step);
        if (p->unit) { sb_str(b, ",\"unit\":"); sb_json(b, p->unit); }
    }
    sb_str(b, "}");
    return 1;
}

/* ---- one plugin --------------------------------------------------------- */
static int id_taken(const char *id) {
    size_t n = strlen(id);
    for (int i = 0; i < dr32_model_count(); i++) {      /* built-ins only: not published yet */
        const char *s = dr32_model_at(i)->slug;
        if (!strncmp(s, id, n) && s[n] == '/') return 1;
    }
    for (int i = 0; i < g_nmod; i++)
        if (!strncmp(g_mod[i].slug, id, n) && g_mod[i].slug[n] == '/') return 1;
    return 0;
}

/* Check everything BEFORE registering anything. Returns the reason, or NULL. */
static const char *validate(const dr32x_plugin *pl) {
    static char why[200];
    if (pl->api_version != DR32X_API_VERSION) {
        snprintf(why, sizeof(why), "speaks API %u, DR32 speaks %u", pl->api_version, (unsigned)DR32X_API_VERSION);
        return why;
    }
    if (pl->struct_size < sizeof(dr32x_plugin)) return "struct_size is too small";
    if (!is_ident(pl->id, 16, 0)) return "id must be [a-z0-9], 1-16 chars";
    if (!is_text(pl->name, 24)) return "name must be 1-24 printable chars";
    if (id_taken(pl->id)) return "id is already an engine family";
    if (pl->nengines < 1 || !pl->engines) return "no engines";
    if (pl->nmodels < 1 || !pl->models) return "no models";
    if (g_neng + pl->nengines > PL_MAX_ENGINES) return "too many plugin engines installed";
    if (g_nmod + pl->nmodels > PL_MAX_MODELS) return "too many plugin models installed";
    for (int e = 0; e < pl->nengines; e++) {
        const dr32x_engine *x = &pl->engines[e];
        if (!is_ident(x->slug, 16, 1) || !is_text(x->name, 24)) return "an engine's slug or name is malformed";
        for (int j = 0; j < e; j++)
            if (!strcmp(pl->engines[j].slug, x->slug)) return "two engines share a slug";
        if (!x->create || !x->destroy || !x->set || !x->note_on || !x->choke || !x->render)
            return "an engine is missing a function";
        if (x->nparams < 1 || x->nparams > DR32X_MAX_PARAMS || !x->params) return "an engine needs 1-32 params";
        for (int i = 0; i < x->nparams; i++) {
            const dr32x_param *p = &x->params[i];
            snprintf(why, sizeof(why), "engine %s param %d", x->slug, i);
            if (!is_ident(p->key, 24, 1) || !is_text(p->name, 32) || !is_text(p->short_name, 8) ||
                !is_text(p->page, 16) || (p->unit && !is_text(p->unit, 8)))
                return strncat(why, ": key, name, short_name, page or unit is malformed", sizeof(why) - strlen(why) - 1);
            for (int j = 0; j < i; j++)
                if (!strcmp(x->params[j].key, p->key)) return strncat(why, ": key is repeated", sizeof(why) - strlen(why) - 1);
            if (p->options) {
                if (!is_text(p->options, 255)) return strncat(why, ": options malformed", sizeof(why) - strlen(why) - 1);
                int n = option_count(p->options);
                char opt[64];
                for (int k = 0; k < n; k++)
                    if (!option_at(p->options, k, opt, sizeof(opt))) return strncat(why, ": an option is empty or too long", sizeof(why) - strlen(why) - 1);
                if (p->min != 0.0f || p->max != (float)(n - 1) || p->def < 0.0f || p->def > p->max || !whole(p->def))
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

/* "Tone" -> "tone": the page's part of a level key (gen_engine_ui.mjs). */
static void page_slug(const char *page, char *out, size_t cap) {
    size_t n = 0;
    int gap = 0;
    for (const char *p = page; *p && n + 1 < cap; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') { out[n++] = c; gap = 0; }
        else if (!gap) { out[n++] = '_'; gap = 1; }
    }
    out[n] = '\0';
}

/* An engine's pages, from the template: one level per distinct `page`. */
static int make_pages(const dr32_engine_ops *e, const char *keys_csv, const char *tpl) {
    char idtxt[16];
    snprintf(idtxt, sizeof(idtxt), "%d", e->id);
    for (int i = 0; i < e->nparams; i++) {
        const char *page = e->params[i].page;
        int first = 1;
        for (int j = 0; j < i; j++) if (!strcmp(e->params[j].page, page)) first = 0;
        if (!first) continue;
        if (g_npages >= PL_MAX_PAGES) return 0;

        sb params = {0}, knobs = {0}, name = {0}, nav = {0}, level = {0};
        for (int j = i; j < e->nparams; j++) {
            if (strcmp(e->params[j].page, page)) continue;
            if (params.n) { sb_str(&params, ","); sb_str(&knobs, ","); }
            if (!param_json(&params, &e->params[j])) return 0;
            sb_json(&knobs, e->params[j].key);
        }
        sb_json(&name, page);
        char ps[32], key[96];
        page_slug(page, ps, sizeof(ps));
        snprintf(key, sizeof(key), "eng_%s%s", e->prefix, ps);

        /* The template's params and knobs are one-element arrays holding the
         * token, so the token with its brackets becomes the whole array. */
        sb pa = {0}, kn = {0};
        sb_fmt(&pa, "["); sb_str(&pa, params.s ? params.s : ""); sb_str(&pa, "]");
        sb_fmt(&kn, "["); sb_str(&kn, knobs.s ? knobs.s : "");   sb_str(&kn, "]");
        char *a = replace_all(tpl, "\"@NAME@\"", name.s ? name.s : "\"\"");
        char *b = a ? replace_all(a, "\"@ID@\"", idtxt) : NULL;
        char *c = b ? replace_all(b, ",\"@KEYS@\"", keys_csv) : NULL;
        char *d = c ? replace_all(c, "[\"@PARAMS@\"]", pa.s) : NULL;
        char *f = d ? replace_all(d, "[\"@KNOBS@\"]", kn.s) : NULL;
        free(a); free(b); free(c); free(d);
        free(params.s); free(knobs.s); free(pa.s); free(kn.s);
        if (!f) { free(name.s); return 0; }

        sb_fmt(&level, "\"%s\":", key); sb_str(&level, f);
        sb_fmt(&nav, "{\"level\":\"%s\",\"label\":", key); sb_str(&nav, name.s); sb_str(&nav, "}");
        free(f); free(name.s);
        dr32_plugin_page *pg = &g_pages[g_npages];
        pg->engine = e->id;
        pg->nav = sb_take(&nav);
        pg->level = sb_take(&level);
        if (!pg->nav || !pg->level) return 0;
        pg->nav_len = (int)strlen(pg->nav);
        pg->level_len = (int)strlen(pg->level);
        g_npages++;
    }
    return 1;
}

static void add_plugin(const dr32x_plugin *pl, const char *tpl, sb *fam) {
    int base = g_neng;
    for (int e = 0; e < pl->nengines; e++) {
        const dr32x_engine *x = &pl->engines[e];
        dr32_engine_ops *o = &g_eng[g_neng];
        dr32_eparam *ps = (dr32_eparam *)calloc((size_t)x->nparams, sizeof(*ps));
        char *prefix = NULL, *slug = NULL;
        if (asprintf(&prefix, "x_%s_%s_", pl->id, x->slug) < 0 || asprintf(&slug, "%s_%s", pl->id, x->slug) < 0 || !ps) return;
        sb keys = {0};
        for (int i = 0; i < x->nparams; i++) {
            const dr32x_param *p = &x->params[i];
            char *key = NULL;
            if (asprintf(&key, "%s%s", prefix, p->key) < 0) return;
            ps[i] = (dr32_eparam){ key, p->name, p->short_name, p->min, p->max, p->def, p->step,
                                   p->unit, p->page, p->options };
            sb_str(&keys, ","); sb_json(&keys, key);
        }
        *o = (dr32_engine_ops){ DR32_ENG_COUNT + g_neng, slug, x->name, prefix, x->nparams, ps,
                                x->create, x->destroy, x->set, x->note_on, x->choke, x->render,
                                DR32_FAM_PLUGIN };
        g_eng_keys[g_neng] = sb_take(&keys);
        if (!g_eng_keys[g_neng] || (tpl && !make_pages(o, g_eng_keys[g_neng], tpl))) return;
        g_neng++;
    }
    if (fam->n > 1) sb_str(fam, ",");
    sb_str(fam, "{\"id\":");      sb_json(fam, pl->id);
    sb_str(fam, ",\"label\":");   sb_json(fam, pl->name);
    sb_str(fam, ",\"models\":[");
    for (int m = 0; m < pl->nmodels; m++) {
        const dr32x_model *x = &pl->models[m];
        char *slug = NULL;
        if (asprintf(&slug, "%s/%s", pl->id, x->slug) < 0) return;
        g_mod[g_nmod++] = (dr32_model){ slug, x->name, DR32_ENG_COUNT + base + x->engine,
                                        x->values, x->volume_db, x->pan };
        if (m) sb_str(fam, ",");
        sb_str(fam, "{\"slug\":"); sb_json(fam, slug);
        sb_str(fam, ",\"name\":"); sb_json(fam, x->name);
        sb_str(fam, "}");
    }
    sb_str(fam, "]}");
}

/* The level template gen_engine_ui.mjs writes: `{"level":{...}}`, minified. */
static char *read_template(const char *module_dir) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/engine_tpl.json", module_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = (char *)malloc(1 << 16);
    size_t n = buf ? fread(buf, 1, (1 << 16) - 1, f) : 0;
    fclose(f);
    if (!buf) return NULL;
    buf[n] = '\0';
    const char *open = strstr(buf, "\"level\":{");
    char *close = strrchr(buf, '}');           /* the document's own brace */
    if (!open || !close || close <= open) { free(buf); return NULL; }
    *close = '\0';                             /* what is left ends at the level's */
    char *out = strdup(open + 8);
    free(buf);
    return out;
}

static int by_name(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static void *scan(void *arg) {
    (void)arg;
    /* <modules>/<kind>/dr32  ->  every sibling <modules>/<kind>/<x>/dr32_engine.so */
    char parent[1024], self[256] = "";
    snprintf(parent, sizeof(parent), "%s", g_dir);
    size_t n = strlen(parent);
    while (n > 1 && parent[n - 1] == '/') parent[--n] = '\0';
    char *slash = strrchr(parent, '/');
    if (slash) { snprintf(self, sizeof(self), "%s", slash + 1); *(slash > parent ? slash : slash + 1) = '\0'; }
    else { snprintf(self, sizeof(self), "%s", parent); snprintf(parent, sizeof(parent), "."); }

    char *found[PL_MAX_FOUND];
    int nfound = 0;
    DIR *d = opendir(parent);
    for (struct dirent *de; d && (de = readdir(d)) && nfound < PL_MAX_FOUND;) {
        if (de->d_name[0] == '.' || !strcmp(de->d_name, self)) continue;
        char so[1400];
        snprintf(so, sizeof(so), "%s/%s/" DR32X_FILE, parent, de->d_name);
        if (access(so, R_OK) == 0) found[nfound++] = strdup(de->d_name);
    }
    if (d) closedir(d);
    qsort(found, (size_t)nfound, sizeof(found[0]), by_name);   /* readdir's order is not one */

    char *tpl = nfound ? read_template(g_dir) : NULL;
    if (nfound && !tpl) report("dr32: no engine_tpl.json; plugin engines will have no pages");
    sb fam = {0};
    sb_str(&fam, "[");
    for (int i = 0; i < nfound; i++) {
        char dir[1300], so[1400];
        snprintf(dir, sizeof(dir), "%s/%s", parent, found[i]);
        snprintf(so, sizeof(so), "%s/" DR32X_FILE, dir);
        void *h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
        if (!h) { report("dr32: %s did not load: %s", so, dlerror()); continue; }
        const dr32x_plugin *(*entry)(const dr32x_host *) =
            (const dr32x_plugin *(*)(const dr32x_host *))dlsym(h, DR32X_ENTRY);
        if (!entry) { report("dr32: %s exports no " DR32X_ENTRY, so); dlclose(h); continue; }
        dr32x_host host = { DR32X_API_VERSION, 44100, dir };
        const dr32x_plugin *pl = entry(&host);
        if (!pl) { report("dr32: %s offers no engines", found[i]); continue; }
        const char *why = validate(pl);
        if (why) { report("dr32: engine plugin %s refused: %s", found[i], why); continue; }
        int e0 = g_neng, m0 = g_nmod, p0 = g_npages;
        size_t f0 = fam.n;
        add_plugin(pl, tpl, &fam);
        if (g_neng - e0 != pl->nengines || g_nmod - m0 != pl->nmodels || fam.bad) {
            /* Out of memory part-way: none of it. */
            g_neng = e0; g_nmod = m0; g_npages = p0;
            if (!fam.bad) { fam.n = f0; fam.s[f0] = '\0'; }
            report("dr32: engine plugin %s refused: out of memory", found[i]);
            continue;
        }
        report("dr32: engine plugin %s: %d engines, %d models, %d pages",
               pl->id, pl->nengines, pl->nmodels, g_npages - p0);
    }
    for (int i = 0; i < nfound; i++) free(found[i]);
    free(tpl);
    sb_str(&fam, "]");
    g_families = sb_take(&fam);
    atomic_store(&g_ready, 1);
    return NULL;
}

/* ---- the public side (dr32_engine.h) ------------------------------------ */
void dr32_plugins_start(const char *module_dir) {
    if (!module_dir || !module_dir[0]) return;
    pthread_mutex_lock(&g_lock);
    if (!g_started) {
        snprintf(g_dir, sizeof(g_dir), "%s", module_dir);
        if (pthread_create(&g_thread, NULL, scan, NULL) == 0) g_started = 1;
    }
    pthread_mutex_unlock(&g_lock);
}

int dr32_plugins_ready(void) { return atomic_load(&g_ready); }

void dr32_plugins_wait(void) {
    pthread_mutex_lock(&g_lock);
    if (g_started && !g_joined) { pthread_join(g_thread, NULL); g_joined = 1; }
    pthread_mutex_unlock(&g_lock);
}

const dr32_engine_ops *dr32_plugin_engine(int id) {
    int i = id - DR32_ENG_COUNT;
    return (dr32_plugins_ready() && i >= 0 && i < g_neng) ? &g_eng[i] : NULL;
}
const char *dr32_plugin_engine_keys(int id) {
    int i = id - DR32_ENG_COUNT;
    return (dr32_plugins_ready() && i >= 0 && i < g_neng) ? g_eng_keys[i] : NULL;
}
int dr32_plugin_engine_count(void) { return dr32_plugins_ready() ? g_neng : 0; }
int dr32_plugin_model_count(void) { return dr32_plugins_ready() ? g_nmod : 0; }
const dr32_model *dr32_plugin_model_at(int i) {
    return (dr32_plugins_ready() && i >= 0 && i < g_nmod) ? &g_mod[i] : NULL;
}
int dr32_plugin_page_count(void) { return dr32_plugins_ready() ? g_npages : 0; }
const dr32_plugin_page *dr32_plugin_page_at(int i) {
    return (dr32_plugins_ready() && i >= 0 && i < g_npages) ? &g_pages[i] : NULL;
}
const char *dr32_plugin_families_json(void) {
    return (dr32_plugins_ready() && g_families) ? g_families : "[]";
}
const char *dr32_plugins_report(void) { return dr32_plugins_ready() ? g_report : NULL; }
