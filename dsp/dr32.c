/* clock_gettime/CLOCK_MONOTONIC are POSIX, not ISO C11, and we build -std=c11.
 * Use 200809L, not 199309L: the older level hides C99 functions like snprintf. */
#define _POSIX_C_SOURCE 200809L

// dr32.c — Schwung plugin entry (API v2) for DR32.
//
// Kits load HERE (dr32_preset.c parses the .ablpreset on the host thread) and
// every pad parameter is a flat param, pad<N>_<key> (dr32_params.c). The UI is
// the host's own param-pages grid, planned from the hierarchy this file serves
// — DR32 ships no UI code of its own beyond ui.js's play view.

#include "host/plugin_api_v1.h"
#include "dr32_kit.h"
#include "dr32_params.h"
#include "dr32_preset.h"
#include "dr32_kits.h"
#include "dr32_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const host_api_v1_t *g_host = NULL;

typedef struct {
    dr32_kit kit;
    float    scratch[2 * 1024];   // float mix before int16 conversion
    char     err[256];
    // The kit path is set by the host's file browser (the `kit` / `kit_move` /
    // `kit_user` filepath params), which calls set_param on the DSP; the load
    // happens right there, on the host thread.
    char     kit_path[DR32_MAX_PATH];
    // The Shadow UI asks the DSP for "ui_hierarchy" FIRST and only parses
    // module.json if we return <= 2 bytes (shadow_chain_mgmt.c). Serving it
    // ourselves takes that fallback — and any doubt about its brace-matching
    // extraction — out of the picture. `ui_hierarchy_src` is module.json's
    // text verbatim; `ui_hierarchy` is what we serve: the same text with the
    // loaded kit's pad names spliced in as `child_names` (see
    // dr32_refresh_hierarchy), so the host's voice list and header say
    // "Kick 707" rather than "Pad 1".
    char    *ui_hierarchy_src;
    char    *ui_hierarchy;
    int      ui_hierarchy_len;
    // Snapshot taken when the kit browser opens, so cancelling can put the
    // previous kit back. The host's own live_preview restore writes
    // `previewOriginalValue || ""`, and for this param that value is routinely
    // empty — an empty path cannot be loaded, so the previewed kit used to
    // stick after backing out.
    char     kit_saved[DR32_MAX_PATH];
    // Full state blob captured right after each successful kit load — the
    // "unedited" reference. get_param("state") hands it to dr32_state_write so
    // the persisted blob carries ONLY the user's edits; the kit path restores
    // the rest. Kept as the serialized blob (not a struct copy) so the compare
    // uses the exact same read path as the write. NULL = no baseline = full
    // dump, which is always a correct fallback.
    char    *state_baseline;

    /* The kit catalogue behind the two-level Kits browser, and where the
     * browser's cursor currently sits. Built incrementally — see dr32_kits.h
     * for why a single-pass scan is not a legal shape here. */
    dr32_kits *kits;
    int        kit_cat;      /* category the browser is showing */
    int        kit_idx;      /* entry within that category */
    int        kit_located;  /* cursor has been aimed at the loaded kit once */
    /* Deferred audition. A detent on the kit list only moves the CURSOR; the
     * load happens once the cursor has been still for a moment. See
     * dr32_service_pending_kit. */
    int        kit_pending;      /* -1 = nothing owed */
    unsigned   kit_pending_at;   /* render-block counter when it was last moved */

    /* A per-pad sample swap changed the pad NAMES and the host has not re-read
     * the hierarchy yet. Serving `is_loading` across the swap is how we ask it
     * to — see the is_loading note in get_param. */
    int        names_dirty;
    unsigned   names_dirty_at;   /* render-block counter of the last swap */
} dr32_instance;

/** Capture the freshly-loaded kit as the state baseline. Called after every
 *  successful kit load (default kit, picker, preview-cancel restore, state
 *  restore via the same picker path). Failure just leaves no baseline, which
 *  degrades to the full state dump — never an error. */
static void dr32_refresh_hierarchy(dr32_instance *in);
static void dr32_capture_baseline(dr32_instance *in) {
    free(in->state_baseline);
    in->state_baseline = NULL;
    char *tmp = malloc(65536);
    if (!tmp) return;
    int n = dr32_state_write(&in->kit, in->kit_path, tmp, 65536, NULL);
    if (n > 0) in->state_baseline = strdup(tmp);
    free(tmp);
    /* Every kit load lands here, so this is the one place the pad names can
     * change. Not in load_sample: a per-pad swap keeps the rest of the kit and
     * the host re-reads the hierarchy only on a contract settle anyway. */
    dr32_refresh_hierarchy(in);
}

/** Append one pad's display name to `out` as a JSON string: the sample's
 *  basename without extension, escaped; "" for an empty pad, which the host
 *  falls back PER ITEM to "Pad N". Returns bytes written, 0 if it would not
 *  fit — the caller then serves the hierarchy without names, which is the
 *  same page with worse labels rather than no page. */
static int append_pad_name(const dr32_pad_slot *s, char *out, int cap) {
    const char *base = s->path[0] ? strrchr(s->path, '/') : NULL;
    base = base ? base + 1 : s->path;
    const char *dot = strrchr(base, '.');
    int len = (dot && dot != base) ? (int)(dot - base) : (int)strlen(base);
    int n = 0;
    if (n + 1 >= cap) return 0;
    out[n++] = '"';
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)base[i];
        const char *esc = NULL;
        if (c == '"') esc = "\\\"";
        else if (c == '\\') esc = "\\\\";
        else if (c < 0x20) esc = " ";
        int w = esc ? (int)strlen(esc) : 1;
        if (n + w + 1 >= cap) return 0;
        if (esc) { memcpy(out + n, esc, (size_t)w); n += w; }
        else out[n++] = (char)c;
    }
    out[n++] = '"';
    out[n] = '\0';
    return n;
}

/** Write `"child_names": [...], ` at out+n. Returns the new length, or 0 if it
 *  would not fit — the caller then serves the hierarchy without names, which is
 *  the same pages with worse labels rather than no pages. */
static size_t append_child_names(const dr32_kit *kit, char *out, size_t n, size_t cap) {
    int w = snprintf(out + n, cap - n, "\"child_names\": [");
    if (w <= 0 || n + (size_t)w >= cap) return 0;
    n += (size_t)w;
    for (int i = 0; i < DR32_PADS; i++) {
        if (i) {
            if (n + 2 >= cap) return 0;
            out[n++] = ','; out[n++] = ' ';
        }
        int m = append_pad_name(&kit->pads[i], out + n, (int)(cap - n));
        if (m <= 0) return 0;
        n += (size_t)m;
    }
    w = snprintf(out + n, cap - n, "], ");
    if (w <= 0 || n + (size_t)w >= cap) return 0;
    return n + (size_t)w;
}

/**
 * Rebuild the served hierarchy from module.json's text with the current kit's
 * pad names spliced in as `child_names`.
 *
 * The anchor is a level's `child_index_param` declaration (module.json is ours,
 * so the key is guaranteed present) and the array goes immediately before it.
 * String surgery rather than a JSON writer: the document is otherwise verbatim,
 * and a re-serialised copy would be a second thing that could drift from the
 * file the tests and the docs read.
 *
 * ⚠ EVERY anchor, not the first. The pad knobs are three sibling child levels
 * — Sample / Shape / Mix — and each names the pads it draws. Splicing only the
 * first left Shape and Mix reading "Pad 7" while Sample said "Kick 707", which
 * is not a page that looks broken; it is one that looks like a different pad.
 */
static void dr32_refresh_hierarchy(dr32_instance *in) {
    if (!in->ui_hierarchy_src) return;
    const char *src = in->ui_hierarchy_src;
    size_t src_len = strlen(src);
    /* 32 names x up to DR32_MAX_PATH is the pathological bound, times one copy
     * per pad level; real kits are ~20 bytes a name. The value channel is
     * 64 KB, so cap the whole thing there and fall back to the plain document
     * if the names would not fit. */
    const size_t cap = 65536;
    char *out = malloc(cap);
    if (!out) return;

    static const char ANCHOR[] = "\"child_index_param\"";
    const size_t alen = sizeof(ANCHOR) - 1;
    size_t n = 0, at = 0;
    int ok = 1;
    for (;;) {
        const char *anchor = strstr(src + at, ANCHOR);
        if (!anchor) break;
        size_t head = (size_t)(anchor - (src + at));
        if (n + head >= cap) { ok = 0; break; }
        memcpy(out + n, src + at, head);
        n += head;
        at += head;
        size_t after = append_child_names(&in->kit, out, n, cap);
        if (!after) { ok = 0; break; }
        n = after;
        /* ⚠ STEP PAST THE ANCHOR, not just up to it. Resuming the search AT
         * the anchor finds the same one again, and the loop re-splices into
         * its own output until the buffer fills — which fails soft, as the
         * plain document, so the only symptom is names that never appear. */
        if (n + alen >= cap) { ok = 0; break; }
        memcpy(out + n, src + at, alen);
        n += alen;
        at += alen;
    }
    if (ok) {
        size_t tail = src_len - at;
        if (n + tail >= cap) ok = 0;
        else { memcpy(out + n, src + at, tail); n += tail; out[n] = '\0'; }
    }

    if (!ok) {
        if (src_len >= cap) { free(out); return; }
        memcpy(out, src, src_len + 1);
        n = src_len;
    }
    free(in->ui_hierarchy);
    in->ui_hierarchy = out;
    in->ui_hierarchy_len = (int)n;
}

/** Pull the ui_hierarchy object out of our own module.json, so the UI contract
 *  has exactly one source. Returns a malloc'd string or NULL. */
static char *load_ui_hierarchy(const char *module_dir, int *out_len) {
    if (!module_dir) return NULL;
    char path[DR32_MAX_PATH];
    snprintf(path, sizeof(path), "%s/module.json", module_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (1 << 20)) { fclose(f); return NULL; }

    char *json = (char *)malloc((size_t)size + 1);
    if (!json) { fclose(f); return NULL; }
    size_t n = fread(json, 1, (size_t)size, f);
    json[n] = '\0';
    fclose(f);

    const char *tag = strstr(json, "\"ui_hierarchy\"");
    if (!tag) { free(json); return NULL; }
    const char *start = strchr(tag + 14, '{');
    if (!start) { free(json); return NULL; }
    int depth = 1;
    const char *end = start + 1;
    while (*end && depth > 0) {
        if (*end == '{') depth++;
        else if (*end == '}') depth--;
        end++;
    }
    if (depth != 0) { free(json); return NULL; }

    int len = (int)(end - start);
    char *out = (char *)malloc((size_t)len + 1);
    if (!out) { free(json); return NULL; }
    memcpy(out, start, (size_t)len);
    out[len] = '\0';
    free(json);
    if (out_len) *out_len = len;
    return out;
}

static void logmsg(const char *s) {
    if (g_host && g_host->log) g_host->log(s);
}

/* Is anything sequencing? Decides whether a bare note-on may move the editor's
 * focus (dr32_kit.h, transport_running). get_beat_position is < 0 whenever no
 * transport runs and is the drift-free source; the clock status is the
 * fallback for a host that predates it. Both pointers may be NULL on an old
 * host, in which case we assume STOPPED -- the follow then behaves as the
 * canvas era's "every note" did, which is the state DR32 shipped in for weeks;
 * the alternative (assume running) would leave focus dead on such a host with
 * no vouch to move it.
 *
 * Asked at EVERY note-on, not only per render block: the host hands a block's
 * MIDI to on_midi BEFORE rendering it, and a slot parked as idle during
 * silence renders nothing at all -- so a value mirrored only in render_block
 * was a block, or a whole rest, stale on exactly the note that mattered (the
 * first hit after Play). Two function calls, no I/O; fine on the audio thread. */
static void dr32_sync_transport(dr32_instance *in) {
    int running = 0;
    if (g_host && g_host->get_beat_position) {
        running = g_host->get_beat_position() >= 0.0;
    } else if (g_host && g_host->get_clock_status) {
        running = g_host->get_clock_status() == MOVE_CLOCK_STATUS_RUNNING;
    }
    in->kit.transport_running = running;
}

// ------------------------------------------------------------------ helpers

/* Kit loaded on a fresh instance, so the module arrives making a sound rather
 * than as 32 empty pads. Core Library, not the user library — it ships with
 * every Move, so this resolves on any device.
 *
 * Missing file = silently skipped, not an error: a user may have pruned the
 * Core Library, and an empty rack is a perfectly valid state to open in. A slot
 * restoring saved state overwrites this a moment later, which is only the cost
 * of one kit load. */

// ------------------------------------------------------------------ v2 API

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    dr32_instance *in = (dr32_instance *)calloc(1, sizeof(dr32_instance));
    if (!in) return NULL;
    dr32_kit_init(&in->kit);
    /* Empty, and NOT scanned here: create_instance is on the SPI callback too,
     * so walking ~442 files at this point would stall the load. The catalogue
     * fills in from the browser's own reads — see get_param. */
    in->kits = dr32_kits_create();
    in->kit_pending = -1;
    in->ui_hierarchy_src = load_ui_hierarchy(module_dir, &in->ui_hierarchy_len);
    dr32_refresh_hierarchy(in);
    if (in->ui_hierarchy) {
        char msg[128];
        snprintf(msg, sizeof(msg), "dr32: instance created (ui_hierarchy %d bytes)",
                 in->ui_hierarchy_len);
        logmsg(msg);
    } else {
        logmsg("dr32: instance created (NO ui_hierarchy — UI will be empty)");
    }

    /*
     * NO DEFAULT KIT — DR32 opens EMPTY (Josh, 2026-09-09).
     *
     * It used to load the 707 from the Core Library at create. An instrument
     * that arrives already full decides for you, and every new slot then starts
     * by undoing that choice; an empty rack is the honest starting point and it
     * is also the faster one, since create_instance is on the SPI callback and
     * a kit load reads up to 32 WAVs there.
     *
     * The baseline is still captured. It is what makes the state blob carry
     * only the user's DELTAS, and an empty kit is a perfectly good baseline —
     * every pad the user fills is a delta from it. Skipping this would leave
     * state_baseline NULL, which degrades to a full dump: correct, but larger
     * for no reason.
     */
    dr32_capture_baseline(in);
    return in;
}

static void destroy_instance(void *instance) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in) return;
    dr32_kit_free(&in->kit);
    dr32_kits_destroy(in->kits);
    free(in->ui_hierarchy_src);
    free(in->ui_hierarchy);
    free(in->state_baseline);
    free(in);
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    /* `source` is deliberately unread — see the note below. Kept in the
     * signature because plugin_api_v2 defines it. */
    (void)source;
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || len < 3) return;
    uint8_t status = msg[0] & 0xF0;

    if (status == 0x90 && msg[2] > 0) {
        dr32_sync_transport(in);
        /* Focus-follow is decided inside dr32_kit_note_on: outright while no
         * transport runs, otherwise only for a note a host has vouched for. MEASURED on device: a live pad hit and a
         * sequenced note reach on_midi with identical status, channel, note and
         * source (both report EXTERNAL, not INTERNAL — an earlier version gated
         * on INTERNAL and silently killed focus-follow entirely). The canvas
         * supplies the one missing bit via ui_live_press; the note supplies the
         * pad. Neither alone is enough, which is why it is not decided here. */
        dr32_kit_note_on(&in->kit, msg[1], msg[2]);
    } else if (status == 0x80 || (status == 0x90 && msg[2] == 0)) {
        dr32_kit_note_off(&in->kit, msg[1]);
    } else if (status == 0xB0 && msg[1] == 123) {   // all notes off
        dr32_kit_all_off(&in->kit);
    }
}

/* State restore loads its kit through the ordinary "kit" entry point below,
 * rather than calling dr32_preset_load itself. That path already owns preset
 * loading, kit_path bookkeeping, the error string, the dirty flag and the
 * timing log — a second copy would drift from it, and the drift would be
 * invisible until a restored slot behaved subtly unlike a freshly loaded one. */
static void set_param(void *instance, const char *key, const char *val);
static void dr32_state_load_kit_cb(void *ctx, const char *path) {
    set_param(ctx, "kit", path);
}

static void set_param(void *instance, const char *key, const char *val) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || !key || !val) return;

    // The kit path is host-side state, not engine state, so it is handled here;
    // everything else goes through the shared dispatch.
    // The Kit menu is a PICKER of two roots: "Move" browses the Core Library's
    // drum kits, "User" the user library. The filepath type takes exactly one
    // root, so they are two params that mean the same thing.
    /*
     * The Kits browser: a category list, then that category's kits.
     *
     * Both are the host's own page kinds and need no host change —
     * `items_param`/`select_param` for the categories, the
     * `list_param`/`count_param`/`name_param` triple for the kits, joined by
     * `navigate_to`. The host never asks for a LIST of kits: it writes an index
     * and reads back the name at that index, so the cost is constant however
     * many kits exist.
     *
     * ⚠ Writing kit_index LOADS. That is the page kind's contract, not a
     * choice: it auditions as you scroll and there is no cancel — the host
     * offers no `live_preview` or `browser_hooks` here. See the board item.
     */
    if (!strcmp(key, "kit_cat")) {
        int v = atoi(val);
        int n = dr32_kits_cat_count(in->kits);
        in->kit_cat = (v < 0) ? 0 : (n > 0 && v >= n ? n - 1 : v);
        in->kit_idx = 0;
        return;
    }
    if (!strcmp(key, "kit_index")) {
        int v = atoi(val);
        int n = dr32_kits_count(in->kits, in->kit_cat);
        if (n <= 0) return;
        if (v < 0) v = 0;
        if (v >= n) v = n - 1;
        in->kit_idx = v;
        /* Cursor only — the load is deferred until the cursor settles. */
        in->kit_pending = v;
        in->kit_pending_at = in->kit.block;
        return;
    }

    if (!strcmp(key, "kit") || !strcmp(key, "kit_move") || !strcmp(key, "kit_user")) {
        snprintf(in->kit_path, sizeof(in->kit_path), "%s", val);
        // Load HERE, on the host thread. This used to raise a dirty flag for
        // ui.js to notice, but that file never runs in a chain slot, so the
        // kit was never actually loaded and the module was silent.
        if (!val[0]) {
            // An empty path is the host's cancel-restore when it has no original
            // value to give back. Not an error, and not a reason to unload.
            return;
        }
        dr32_preset_report rep;
        // Timed because the kit browser previews live: every cursor move parses
        // a preset and loads up to 32 samples, so this cost is felt directly
        // while scrolling.
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int ok = dr32_preset_load(&in->kit, val, &rep);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        char msg[DR32_MAX_PATH + 200];
        if (ok) {
            // Clear any previous error. Without this a single failed load stuck
            // a "could not load kit" warning on the synth forever, including on
            // every later re-entry into the module.
            in->err[0] = '\0';
            dr32_capture_baseline(in);
            snprintf(msg, sizeof(msg),
                     "dr32: kit '%s' loaded in %.1f ms — %d pads, %d samples, %d empty, %d unresolved, %d failed",
                     val, ms, rep.pads, rep.loaded, rep.empty, rep.unresolved, rep.failed);
        } else {
            snprintf(msg, sizeof(msg), "dr32: kit '%s' FAILED to load", val);
            snprintf(in->err, sizeof(in->err), "could not load kit: %s", val);
        }
        logmsg(msg);
        return;
    }

    if (!strcmp(key, "kit_mark")) {
        // Browser opened: remember what was loaded. Cleared on commit.
        snprintf(in->kit_saved, sizeof(in->kit_saved), "%s",
                 atoi(val) ? in->kit_path : "");
        return;
    }
    if (!strcmp(key, "kit_restore")) {
        if (in->kit_saved[0] && strcmp(in->kit_saved, in->kit_path) != 0) {
            dr32_preset_report rep;
            char saved[DR32_MAX_PATH];
            snprintf(saved, sizeof(saved), "%s", in->kit_saved);
            if (dr32_preset_load(&in->kit, saved, &rep)) {
                snprintf(in->kit_path, sizeof(in->kit_path), "%s", saved);
                dr32_capture_baseline(in);
                char msg[DR32_MAX_PATH + 200];
                snprintf(msg, sizeof(msg), "dr32: kit preview cancelled — restored '%s'", saved);
                logmsg(msg);
            }
        }
        in->kit_saved[0] = '\0';
        return;
    }

    if (!strcmp(key, "state")) {
        // Schwung restoring a saved slot. The kit is loaded through the same
        // path set_param("kit") uses (via the callback below) so preset loading
        // and kit_path bookkeeping stay in ONE place.
        dr32_state_read(&in->kit, val, dr32_state_load_kit_cb, in);
        return;
    }

    dr32_apply_param(&in->kit, key, val);

    /* A per-pad sample swap (browser, or a browse step) renames that pad in
     * the served hierarchy. Cheap — one 32 KB copy — and the host only re-reads
     * the contract on a settle, so this is never per-frame work. */
    size_t kl = strlen(key);
    if ((kl >= 7 && !strcmp(key + kl - 7, "_sample")) ||
        (kl >= 7 && !strcmp(key + kl - 7, "_browse"))) {
        dr32_refresh_hierarchy(in);
        /* Re-serving is only half of it: the host has to come back and READ.
         * Arm the is_loading pulse, and re-arm on every step so a sweep of the
         * browse knob costs one re-read rather than one per detent. */
        in->names_dirty = 1;
        in->names_dirty_at = in->kit.block;
    }
}

/*
 * How long `is_loading` stays "1" after a pad's sample changes.
 *
 * ⚠ THIS IS A LOWER BOUND ON THE HOST'S POLL, NOT A LOAD TIME. Nothing is
 * loading: the swap already happened, synchronously, in set_param. The pulse
 * exists so the host SEES a 1 -> 0 edge, and it only sees one if at least one
 * poll lands inside the window. The shadow grid polls is_loading every
 * LOADING_POLL_TICKS = 8 frames (~133 ms at 60 Hz), so 120 blocks
 * (120 x 2.902 ms = 348 ms) fits two polls with room for a slow frame.
 */
#define DR32_NAMES_SETTLE_BLOCKS 120

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || !key || !buf || buf_len <= 0) return 0;

    /*
     * ⭑ THE PAD NAMES REFRESH BECAUSE OF THIS KEY, AND ONLY BECAUSE OF IT.
     *
     * dr32_refresh_hierarchy re-serves the names the moment a sample changes,
     * but the host does not re-read `ui_hierarchy` on a knob turn or a
     * filepath commit — armContractSettle is called for a SELECTION (an items
     * row, a preset step), never for either of those. The one module-side
     * lever is `is_loading`: the shadow grid polls it and calls
     * reloadIfChanged on the loading -> ready edge
     * (shadow_ui_param_pages.mjs, "Only re-plan on the loading->ready edge").
     * So a sample swap fakes exactly one such edge and the header follows.
     *
     * ⚠ ANSWER IT ALWAYS, AND ONLY EVER "1" OR "0". An unserved key reads ""
     * and the host stops asking FOR THE LIFE OF THE COMPONENT
     * (_loadingInterval = Infinity) — one "" and this never works again. The
     * controller's own probe (isLoadingSays) is stricter still: anything but
     * "1"/"0" sets isLoadingSupported = false permanently.
     *
     * Cost of answering: one extra param read per 8 frames while a DR32 page
     * is on screen, which is why this is two integer compares and no more.
     */
    if (!strcmp(key, "is_loading")) {
        if (in->names_dirty) {
            if (in->kit.block - in->names_dirty_at < DR32_NAMES_SETTLE_BLOCKS)
                return snprintf(buf, buf_len, "1");
            /* Window elapsed: report ready ONCE and disarm. If the page was
             * closed through the whole window this is the first read, the host
             * never saw a "1", and no reload fires — correct, because opening
             * a page reads the hierarchy anyway. */
            in->names_dirty = 0;
        }
        return snprintf(buf, buf_len, "0");
    }

    if (!strcmp(key, "ui_hierarchy")) {
        if (!in->ui_hierarchy || in->ui_hierarchy_len >= buf_len) return 0;
        memcpy(buf, in->ui_hierarchy, (size_t)in->ui_hierarchy_len + 1);
        return in->ui_hierarchy_len;
    }
    /*
     * The voices this kit can render separately, in the module's own declared
     * order — the INDEX HERE IS THE voice_out[] INDEX handed to
     * move_plugin_render_split, so this list and that loop must stay in step.
     *
     * ⚠ ALL 32 PADS ARE LISTED, INCLUDING EMPTY ONES, and that is deliberate:
     * the index is a render-buffer index, so dropping the empty pads would
     * shift every pad behind them onto the wrong buffer. An empty pad simply
     * renders silence, exactly as it does today.
     *
     * 🔴 THE ID IS THE PAD'S OWN PARAM PREFIX, AND IT IS 1-BASED: `pad1`..`pad32`.
     *
     * That is not cosmetic and it was wrong until 2026-09-08. `voice_send_params`
     * below publishes the TEMPLATE `{id}_send_a`, and the host substitutes each
     * id verbatim to read a real parameter off this module — so an id must be
     * exactly what dr32_params.c's split_pad_key() parses, which counts from
     * ZERO (`pad0_attack`). Published as `pad1`..`pad32`, every send level would
     * have landed on the pad NEXT DOOR and `pad32_send_a` would have addressed
     * nothing at all, silently: nothing in the host errors on a key that does
     * not resolve. Upstream's own docs name this exact hazard and say it is the
     * module's to reconcile, not the host's to guess.
     *
     * Ids are stable across content changes and the LABELS follow whatever
     * sample is loaded, so a saved bus assignment survives a kit change. An
     * empty pad falls back to "Pad N" (1-based, matching the pads the user's
     * hands are on) rather than to its id; a row reading "" would be
     * unclickable on the host's screen.
     *
     * A host that does not know about buses never asks for this key, which is
     * half of why this feature is inert on stock.
     */
    if (!strcmp(key, "split_voices")) {
        int n = 0;
        int w = snprintf(buf, buf_len, "[");
        if (w <= 0 || w >= buf_len) return 0;
        n = w;
        for (int i = 0; i < DR32_PADS; i++) {
            if (i) {
                if (n + 1 >= buf_len) return 0;
                buf[n++] = ',';
            }
            w = snprintf(buf + n, buf_len - n, "{\"id\":\"pad%d\",\"label\":", i + 1);
            if (w <= 0 || n + w >= buf_len) return 0;
            n += w;
            int m = in->kit.pads[i].path[0]
                  ? append_pad_name(&in->kit.pads[i], buf + n, buf_len - n)
                  : snprintf(buf + n, buf_len - n, "\"Pad %d\"", i + 1);
            if (m <= 0 || n + m >= buf_len) return 0;
            n += m;
            if (n + 1 >= buf_len) return 0;
            buf[n++] = '}';
        }
        if (n + 2 > buf_len) return 0;
        buf[n++] = ']';
        buf[n] = '\0';
        return n;
    }
    /*
     * WHERE EACH VOICE'S SEND LEVEL LIVES — the third, optional half of the
     * module-bus contract (upstream >= 1.3.0, docs/MODULES.md).
     *
     * A voice's send level is OURS. The host reads it; it does not own it, does
     * not draw a fader for it and does not save it — these levels are already
     * in our own `state` blob and on our own pad pages, and they arrive from a
     * Move kit's per-pad send amounts, which is what they have always meant on
     * the hardware.
     *
     * ARRAY POSITION IS THE SEND INDEX: [0] is Send A, [1] is Send B. More than
     * two is refused outright by the host rather than quietly truncated.
     *
     * `{id}` is substituted verbatim, so this resolves to `pad0_send_a` ...
     * `pad31_send_b` — see the id note above, which is the whole reason the
     * split_voices ids had to become 0-based.
     *
     * ⚠ The host reads these keys ON THE AUDIO CALLBACK, a few per frame. Keep
     * the answer a constant: no allocation, no formatting, no work.
     *
     * ⚠ The pads level must keep declaring `send_a`/`send_b` with `min`, `max`
     * and `unit: "dB"`. That is where the host gets the scale from, and if it
     * cannot find it, it REFUSES the send rather than guessing — nothing is
     * heard, and nothing is mis-scaled.
     *
     * A host below 1.3.0 never asks for this key, and there is no internal
     * return left for the levels to feed, so on such a host the two per-pad
     * send knobs simply do nothing. That is the trade this change accepted.
     */
    if (!strcmp(key, "voice_send_params"))
        return snprintf(buf, buf_len, "[\"{id}_send_a\",\"{id}_send_b\"]");
    /*
     * The Kits browser's reads. Every one of these PUMPS the catalogue a little
     * first: the host polls this page (count -> index -> name, one read per
     * tick), so its own polling drives the scan to completion over a few dozen
     * ticks while the user is looking at the page. A budget of 24 entries keeps
     * any single call far inside the ~900us SPI budget; scanning all ~442 files
     * in one call would drop frames, which is the whole reason the catalogue is
     * incremental. Once complete, pump() returns immediately.
     */
    if (!strncmp(key, "kit_", 4) &&
        (!strcmp(key, "kit_cat_items") || !strcmp(key, "kit_cat") ||
         !strcmp(key, "kit_count") || !strcmp(key, "kit_index") ||
         !strcmp(key, "kit_name"))) {
        dr32_kits_pump(in->kits, 24);
        /* Once the scan finishes, point the cursor at the kit that is actually
         * loaded. Otherwise the browser opens at entry 0 and the first detent
         * loads something the user did not ask for — on a page that auditions
         * with no undo, that is the difference between a browser and a trap. */
        if (dr32_kits_ready(in->kits) && !in->kit_located) {
            int c = 0, i = 0;
            if (dr32_kits_locate(in->kits, in->kit_path, &c, &i) == 0) {
                in->kit_cat = c;
                in->kit_idx = i;
            }
            in->kit_located = 1;
        }

        if (!strcmp(key, "kit_cat_items")) {
            int n = dr32_kits_cat_count(in->kits), w = 0;
            w += snprintf(buf + w, buf_len - w, "[");
            for (int i = 0; i < n && w < buf_len; i++)
                w += snprintf(buf + w, buf_len - w, "%s{\"index\":%d,\"label\":\"%s\"}",
                              i ? "," : "", i, dr32_kits_cat_name(in->kits, i));
            if (w < buf_len) w += snprintf(buf + w, buf_len - w, "]");
            return w;
        }
        if (!strcmp(key, "kit_cat"))   return snprintf(buf, buf_len, "%d", in->kit_cat);
        if (!strcmp(key, "kit_count")) return snprintf(buf, buf_len, "%d",
                                                       dr32_kits_count(in->kits, in->kit_cat));
        if (!strcmp(key, "kit_index")) return snprintf(buf, buf_len, "%d", in->kit_idx);
        /* kit_name — an array lookup, never disk. REALTIME_SAFETY names
         * "get_param that rescans a directory" as a known budget-blower, and
         * this is the key the host reads most often. */
        return snprintf(buf, buf_len, "%s",
                        dr32_kits_name(in->kits, in->kit_cat, in->kit_idx));
    }

    if (!strcmp(key, "kit") || !strcmp(key, "kit_move") || !strcmp(key, "kit_user"))
        return snprintf(buf, buf_len, "%s", in->kit_path);
    // The blob Schwung stores in the set's slot_N.json. Without this the host
    // has nothing to persist and a DR32 slot comes back empty after a reboot.
    // The baseline keeps the blob to the user's EDITS — see dr32_state.h for
    // why size matters here (a host-side cap has silently dropped a full dump).
    if (!strcmp(key, "state"))
        return dr32_state_write(&in->kit, in->kit_path, buf, buf_len,
                                in->state_baseline);

    return dr32_read_param(&in->kit, key, buf, buf_len);
}

static int get_error(void *instance, char *buf, int buf_len) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || !in->err[0]) return 0;
    return snprintf(buf, buf_len, "%s", in->err);
}

/*
 * DEFERRED AUDITION — why the kit list does not load on every detent.
 *
 * The preset page auditions unconditionally: a detent writes kit_index and the
 * host offers no commit signal, so "load what is selected" is the only contract
 * available. Loading on EVERY detent measured 5.3-10.1 ms per step on the SPI
 * callback (device, 2026-09-09) against a 2.9 ms block — a couple of dropped
 * frames per detent, continuously, for as long as you scroll.
 *
 * So a detent moves the CURSOR only, and the load happens once the cursor has
 * been still for DR32_KIT_SETTLE_BLOCKS. Scrolling past twenty kits now costs
 * one load instead of twenty, and the one it costs lands where the user has
 * stopped — which is also the only kit they actually asked to hear.
 *
 * ⚠ This does not make the load cheap, and it is not meant to: it makes it
 * happen ONCE. The cost is inherent — a kit parses a preset and reads up to 32
 * WAVs — and the decode memo already covers the repeat case.
 *
 * ⚠ Serviced from render_block because that is the only thing that runs on a
 * clock. set_param and render_block are the same thread, so this does not move
 * the work off the callback; it removes the repetition.
 */
#define DR32_KIT_SETTLE_BLOCKS 60   /* ~174 ms at 2.902 ms/block */

static void dr32_service_pending_kit(dr32_instance *in) {
    if (in->kit_pending < 0) return;
    if (in->kit.block - in->kit_pending_at < DR32_KIT_SETTLE_BLOCKS) return;

    int idx = in->kit_pending;
    in->kit_pending = -1;
    const char *path = dr32_kits_path(in->kits, in->kit_cat, idx);
    if (path && path[0]) set_param(in, "kit", path);
}

static void render_block(void *instance, int16_t *out, int frames) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in) return;
    if (frames > 1024) frames = 1024;

    dr32_sync_transport(in);
    dr32_service_pending_kit(in);

    dr32_kit_render(&in->kit, in->scratch, frames);

    for (int i = 0; i < 2 * frames; i++) {
        float v = in->scratch[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = (int16_t)(v * 32767.0f);
    }
}

/*
 * ============================================================================
 * move_plugin_render_split — the OPTIONAL per-voice render (Schwung >= 1.3.0)
 * ============================================================================
 *
 * ⭐ OPT-IN FROM BOTH ENDS, WHICH IS WHY THIS IS SAFE ON STOCK. A host that
 * does not know about buses never dlsym's this symbol and never asks for
 * `split_voices`, so DR32 goes on rendering through render_block exactly as it
 * always has. Nothing here changes the module's behaviour on a host that does
 * not use it — that matters because DR32 must run on stock Schwung AND under
 * dAVEBOx, and only one of those has module buses today.
 *
 * A SEPARATE EXPORTED SYMBOL, not a field appended to plugin_api_v2_t:
 * extending that struct once boot-looped a device, because a host built against
 * the shorter version reads past what the module allocated. dlsym is
 * present-or-absent with no ABI question at all.
 *
 * The host switches between this and render_block AT RUNTIME, PER FRAME, by
 * whether any voice is currently assigned to a bus — so the two share the kit,
 * its voices and their envelopes by construction, and neither may hold state
 * the other does not advance.
 */
void move_plugin_render_split(void *instance, int16_t *const *voice_out,
                              int n_voices, int16_t *main_out, int frames) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in) return;
    if (frames > 1024) frames = 1024;

    /* The same per-block housekeeping render_block does, and it may not be
     * skipped on this path: the transport sync drives choke/retrigger, and a
     * kit that only saw it on one of its two entry points would drift the
     * moment a voice was assigned to a bus. */
    dr32_sync_transport(in);

    dr32_kit_render_split(&in->kit, voice_out, n_voices, main_out, frames);
}

static plugin_api_v2_t g_api = {
    MOVE_PLUGIN_API_VERSION_2,
    create_instance,
    destroy_instance,
    on_midi,
    set_param,
    get_param,
    get_error,
    render_block,
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
