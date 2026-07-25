// dr32.c — Schwung plugin entry (API v2) for DR32.
//
// The JS side parses .ablpreset files (lib/ablpreset.mjs) and pushes the kit
// down as flat params: pad<N>_<key>. The DSP never parses JSON — that keeps the
// format knowledge in one place and the audio side dumb and fast.

#include "host/plugin_api_v1.h"
#include "dr32_kit.h"

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

static int parse_filter_type(const char *v) {
    // The JSON's own spellings, measured on device. Accept the numeric form too
    // so the UI can send either.
    if (!strcmp(v, "Lowpass") || !strcmp(v, "0")) return DR32_FILT_LP24;
    if (!strcmp(v, "Lowpass 12dB") || !strcmp(v, "1")) return DR32_FILT_LP12;
    if (!strcmp(v, "Highpass") || !strcmp(v, "2")) return DR32_FILT_HP24;
    if (!strcmp(v, "Peak") || !strcmp(v, "3")) return DR32_FILT_PEAK;
    return DR32_FILT_LP24;
}

static int parse_mod_target(const char *v) {
    if (!strcmp(v, "Filter")) return DR32_MOD_FILTER;
    if (!strcmp(v, "Attack")) return DR32_MOD_ATTACK;
    if (!strcmp(v, "Hold"))   return DR32_MOD_HOLD;
    if (!strcmp(v, "Decay"))  return DR32_MOD_DECAY;
    if (!strcmp(v, "FX1"))    return DR32_MOD_FX1;
    if (!strcmp(v, "FX2"))    return DR32_MOD_FX2;
    return atoi(v);
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

    const char *sub;
    int pad = split_pad_key(key, &sub);
    if (pad >= 0) {
        dr32_pad_slot *s = &in->kit.pads[pad];
        dr32_pad *p = &s->params;
        float f = (float)atof(val);

        if      (!strcmp(sub, "sample"))        dr32_kit_load_sample(&in->kit, pad, val);
        else if (!strcmp(sub, "note"))          dr32_kit_set_note(&in->kit, pad, atoi(val));
        else if (!strcmp(sub, "choke"))         p->choke_group = atoi(val);
        else if (!strcmp(sub, "start"))         p->play_start = f;
        else if (!strcmp(sub, "length"))        p->play_length = f;
        else if (!strcmp(sub, "transpose"))     p->transpose = f;
        else if (!strcmp(sub, "detune"))        p->detune = f;
        else if (!strcmp(sub, "gain"))          p->gain = f;
        else if (!strcmp(sub, "volume"))        p->volume_db = f;
        else if (!strcmp(sub, "pan"))           p->pan = f;
        else if (!strcmp(sub, "vel_vol"))       p->vel_to_volume = f;
        else if (!strcmp(sub, "attack"))        p->attack = f;
        else if (!strcmp(sub, "hold"))          p->hold = f;
        else if (!strcmp(sub, "decay"))         p->decay = f;
        else if (!strcmp(sub, "env_mode"))      p->env_mode = (!strcmp(val, "A-S-R") || atoi(val) == 1)
                                                              ? DR32_ENV_ASR : DR32_ENV_AHD;
        else if (!strcmp(sub, "filter_on"))     p->filter_on = atoi(val) ? 1 : 0;
        else if (!strcmp(sub, "filter_type"))   p->filter_type = (dr32_filter_type)parse_filter_type(val);
        else if (!strcmp(sub, "cutoff"))        p->cutoff = f;
        else if (!strcmp(sub, "resonance"))     p->resonance = f;
        else if (!strcmp(sub, "peak_gain"))     p->peak_gain = f;
        else if (!strcmp(sub, "mod_target"))    p->mod_target = (dr32_mod_target)parse_mod_target(val);
        else if (!strcmp(sub, "mod_amount"))    p->mod_amount = f;
        else if (!strcmp(sub, "pitch_env"))     p->pitch_to_env = atoi(val) ? 1 : 0;
        else if (!strcmp(sub, "play"))          dr32_kit_note_on(&in->kit, s->note, atoi(val));
        return;
    }

    if (!strcmp(key, "kit")) {
        snprintf(in->kit_path, sizeof(in->kit_path), "%s", val);
        in->kit_dirty = 1;
        dr32_kit_all_off(&in->kit);
    }
    else if (!strcmp(key, "kit_dirty")) in->kit_dirty = atoi(val);
    else if (!strcmp(key, "master"))      in->kit.master_gain = (float)atof(val);
    else if (!strcmp(key, "panic"))  dr32_kit_all_off(&in->kit);
    else if (!strcmp(key, "clear")) {                       // reset the whole kit
        dr32_kit_all_off(&in->kit);
        for (int i = 0; i < DR32_PADS; i++) {
            dr32_kit_load_sample(&in->kit, i, NULL);
            dr32_pad_defaults(&in->kit.pads[i].params);
            dr32_kit_set_note(&in->kit, i, DR32_FIRST_NOTE + i);
        }
    }
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
