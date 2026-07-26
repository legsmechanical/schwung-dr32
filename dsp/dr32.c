// dr32.c — Schwung plugin entry (API v2) for DR32.
//
// The JS side parses .ablpreset files (lib/ablpreset.mjs) and pushes the kit
// down as flat params: pad<N>_<key>. The DSP never parses JSON — that keeps the
// format knowledge in one place and the audio side dumb and fast.

#include "host/plugin_api_v1.h"
#include "dr32_kit.h"
#include "dr32_params.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} dr32_instance;

static void logmsg(const char *s) {
    if (g_host && g_host->log) g_host->log(s);
}

// ------------------------------------------------------------------ helpers

/** Parse "pad12_attack" -> pad index 12, key "attack". Returns -1 if not a pad key. */
static int split_pad_key(const char *key, const char **rest) {
    if (strncmp(key, "pad", 3) != 0) return -1;
    const char *p = key + 3;
    int idx = 0, digits = 0;
    while (*p >= '0' && *p <= '9') { idx = idx * 10 + (*p - '0'); p++; digits++; }
    if (!digits || *p != '_') return -1;
    *rest = p + 1;
    return (idx >= 0 && idx < DR32_PADS) ? idx : -1;
}

// ------------------------------------------------------------------ v2 API

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir; (void)json_defaults;
    dr32_instance *in = (dr32_instance *)calloc(1, sizeof(dr32_instance));
    if (!in) return NULL;
    dr32_kit_init(&in->kit);
    logmsg("dr32: instance created");
    return in;
}

static void destroy_instance(void *instance) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in) return;
    dr32_kit_free(&in->kit);
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
        in->kit_dirty = 1;
        dr32_kit_all_off(&in->kit);
        return;
    }
    if (!strcmp(key, "kit_dirty")) { in->kit_dirty = atoi(val); return; }

    dr32_apply_param(&in->kit, key, val);
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    dr32_instance *in = (dr32_instance *)instance;
    if (!in || !key || !buf || buf_len <= 0) return 0;

    const char *sub;
    int pad = split_pad_key(key, &sub);
    if (pad >= 0) {
        dr32_pad_slot *s = &in->kit.pads[pad];
        if (!strcmp(sub, "sample")) return snprintf(buf, buf_len, "%s", s->path);
        if (!strcmp(sub, "loaded")) return snprintf(buf, buf_len, "%d", s->sample ? 1 : 0);
        if (!strcmp(sub, "frames")) return snprintf(buf, buf_len, "%zu", s->frames);
        if (!strcmp(sub, "note"))   return snprintf(buf, buf_len, "%d", s->note);
        return 0;
    }
    if (!strcmp(key, "voices"))    return snprintf(buf, buf_len, "%d", dr32_kit_active_voices(&in->kit));
    if (!strcmp(key, "kit"))       return snprintf(buf, buf_len, "%s", in->kit_path);
    if (!strcmp(key, "kit_dirty")) return snprintf(buf, buf_len, "%d", in->kit_dirty);
    return 0;
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
