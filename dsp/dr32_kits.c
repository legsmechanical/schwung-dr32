// dr32_kits.c — see dr32_kits.h for why this exists and why the scan is
// incremental rather than a single pass.

#include "dr32_kits.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* The two roots, and how a file under each is categorised.
 *
 * Core drum kits are already sorted by the library itself — Acoustic (11),
 * Electronic (23), Hybrid (43) — so the category is just the first path segment
 * under the root, and the library's own naming does the work. Nothing sits
 * loose in Drums/ today, but a file that does gets "Move Kits" rather than
 * being dropped. Everything the user made is one category: their tree is
 * theirs, up to 8 levels deep, and inventing categories from it would be
 * guessing. */
#define CORE_ROOT "/data/CoreLibrary/Track Presets/Drums"
#define USER_ROOT "/data/UserData/UserLibrary/Track Presets"
#define CORE_LOOSE_CAT "Move Kits"
#define USER_CAT       "My Kits"

typedef struct {
    char path[DR32_MAX_PATH];
    char name[DR32_KIT_NAME_LEN];
    int  cat;                       /* index into c->cat_name */
} entry;

typedef struct {
    DIR *d;
    char path[DR32_MAX_PATH];
    int  cat;                       /* category inherited by everything below */
} frame;

struct dr32_kits {
    entry *v;
    int    n;

    char   cat_name[DR32_KIT_CATS][DR32_KIT_NAME_LEN];
    int    cat_n;

    /* Walk state. `root` is which root we are on; -1 once finished. */
    int    root;
    frame  stack[DR32_KITS_WALK_MAX];
    int    depth;
    int    done;
};

/* ---------- small helpers ------------------------------------------------ */

static int is_preset_name(const char *n) {
    const char *d = strrchr(n, '.');
    if (!d) return 0;
    return !strcasecmp(d, ".ablpreset") || !strcasecmp(d, ".json");
}

/** Basename without its extension, into `out`. */
static void stem_of(const char *path, char *out, size_t cap) {
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    const char *d = strrchr(b, '.');
    size_t n = (d && d != b) ? (size_t)(d - b) : strlen(b);
    if (n >= cap) n = cap - 1;
    memcpy(out, b, n);
    out[n] = '\0';
}

/**
 * Is this file a drum rack?
 *
 * ⭑ ONE 1 KB READ. Measured over every drum rack on the device, the
 * `drumZoneSettings` / `drumRack` marker lands between byte 452 and 581 —
 * always inside the first kilobyte — so a file without it in that window is not
 * a drum rack and needs no further reading. That is what makes scanning 442
 * files affordable at all; the non-drum presets are ~11 KB each and reading
 * them whole is what made a shell version of this take a second.
 *
 * ⚠ Do NOT switch this to the `kind` field. A drum kit's kind is
 * "instrumentRack", exactly like every other rack — it does not distinguish.
 */
static int is_drum_rack(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[1025];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, "drumZoneSettings") != NULL || strstr(buf, "drumRack") != NULL;
}

/** Index of `name` in the category table, appending it if new. -1 if full. */
static int cat_intern(dr32_kits *c, const char *name) {
    for (int i = 0; i < c->cat_n; i++)
        if (!strcmp(c->cat_name[i], name)) return i;
    if (c->cat_n >= DR32_KIT_CATS) return -1;
    snprintf(c->cat_name[c->cat_n], DR32_KIT_NAME_LEN, "%s", name);
    return c->cat_n++;
}

static int cmp_entry(const void *a, const void *b) {
    const entry *x = (const entry *)a, *y = (const entry *)b;
    if (x->cat != y->cat) return x->cat - y->cat;
    return strcasecmp(x->name, y->name);
}

/* ---------- the incremental walk ----------------------------------------- */

static void walk_reset(dr32_kits *c) {
    for (int i = 0; i < c->depth; i++)
        if (c->stack[i].d) closedir(c->stack[i].d);
    c->depth = 0;
}

/** Open `path` as the next frame. Silently ignored if it will not open or the
 *  stack is full — an unreadable folder is a folder with no kits in it, not an
 *  error worth failing the whole catalogue for. */
static void walk_push(dr32_kits *c, const char *path, int cat) {
    if (c->depth >= DR32_KITS_WALK_MAX) return;
    DIR *d = opendir(path);
    if (!d) return;
    frame *f = &c->stack[c->depth++];
    f->d = d;
    f->cat = cat;
    snprintf(f->path, sizeof(f->path), "%s", path);
}

/**
 * Begin the next root, or finish.
 *
 * ⭑ `DR32_KIT_ROOTS` overrides both roots with "<core>:<user>" so the catalogue
 * is TESTABLE. Without it the roots are two absolute /data paths that exist only
 * on the device, which meant the off-device tests could only poke at boundary
 * conditions — and a test written against an empty catalogue passes VACUOUSLY,
 * which is exactly how the deferred-audition test first "passed" while asserting
 * nothing. Unset in every real run; reading it costs one getenv at instance
 * creation.
 */
static void walk_next_root(dr32_kits *c) {
    const char *over = getenv("DR32_KIT_ROOTS");
    char core[DR32_MAX_PATH], user[DR32_MAX_PATH];
    const char *core_root = CORE_ROOT, *user_root = USER_ROOT;
    if (over && *over) {
        const char *sep = strchr(over, ':');
        if (sep) {
            size_t n = (size_t)(sep - over);
            if (n < sizeof core) {
                memcpy(core, over, n); core[n] = '\0';
                snprintf(user, sizeof user, "%s", sep + 1);
                core_root = core; user_root = user;
            }
        }
    }
    c->root++;
    if (c->root == 0) {
        walk_push(c, core_root, -1);        /* -1: category comes from the subfolder */
    } else if (c->root == 1) {
        walk_push(c, user_root, cat_intern(c, USER_CAT));
    } else {
        c->done = 1;
        qsort(c->v, (size_t)c->n, sizeof(entry), cmp_entry);
    }
}

int dr32_kits_pump(dr32_kits *c, int budget) {
    if (!c || c->done) return 1;
    if (budget <= 0) budget = 1;

    while (budget > 0) {
        if (c->depth == 0) {
            walk_next_root(c);
            if (c->done) return 1;
            if (c->depth == 0) continue;    /* that root does not exist */
        }

        frame *f = &c->stack[c->depth - 1];
        struct dirent *e = readdir(f->d);
        if (!e) {
            closedir(f->d);
            c->depth--;
            continue;                        /* costs nothing; not charged */
        }
        if (e->d_name[0] == '.') continue;   /* dotfiles, . and .. */

        char full[DR32_MAX_PATH];
        if ((size_t)snprintf(full, sizeof(full), "%s/%s", f->path, e->d_name) >= sizeof(full))
            continue;

        struct stat st;
        if (stat(full, &st) != 0) continue;
        budget--;

        if (S_ISDIR(st.st_mode)) {
            /* Directly under the Core root, the folder NAMES the category and
             * everything below it inherits that. Anywhere else the category is
             * whatever the frame already carries. */
            int cat = (f->cat < 0) ? cat_intern(c, e->d_name) : f->cat;
            walk_push(c, full, cat);
            continue;
        }
        if (!S_ISREG(st.st_mode) || !is_preset_name(e->d_name)) continue;
        if (c->n >= DR32_KITS_MAX) continue;
        if (!is_drum_rack(full)) continue;

        int cat = (f->cat < 0) ? cat_intern(c, CORE_LOOSE_CAT) : f->cat;
        if (cat < 0) continue;               /* category table full */
        entry *en = &c->v[c->n++];
        snprintf(en->path, sizeof(en->path), "%s", full);
        stem_of(full, en->name, sizeof(en->name));
        en->cat = cat;
    }
    return c->done;
}

/* ---------- lifecycle and reads ------------------------------------------ */

dr32_kits *dr32_kits_create(void) {
    dr32_kits *c = (dr32_kits *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->v = (entry *)calloc(DR32_KITS_MAX, sizeof(entry));
    if (!c->v) { free(c); return NULL; }
    c->root = -1;
    return c;
}

void dr32_kits_destroy(dr32_kits *c) {
    if (!c) return;
    walk_reset(c);
    free(c->v);
    free(c);
}

void dr32_kits_invalidate(dr32_kits *c) {
    if (!c) return;
    walk_reset(c);
    c->n = 0;
    c->cat_n = 0;
    c->root = -1;
    c->done = 0;
}

int dr32_kits_ready(const dr32_kits *c) { return c && c->done; }

int dr32_kits_cat_count(const dr32_kits *c) { return c ? c->cat_n : 0; }

const char *dr32_kits_cat_name(const dr32_kits *c, int cat) {
    if (!c || cat < 0 || cat >= c->cat_n) return "";
    return c->cat_name[cat];
}

int dr32_kits_count(const dr32_kits *c, int cat) {
    if (!c) return 0;
    int n = 0;
    for (int i = 0; i < c->n; i++) if (c->v[i].cat == cat) n++;
    return n;
}

/** The nth entry of a category, or NULL. The catalogue is sorted by (cat,name),
 *  so this is a short scan rather than an index — at ~152 entries that is
 *  cheaper than keeping a second table in step with the incremental fill. */
static const entry *nth(const dr32_kits *c, int cat, int idx) {
    if (!c || idx < 0) return NULL;
    for (int i = 0; i < c->n; i++) {
        if (c->v[i].cat != cat) continue;
        if (idx-- == 0) return &c->v[i];
    }
    return NULL;
}

const char *dr32_kits_name(const dr32_kits *c, int cat, int idx) {
    const entry *e = nth(c, cat, idx);
    return e ? e->name : "";
}

const char *dr32_kits_path(const dr32_kits *c, int cat, int idx) {
    const entry *e = nth(c, cat, idx);
    return e ? e->path : "";
}

int dr32_kits_locate(const dr32_kits *c, const char *path, int *cat, int *idx) {
    if (!c || !path || !path[0]) return -1;
    for (int i = 0; i < c->n; i++) {
        if (strcmp(c->v[i].path, path)) continue;
        int k = 0;
        for (int j = 0; j < i; j++) if (c->v[j].cat == c->v[i].cat) k++;
        if (cat) *cat = c->v[i].cat;
        if (idx) *idx = k;
        return 0;
    }
    return -1;
}
