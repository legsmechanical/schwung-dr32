// dr32_params.c — the ONE place that maps flat string params onto the kit.
//
// Shared deliberately: the plugin (dsp/dr32.c) and the offline null-test
// renderer (tests/render_score.c) both go through this, so the thing we
// validate against the native engine is exactly the thing that ships. A second
// copy of this mapping would be free to drift out of agreement with the first.

#include "dr32_params.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    // Numeric values follow the NATIVE engine indices (0 LP12, 1 LP24, 2 HP24,
    // 3 Peak); the JSON default "Lowpass" is the 24 dB slope.
    if (!strcmp(v, "Lowpass") || !strcmp(v, "1")) return DR32_FILT_LP24;
    if (!strcmp(v, "Lowpass 12dB") || !strcmp(v, "0")) return DR32_FILT_LP12;
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


static const char *filter_type_name(dr32_filter_type t) {
    switch (t) {
        case DR32_FILT_LP12: return "Lowpass 12dB";
        case DR32_FILT_LP24: return "Lowpass";
        case DR32_FILT_HP24: return "Highpass";
        case DR32_FILT_PEAK: return "Peak";
    }
    return "Lowpass";
}

static const char *mod_target_name(dr32_mod_target t) {
    switch (t) {
        case DR32_MOD_FILTER: return "Filter";
        case DR32_MOD_ATTACK: return "Attack";
        case DR32_MOD_HOLD:   return "Hold";
        case DR32_MOD_DECAY:  return "Decay";
        case DR32_MOD_FX1:    return "FX1";
        case DR32_MOD_FX2:    return "FX2";
    }
    return "Filter";
}

int dr32_read_param(const dr32_kit *kit, const char *key, char *buf, int buf_len) {
    if (!kit || !key || !buf || buf_len <= 0) return 0;

    const char *sub;
    int pad = split_pad_key(key, &sub);
    if (pad >= 0) {
        const dr32_pad_slot *s = &kit->pads[pad];
        const dr32_pad *p = &s->params;
        if (!strcmp(sub, "sample"))      return snprintf(buf, buf_len, "%s", s->path);
        if (!strcmp(sub, "loaded"))      return snprintf(buf, buf_len, "%d", s->sample ? 1 : 0);
        if (!strcmp(sub, "frames"))      return snprintf(buf, buf_len, "%zu", s->frames);
        if (!strcmp(sub, "note"))        return snprintf(buf, buf_len, "%d", s->note);
        if (!strcmp(sub, "choke"))       return snprintf(buf, buf_len, "%d", p->choke_group);
        if (!strcmp(sub, "transpose"))   return snprintf(buf, buf_len, "%g", (double)p->transpose);
        if (!strcmp(sub, "detune"))      return snprintf(buf, buf_len, "%g", (double)p->detune);
        if (!strcmp(sub, "start"))       return snprintf(buf, buf_len, "%g", (double)p->play_start);
        if (!strcmp(sub, "length"))      return snprintf(buf, buf_len, "%g", (double)p->play_length);
        if (!strcmp(sub, "gain"))        return snprintf(buf, buf_len, "%g", (double)p->gain);
        if (!strcmp(sub, "volume"))      return snprintf(buf, buf_len, "%g", (double)p->volume_db);
        if (!strcmp(sub, "cell_volume")) return snprintf(buf, buf_len, "%g", (double)p->cell_volume_db);
        if (!strcmp(sub, "pan"))         return snprintf(buf, buf_len, "%g", (double)p->pan);
        if (!strcmp(sub, "vel_vol"))     return snprintf(buf, buf_len, "%g", (double)p->vel_to_volume);
        if (!strcmp(sub, "attack"))      return snprintf(buf, buf_len, "%g", (double)p->attack);
        if (!strcmp(sub, "hold"))        return snprintf(buf, buf_len, "%g", (double)p->hold);
        if (!strcmp(sub, "decay"))       return snprintf(buf, buf_len, "%g", (double)p->decay);
        if (!strcmp(sub, "env_mode"))    return snprintf(buf, buf_len, "%s",
                                                        p->env_mode == DR32_ENV_ASR ? "A-S-R" : "A-H-D");
        if (!strcmp(sub, "filter_on"))   return snprintf(buf, buf_len, "%d", p->filter_on);
        if (!strcmp(sub, "filter_type")) return snprintf(buf, buf_len, "%s", filter_type_name(p->filter_type));
        if (!strcmp(sub, "cutoff"))      return snprintf(buf, buf_len, "%g", (double)p->cutoff);
        if (!strcmp(sub, "resonance"))   return snprintf(buf, buf_len, "%g", (double)p->resonance);
        if (!strcmp(sub, "peak_gain"))   return snprintf(buf, buf_len, "%g", (double)p->peak_gain);
        if (!strcmp(sub, "mod_target"))  return snprintf(buf, buf_len, "%s", mod_target_name(p->mod_target));
        if (!strcmp(sub, "mod_amount"))  return snprintf(buf, buf_len, "%g", (double)p->mod_amount);
        if (!strcmp(sub, "pitch_env"))   return snprintf(buf, buf_len, "%d", p->pitch_to_env);
        if (!strcmp(sub, "sending_note"))return snprintf(buf, buf_len, "%d", p->sending_note);
        if (!strcmp(sub, "speaker_on"))  return snprintf(buf, buf_len, "%d", p->speaker_on);
        if (!strcmp(sub, "send1"))       return snprintf(buf, buf_len, "%g", (double)p->send_db[0]);
        if (!strcmp(sub, "send2"))       return snprintf(buf, buf_len, "%g", (double)p->send_db[1]);
        return 0;
    }

    if (!strncmp(key, "send", 4) || !strncmp(key, "insert", 6)) {
        int is_send = (key[0] == 's');
        const char *q = key + (is_send ? 4 : 6);
        int slot = (*q >= '1' && *q <= '2') ? (*q - '1') : -1;
        if (slot >= 0 && q[1] == '_') {
            const char *f2 = q + 2;
            const float *cache = is_send ? kit->send_p[slot] : kit->insert_p[slot];
            int idx = -1;
            if      (!strcmp(f2, "size")  || !strcmp(f2, "comp")   || !strcmp(f2, "p1")) idx = 0;
            else if (!strcmp(f2, "damp")  || !strcmp(f2, "crunch") || !strcmp(f2, "p2")) idx = 1;
            else if (!strcmp(f2, "decay") || !strcmp(f2, "trans")  || !strcmp(f2, "p3")) idx = 2;
            else if (!strcmp(f2, "predelay")) idx = 3;
            else if (!strcmp(f2, "mix"))      idx = 4;
            if (idx >= 0) return snprintf(buf, buf_len, "%g", (double)cache[idx]);
            if (!strcmp(f2, "return") && is_send)
                return snprintf(buf, buf_len, "%g", (double)kit->send_return_ui[slot]);
            if (!strcmp(f2, "type"))
                return snprintf(buf, buf_len, "%s",
                                dr32_efx_name(is_send ? kit->send_type[slot] : kit->insert_type[slot]));
        }
    }

    if (!strcmp(key, "master")) return snprintf(buf, buf_len, "%g", (double)kit->master_gain);
    if (!strcmp(key, "voices")) return snprintf(buf, buf_len, "%d", dr32_kit_active_voices(kit));
    return 0;
}

int dr32_apply_param(dr32_kit *kit, const char *key, const char *val) {
    if (!kit || !key || !val) return 0;

    const char *sub;
    int pad = split_pad_key(key, &sub);
    if (pad >= 0) {
        dr32_pad_slot *s = &kit->pads[pad];
        dr32_pad *p = &s->params;
        float f = (float)atof(val);

        // Assigning a sample NEVER sounds by itself. The browser's live_preview
        // assigns each highlighted file (and restores the old one on Back), so
        // hitting the pad plays whatever is currently previewed — at the pad's
        // real velocity, which an auto-audition could not reproduce.
        if      (!strcmp(sub, "sample"))        dr32_kit_load_sample(kit, pad, val);
        else if (!strcmp(sub, "note"))          dr32_kit_set_note(kit, pad, atoi(val));
        else if (!strcmp(sub, "choke"))         p->choke_group = atoi(val);
        else if (!strcmp(sub, "start"))         p->play_start = f;
        else if (!strcmp(sub, "length"))        p->play_length = f;
        else if (!strcmp(sub, "transpose"))     p->transpose = f;
        else if (!strcmp(sub, "detune"))        p->detune = f;
        else if (!strcmp(sub, "gain"))          p->gain = f;
        else if (!strcmp(sub, "volume"))        p->volume_db = f;
        else if (!strcmp(sub, "cell_volume"))   p->cell_volume_db = f;
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
        else if (!strcmp(sub, "speaker_on"))    p->speaker_on = atoi(val) ? 1 : 0;
        else if (!strcmp(sub, "sending_note"))  p->sending_note = atoi(val);
        else if (!strcmp(sub, "send1"))         p->send_db[0] = f;
        else if (!strcmp(sub, "send2"))         p->send_db[1] = f;
        else if (!strcmp(sub, "fx_type"))       p->fx_type = dr32_fx_from_name(val);
        else if (!strcmp(sub, "fx_p1"))         p->fx_p1 = f;
        else if (!strcmp(sub, "fx_p2"))         p->fx_p2 = f;
        else if (!strcmp(sub, "play"))          dr32_kit_note_on(kit, s->note, atoi(val));
        return 1;
    }

    // --- FX buses: send1_*/send2_* and insert1_*/insert2_*
    if (!strncmp(key, "send", 4) || !strncmp(key, "insert", 6)) {
        int is_send = (key[0] == 's');
        const char *p = key + (is_send ? 4 : 6);
        int slot = (*p >= '1' && *p <= '2') ? (*p - '1') : -1;
        if (slot >= 0 && p[1] == '_' && kit->fx) {
            const char *f2 = p + 2;
            float v = (float)atof(val);
            dr32_fxbus *fx = kit->fx;
            // Params are stored per slot so any one of them can be set alone.
            float *cache = is_send ? kit->send_p[slot] : kit->insert_p[slot];
            if (!strcmp(f2, "type")) {
                dr32_efx_type t = dr32_efx_from_name(val);
                if (is_send) { dr32_fxbus_set_send_type(fx, slot, t); kit->send_type[slot] = t; }
                else         { dr32_fxbus_set_insert_type(fx, slot, t); kit->insert_type[slot] = t; }
                return 1;
            }
            if (!strcmp(f2, "return") && is_send) {
                dr32_fxbus_set_send_return(fx, slot, v);
                kit->send_return_ui[slot] = v;
                return 1;
            }
            // Per-type control names map onto the same three generic slots.
            // They must be DISTINCT keys (the host rejects a hierarchy with any
            // duplicate key), but they address the same underlying parameter.
            int idx = -1;
            if      (!strcmp(f2, "size")  || !strcmp(f2, "comp")   || !strcmp(f2, "p1")) idx = 0;
            else if (!strcmp(f2, "damp")  || !strcmp(f2, "crunch") || !strcmp(f2, "p2")) idx = 1;
            else if (!strcmp(f2, "decay") || !strcmp(f2, "trans")  || !strcmp(f2, "p3")) idx = 2;
            else if (!strcmp(f2, "predelay")) idx = 3;
            else if (!strcmp(f2, "mix"))      idx = 4;
            if (idx >= 0) {
                cache[idx] = v;
                if (is_send) dr32_fxbus_set_send_params(fx, slot, cache[0], cache[1], cache[2], cache[3]);
                else         dr32_fxbus_set_insert_params(fx, slot, cache[0], cache[1], cache[2], cache[3], cache[4]);
                return 1;
            }
        }
    }

    if (!strcmp(key, "master")) { kit->master_gain = (float)atof(val); return 1; }
    if (!strcmp(key, "panic"))  { dr32_kit_all_off(kit); return 1; }
    if (!strcmp(key, "clear")) {
        dr32_kit_all_off(kit);
        for (int i = 0; i < DR32_PADS; i++) {
            dr32_kit_load_sample(kit, i, NULL);
            dr32_pad_defaults(&kit->pads[i].params);
            dr32_kit_set_note(kit, i, DR32_FIRST_NOTE + i);
        }
        return 1;
    }
    return 0;
}
