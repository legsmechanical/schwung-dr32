// dr32_kits.c — see dr32_kits.h for why this exists and why the scan runs on
// its own thread.

/* pthreads are POSIX, and the build is -std=c11. */
#define _POSIX_C_SOURCE 200809L

#include "dr32_kits.h"

#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
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

/* One catalogue: what the reads see. Immutable once published. */
typedef struct {
    entry *v;
    int    n;
    char   cat_name[DR32_KIT_CATS][DR32_KIT_NAME_LEN];
    int    cat_n;
} snap;

struct dr32_kits {
    /* What the reads see. The AUDIO thread alone reads and moves it: `seed`
     * (Init only) until the worker's catalogue is adopted, then that. */
    const snap *cur;
    int         ready;
    snap        seed;
    entry       seed_v[1];

    /* worker -> audio, once: the finished catalogue. */
    _Atomic(snap *) pub;
    atomic_int      quit;
    int             started;    /* the thread was created, so destroy joins it */
    pthread_t       th;
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
static int cat_intern(snap *c, const char *name) {
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

/* ---------- the walk, on the worker thread ------------------------------- */

typedef struct {
    frame stack[DR32_KITS_WALK_MAX];
    int   depth;
} walk;

/** Open `path` as the next frame. Silently ignored if it will not open or the
 *  stack is full — an unreadable folder is a folder with no kits in it, not an
 *  error worth failing the whole catalogue for. */
static void walk_push(walk *w, const char *path, int cat) {
    if (w->depth >= DR32_KITS_WALK_MAX) return;
    DIR *d = opendir(path);
    if (!d) return;
    frame *f = &w->stack[w->depth++];
    f->d = d;
    f->cat = cat;
    snprintf(f->path, sizeof(f->path), "%s", path);
}

/** Walk one root to the end, appending every drum rack under it to `s`.
 *  Returns 0 if `quit` stopped it partway. */
static int walk_root(snap *s, const char *root, int cat, atomic_int *quit) {
    walk w = { .depth = 0 };
    walk_push(&w, root, cat);
    while (w.depth > 0) {
        if (atomic_load(quit)) {
            while (w.depth > 0) closedir(w.stack[--w.depth].d);
            return 0;
        }
        frame *f = &w.stack[w.depth - 1];
        struct dirent *e = readdir(f->d);
        if (!e) { closedir(f->d); w.depth--; continue; }
        if (e->d_name[0] == '.') continue;   /* dotfiles, . and .. */

        char full[DR32_MAX_PATH];
        if ((size_t)snprintf(full, sizeof(full), "%s/%s", f->path, e->d_name) >= sizeof(full))
            continue;

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Directly under the Core root, the folder NAMES the category and
             * everything below it inherits that. Anywhere else the category is
             * whatever the frame already carries. */
            int sub = (f->cat < 0) ? cat_intern(s, e->d_name) : f->cat;
            walk_push(&w, full, sub);
            continue;
        }
        if (!S_ISREG(st.st_mode) || !is_preset_name(e->d_name)) continue;
        if (s->n >= DR32_KITS_MAX) continue;
        if (!is_drum_rack(full)) continue;

        int ec = (f->cat < 0) ? cat_intern(s, CORE_LOOSE_CAT) : f->cat;
        if (ec < 0) continue;                /* category table full */
        entry *en = &s->v[s->n++];
        snprintf(en->path, sizeof(en->path), "%s", full);
        stem_of(full, en->name, sizeof(en->name));
        en->cat = ec;
    }
    return 1;
}

/** The Init kit, FIRST: its category is interned before any folder's, and
 *  categories show in the order they were interned. */
static void seed_init(snap *s) {
    int ic = cat_intern(s, DR32_KIT_INIT_CAT);
    if (ic < 0 || s->n >= DR32_KITS_MAX) return;
    entry *en = &s->v[s->n++];
    snprintf(en->path, sizeof(en->path), "%s", DR32_KIT_INIT_PATH);
    snprintf(en->name, sizeof(en->name), "%s", DR32_KIT_INIT_NAME);
    en->cat = ic;
}

/** An empty category is never shown (dr32_kits.h) — but "My Kits" is interned
 *  before its folder is opened, and a Core folder before its contents are
 *  read, so both can end up holding nothing. Close the gaps, order kept. */
static void drop_empty_cats(snap *s) {
    int map[DR32_KIT_CATS], used[DR32_KIT_CATS] = {0}, n = 0;
    for (int i = 0; i < s->n; i++) used[s->v[i].cat] = 1;
    for (int k = 0; k < s->cat_n; k++) {
        map[k] = used[k] ? n : -1;
        if (used[k] && n != k) memcpy(s->cat_name[n], s->cat_name[k], DR32_KIT_NAME_LEN);
        if (used[k]) n++;
    }
    for (int i = 0; i < s->n; i++) s->v[i].cat = map[s->v[i].cat];
    s->cat_n = n;
}

static void snap_free(snap *s) {
    if (!s) return;
    free(s->v);
    free(s);
}

/**
 * The whole scan, start to finish, then ONE publish.
 *
 * ⭑ `DR32_KIT_ROOTS` overrides both roots with "<core>:<user>" so the catalogue
 * is TESTABLE. Without it the roots are two absolute /data paths that exist only
 * on the device, which meant the off-device tests could only poke at boundary
 * conditions — and a test written against an empty catalogue passes VACUOUSLY,
 * which is exactly how the deferred-audition test first "passed" while asserting
 * nothing. Unset in every real run.
 */
static void *kits_worker(void *arg) {
    dr32_kits *c = (dr32_kits *)arg;

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

    snap *s = (snap *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->v = (entry *)calloc(DR32_KITS_MAX, sizeof(entry));
    if (!s->v) { free(s); return NULL; }

    seed_init(s);
    if (!walk_root(s, core_root, -1, &c->quit) ||        /* -1: category from the subfolder */
        !walk_root(s, user_root, cat_intern(s, USER_CAT), &c->quit)) {
        snap_free(s);
        return NULL;
    }
    drop_empty_cats(s);
    qsort(s->v, (size_t)s->n, sizeof(entry), cmp_entry);
    /* 768 slots were a ceiling, not a size: give the rest back. */
    entry *fit = (entry *)realloc(s->v, (size_t)(s->n ? s->n : 1) * sizeof(entry));
    if (fit) s->v = fit;

    atomic_store_explicit(&c->pub, s, memory_order_release);
    return NULL;
}

/* ---------- lifecycle ----------------------------------------------------- */

dr32_kits *dr32_kits_create(void) {
    dr32_kits *c = (dr32_kits *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    /* Init is there before the scan has found anything, so the browser opens
     * on a real list — and loading Init needs no scan at all. */
    c->seed.v = c->seed_v;
    snprintf(c->seed.cat_name[0], DR32_KIT_NAME_LEN, "%s", DR32_KIT_INIT_CAT);
    c->seed.cat_n = 1;
    snprintf(c->seed_v[0].path, sizeof(c->seed_v[0].path), "%s", DR32_KIT_INIT_PATH);
    snprintf(c->seed_v[0].name, sizeof(c->seed_v[0].name), "%s", DR32_KIT_INIT_NAME);
    c->seed.n = 1;
    c->cur = &c->seed;
    atomic_init(&c->pub, NULL);
    atomic_init(&c->quit, 0);
    return c;
}

void dr32_kits_destroy(dr32_kits *c) {
    if (!c) return;
    if (c->started) {
        /* The walk checks this between entries, so the wait is one file. */
        atomic_store(&c->quit, 1);
        pthread_join(c->th, NULL);
    }
    /* Adopted or not, the published catalogue is freed here and only here. */
    snap_free(atomic_load(&c->pub));
    free(c);
}

int dr32_kits_poll(dr32_kits *c) {
    if (!c) return 0;
    if (c->ready) return 1;
    if (!c->started) {
        /* Once. If the thread cannot be made the browser shows Init alone —
         * never the scan on this thread instead. */
        c->started = 1;
        if (pthread_create(&c->th, NULL, kits_worker, c) != 0) {
            c->started = 0;
            c->ready = 1;
            return 1;
        }
        return 0;
    }
    snap *s = atomic_load_explicit(&c->pub, memory_order_acquire);
    if (!s) return 0;
    c->cur = s;
    c->ready = 1;
    return 1;
}

/* ---------- reads: the adopted catalogue, never disk ---------------------- */

int dr32_kits_ready(const dr32_kits *c) { return c && c->ready; }

int dr32_kits_cat_count(const dr32_kits *c) { return c ? c->cur->cat_n : 0; }

const char *dr32_kits_cat_name(const dr32_kits *c, int cat) {
    if (!c || cat < 0 || cat >= c->cur->cat_n) return "";
    return c->cur->cat_name[cat];
}

int dr32_kits_count(const dr32_kits *c, int cat) {
    if (!c) return 0;
    const snap *s = c->cur;
    int n = 0;
    for (int i = 0; i < s->n; i++) if (s->v[i].cat == cat) n++;
    return n;
}

/** The nth entry of a category, or NULL. The catalogue is sorted by (cat,name),
 *  so this is a short scan rather than an index — at ~152 entries that is
 *  cheaper than keeping a second table. */
static const entry *nth(const dr32_kits *c, int cat, int idx) {
    if (!c || idx < 0) return NULL;
    const snap *s = c->cur;
    for (int i = 0; i < s->n; i++) {
        if (s->v[i].cat != cat) continue;
        if (idx-- == 0) return &s->v[i];
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
    const snap *s = c->cur;
    for (int i = 0; i < s->n; i++) {
        if (strcmp(s->v[i].path, path)) continue;
        int k = 0;
        for (int j = 0; j < i; j++) if (s->v[j].cat == s->v[i].cat) k++;
        if (cat) *cat = s->v[i].cat;
        if (idx) *idx = k;
        return 0;
    }
    return -1;
}
