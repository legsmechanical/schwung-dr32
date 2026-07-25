#include "dr32_kit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dr32_kit_init(dr32_kit *k) {
    memset(k, 0, sizeof(*k));
    for (int i = 0; i < 128; i++) k->note_to_pad[i] = -1;
    for (int i = 0; i < DR32_PADS; i++) {
        dr32_pad_defaults(&k->pads[i].params);
        k->pads[i].note = DR32_FIRST_NOTE + i;
        k->note_to_pad[DR32_FIRST_NOTE + i] = (signed char)i;
    }
    k->master_gain = 1.0f;
}

void dr32_kit_free(dr32_kit *k) {
    for (int i = 0; i < DR32_PADS; i++) {
        free(k->pads[i].sample);
        free(k->pads[i].retired);
        k->pads[i].sample = k->pads[i].retired = NULL;
        k->pads[i].frames = 0;
    }
}

void dr32_kit_set_note(dr32_kit *k, int pad, int note) {
    if (pad < 0 || pad >= DR32_PADS || note < 0 || note > 127) return;
    int old = k->pads[pad].note;
    if (old >= 0 && old < 128 && k->note_to_pad[old] == pad) k->note_to_pad[old] = -1;
    k->pads[pad].note = note;
    k->note_to_pad[note] = (signed char)pad;
}

int dr32_kit_load_sample(dr32_kit *k, int pad, const char *path) {
    if (pad < 0 || pad >= DR32_PADS) return DR32_WAV_ERR_OPEN;
    dr32_pad_slot *s = &k->pads[pad];

    // Silence the pad first: the audio thread checks `active` before touching
    // `sample`, so stopping the voice before the swap means it cannot be mid-read
    // on the buffer we're about to replace.
    s->voice.active = 0;

    // One-deep retire. The buffer we displace now is freed on the NEXT load of
    // this pad — by which time many audio blocks have passed. Freeing it here
    // would race with a render that had already loaded the old pointer.
    free(s->retired);
    s->retired = s->sample;
    s->sample = NULL;
    s->frames = 0;
    s->path[0] = '\0';

    if (!path || !path[0]) return DR32_WAV_OK;     // clearing the pad

    dr32_wav w;
    int err = dr32_wav_load(path, &w);
    if (err != DR32_WAV_OK) return err;

    s->sample = w.data;
    s->frames = w.frames;
    snprintf(s->path, sizeof(s->path), "%s", path);
    return DR32_WAV_OK;
}

void dr32_kit_note_on(dr32_kit *k, int note, int velocity) {
    if (note < 0 || note > 127) return;
    int pad = k->note_to_pad[note];
    if (pad < 0) return;
    dr32_pad_slot *s = &k->pads[pad];

    // Choke: a pad cuts every OTHER pad sharing its group. Note the pad does not
    // choke itself — retrigger handles that, and self-choking would apply the
    // 3 ms fade to a voice we're about to restart anyway.
    int grp = s->params.choke_group;
    if (grp > 0) {
        for (int i = 0; i < DR32_PADS; i++) {
            if (i == pad) continue;
            if (k->pads[i].params.choke_group == grp && k->pads[i].voice.active)
                dr32_voice_choke(&k->pads[i].voice);
        }
    }

    dr32_voice_start(&s->voice, &s->params, s->sample, s->frames, velocity);
}

void dr32_kit_note_off(dr32_kit *k, int note) {
    if (note < 0 || note > 127) return;
    int pad = k->note_to_pad[note];
    if (pad < 0) return;
    dr32_voice_release(&k->pads[pad].voice, &k->pads[pad].params);
}

void dr32_kit_all_off(dr32_kit *k) {
    for (int i = 0; i < DR32_PADS; i++) k->pads[i].voice.active = 0;
}

void dr32_kit_render(dr32_kit *k, float *out, int frames) {
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);
    for (int i = 0; i < DR32_PADS; i++) {
        dr32_voice *v = &k->pads[i].voice;
        if (v->active) dr32_voice_render(v, out, frames);
    }
    if (k->master_gain != 1.0f) {
        for (int i = 0; i < 2 * frames; i++) out[i] *= k->master_gain;
    }
}

int dr32_kit_active_voices(const dr32_kit *k) {
    int n = 0;
    for (int i = 0; i < DR32_PADS; i++) if (k->pads[i].voice.active) n++;
    return n;
}
