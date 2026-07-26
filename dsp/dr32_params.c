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


int dr32_apply_param(dr32_kit *kit, const char *key, const char *val) {
    if (!kit || !key || !val) return 0;

    const char *sub;
    int pad = split_pad_key(key, &sub);
    if (pad >= 0) {
        dr32_pad_slot *s = &kit->pads[pad];
        dr32_pad *p = &s->params;
        float f = (float)atof(val);

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
        else if (!strcmp(sub, "fx_type"))       p->fx_type = dr32_fx_from_name(val);
        else if (!strcmp(sub, "fx_p1"))         p->fx_p1 = f;
        else if (!strcmp(sub, "fx_p2"))         p->fx_p2 = f;
        else if (!strcmp(sub, "play"))          dr32_kit_note_on(kit, s->note, atoi(val));
        return 1;
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
