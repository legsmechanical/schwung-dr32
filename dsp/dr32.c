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

/** Rebuild the served hierarchy from module.json's text with the current kit's
 *  pad names spliced in as `child_names` on the pads level. The anchor is the
 *  level's `child_index_param` declaration (module.json is ours, so the key is
 *  guaranteed present); the array is inserted immediately before it. String
 *  surgery rather than a JSON writer: the document is otherwise verbatim, and
 *  a re-serialised copy would be a second thing that could drift from the
 *  file the tests and the docs read. */
static void dr32_refresh_hierarchy(dr32_instance *in) {
    if (!in->ui_hierarchy_src) return;
    const char *src = in->ui_hierarchy_src;
    const char *anchor = strstr(src, "\"child_index_param\"");
    size_t src_len = strlen(src);
    /* 32 names × up to DR32_MAX_PATH is the pathological bound; real kits are
     * ~20 bytes a name. The value channel is 64 KB, so cap the whole thing
     * there and fall back to the plain document if names would not fit. */
    const size_t cap = 65536;
    char *out = malloc(cap);
    if (!out) return;
    size_t n = 0;
    int ok = 0;
    if (anchor && src_len < cap) {
        size_t head = (size_t)(anchor - src);
        memcpy(out, src, head);
        n = head;
        int w = snprintf(out + n, cap - n, "\"child_names\": [");
        if (w > 0 && n + (size_t)w < cap) {
            n += (size_t)w;
            ok = 1;
            for (int i = 0; i < DR32_PADS && ok; i++) {
                if (i && n + 2 < cap) { out[n++] = ','; out[n++] = ' '; }
                int m = append_pad_name(&in->kit.pads[i], out + n, (int)(cap - n));
                if (m <= 0) ok = 0; else n += (size_t)m;
            }
            if (ok) {
                w = snprintf(out + n, cap - n, "], ");
                if (w <= 0 || n + (size_t)w >= cap) ok = 0; else n += (size_t)w;
            }
            if (ok) {
                size_t tail = src_len - head;
                if (n + tail >= cap) ok = 0;
                else { memcpy(out + n, anchor, tail); n += tail; out[n] = '\0'; }
            }
        }
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

// ------------------------------------------------------------------ helpers

/* Kit loaded on a fresh instance, so the module arrives making a sound rather
 * than as 32 empty pads. Core Library, not the user library — it ships with
 * every Move, so this resolves on any device.
 *
 * Missing file = silently skipped, not an error: a user may have pruned the
 * Core Library, and an empty rack is a perfectly valid state to open in. A slot
 * restoring saved state overwrites this a moment later, which is only the cost
 * of one kit load. */
#define DR32_DEFAULT_KIT "/data/CoreLibrary/Track Presets/Drums/Electronic/707 Kit.json"

// ------------------------------------------------------------------ v2 API

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    dr32_instance *in = (dr32_instance *)calloc(1, sizeof(dr32_instance));
    if (!in) return NULL;
    dr32_kit_init(&in->kit);
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

    dr32_preset_report rep;
    if (dr32_preset_load(&in->kit, DR32_DEFAULT_KIT, &rep)) {
        snprintf(in->kit_path, sizeof(in->kit_path), "%s", DR32_DEFAULT_KIT);
        dr32_capture_baseline(in);
        char msg[DR32_MAX_PATH + 120];
        snprintf(msg, sizeof(msg), "dr32: default kit loaded — %d pads, %d samples",
                 rep.pads, rep.loaded);
        logmsg(msg);
    }
    return in;
}

static void destroy_instance(void *instance) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in) return;
    dr32_kit_free(&in->kit);
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
        /* Focus-follow is decided inside dr32_kit_note_on, and only for a note
         * the canvas has vouched for. MEASURED on device: a live pad hit and a
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
        (kl >= 12 && !strcmp(key + kl - 12, "_sample_move")) ||
        (kl >= 12 && !strcmp(key + kl - 12, "_sample_user")) ||
        (kl >= 7 && !strcmp(key + kl - 7, "_browse")))
        dr32_refresh_hierarchy(in);
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || !key || !buf || buf_len <= 0) return 0;

    if (!strcmp(key, "ui_hierarchy")) {
        if (!in->ui_hierarchy || in->ui_hierarchy_len >= buf_len) return 0;
        memcpy(buf, in->ui_hierarchy, (size_t)in->ui_hierarchy_len + 1);
        return in->ui_hierarchy_len;
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

static void render_block(void *instance, int16_t *out, int frames) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in) return;
    if (frames > 1024) frames = 1024;

    /* Tempo for the synced Delay send. get_bpm has its own fallback chain and
     * documents 120 as the floor of it, but the POINTER may be NULL on an older
     * host, so it is guarded — and the kit early-outs on an unchanged value, so
     * this is a float compare per block rather than a recompute. */
    if (g_host && g_host->get_bpm) dr32_kit_set_bpm(&in->kit, g_host->get_bpm());

    /* Is anything sequencing? Decides whether a bare note-on may move the
     * editor's focus (dr32_kit.h, transport_running). get_beat_position is
     * < 0 whenever no transport runs and is the drift-free source; the clock
     * status is the fallback for a host that predates it. Both pointers may be
     * NULL on an old host, in which case we assume STOPPED — the follow then
     * behaves as the canvas era's "every note" did, which is the state DR32
     * shipped in for weeks; the alternative (assume running) would leave focus
     * dead on such a host with no vouch to move it. */
    {
        int running = 0;
        if (g_host && g_host->get_beat_position) {
            running = g_host->get_beat_position() >= 0.0;
        } else if (g_host && g_host->get_clock_status) {
            running = g_host->get_clock_status() == MOVE_CLOCK_STATUS_RUNNING;
        }
        in->kit.transport_running = running;
    }

    dr32_kit_render(&in->kit, in->scratch, frames);

    for (int i = 0; i < 2 * frames; i++) {
        float v = in->scratch[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = (int16_t)(v * 32767.0f);
    }
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
