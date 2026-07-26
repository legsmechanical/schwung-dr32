/* clock_gettime/CLOCK_MONOTONIC are POSIX, not ISO C11, and we build -std=c11.
 * Use 200809L, not 199309L: the older level hides C99 functions like snprintf. */
#define _POSIX_C_SOURCE 200809L

// dr32.c — Schwung plugin entry (API v2) for DR32.
//
// The JS side parses .ablpreset files (lib/ablpreset.mjs) and pushes the kit
// down as flat params: pad<N>_<key>. The DSP never parses JSON — that keeps the
// format knowledge in one place and the audio side dumb and fast.

#include "host/plugin_api_v1.h"
#include "dr32_kit.h"
#include "dr32_params.h"
#include "dr32_preset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const host_api_v1_t *g_host = NULL;

typedef struct {
    dr32_kit kit;
    float    scratch[2 * 1024];   // float mix before int16 conversion
    char     err[256];
    // The kit path is set by the host's file browser (chain_params "kit"),
    // which calls set_param on the DSP. The DSP does NOT parse JSON — it just
    // records the path and raises kit_dirty; the UI polls that, parses the
    // .ablpreset with lib/ablpreset.mjs, and pushes the pads back down as
    // flat params. One place knows the format, and it isn't the audio side.
    char     kit_path[DR32_MAX_PATH];
    int      kit_dirty;
    // The Shadow UI asks the DSP for "ui_hierarchy" FIRST and only parses
    // module.json if we return <= 2 bytes (shadow_chain_mgmt.c). Serving it
    // ourselves takes that fallback — and any doubt about its brace-matching
    // extraction — out of the picture.
    char    *ui_hierarchy;
    int      ui_hierarchy_len;
    // Snapshot taken when the kit browser opens, so cancelling can put the
    // previous kit back. The host's own live_preview restore writes
    // `previewOriginalValue || ""`, and for this param that value is routinely
    // empty — an empty path cannot be loaded, so the previewed kit used to
    // stick after backing out.
    char     kit_saved[DR32_MAX_PATH];
} dr32_instance;

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

// ------------------------------------------------------------------ v2 API

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    dr32_instance *in = (dr32_instance *)calloc(1, sizeof(dr32_instance));
    if (!in) return NULL;
    dr32_kit_init(&in->kit);
    in->ui_hierarchy = load_ui_hierarchy(module_dir, &in->ui_hierarchy_len);
    if (in->ui_hierarchy) {
        char msg[128];
        snprintf(msg, sizeof(msg), "dr32: instance created (ui_hierarchy %d bytes)",
                 in->ui_hierarchy_len);
        logmsg(msg);
    } else {
        logmsg("dr32: instance created (NO ui_hierarchy — UI will be empty)");
    }
    return in;
}

static void destroy_instance(void *instance) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in) return;
    dr32_kit_free(&in->kit);
    free(in->ui_hierarchy);
    free(in);
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || len < 3) return;
    uint8_t status = msg[0] & 0xF0;
    if (status == 0x90 && msg[2] > 0) {
        dr32_kit_note_on(&in->kit, msg[1], msg[2]);
    } else if (status == 0x80 || (status == 0x90 && msg[2] == 0)) {
        dr32_kit_note_off(&in->kit, msg[1]);
    } else if (status == 0xB0 && msg[1] == 123) {   // all notes off
        dr32_kit_all_off(&in->kit);
    }
}

static void set_param(void *instance, const char *key, const char *val) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || !key || !val) return;

    // The kit path is host-side state, not engine state, so it is handled here;
    // everything else goes through the shared dispatch.
    if (!strcmp(key, "kit")) {
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
        char msg[360];
        if (ok) {
            // Clear any previous error. Without this a single failed load stuck
            // a "could not load kit" warning on the synth forever, including on
            // every later re-entry into the module.
            in->err[0] = '\0';
            snprintf(msg, sizeof(msg),
                     "dr32: kit '%s' loaded in %.1f ms — %d pads, %d samples, %d empty, %d unresolved, %d failed",
                     val, ms, rep.pads, rep.loaded, rep.empty, rep.unresolved, rep.failed);
        } else {
            snprintf(msg, sizeof(msg), "dr32: kit '%s' FAILED to load", val);
            snprintf(in->err, sizeof(in->err), "could not load kit: %s", val);
        }
        logmsg(msg);
        in->kit_dirty = 0;
        return;
    }
    if (!strcmp(key, "kit_dirty")) { in->kit_dirty = atoi(val); return; }

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
                char msg[360];
                snprintf(msg, sizeof(msg), "dr32: kit preview cancelled — restored '%s'", saved);
                logmsg(msg);
            }
        }
        in->kit_saved[0] = '\0';
        return;
    }

    dr32_apply_param(&in->kit, key, val);
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || !key || !buf || buf_len <= 0) return 0;

    if (!strcmp(key, "ui_hierarchy")) {
        if (!in->ui_hierarchy || in->ui_hierarchy_len >= buf_len) return 0;
        memcpy(buf, in->ui_hierarchy, (size_t)in->ui_hierarchy_len + 1);
        return in->ui_hierarchy_len;
    }
    if (!strcmp(key, "kit"))       return snprintf(buf, buf_len, "%s", in->kit_path);
    if (!strcmp(key, "kit_dirty")) return snprintf(buf, buf_len, "%d", in->kit_dirty);

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
