#include "dr32_kit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void dr32_kit_init(dr32_kit *k) {
    memset(k, 0, sizeof(*k));
    k->fx = dr32_fxbus_create(DR32_SR);
    for (int i = 0; i < 128; i++) k->note_to_pad[i] = -1;
    for (int i = 0; i < DR32_PADS; i++) {
        dr32_pad_defaults(&k->pads[i].params);
        k->pads[i].note = DR32_FIRST_NOTE + i;
        k->note_to_pad[DR32_FIRST_NOTE + i] = (signed char)i;
    }
    k->master_gain = 1.0f;
    for (int i = 0; i < 2; i++) {
        k->send_p[i][0] = 0.5f;   // size
        k->send_p[i][1] = 0.3f;   // damping
        k->send_p[i][2] = 0.5f;   // decay
        k->send_p[i][3] = 0.0f;   // pre-delay OFF by default (it was 125 ms)
        k->send_p[i][4] = 1.0f;
        // Insert defaults: Compress/Crunch off, Transients CENTRED (0.5 is
        // neutral for a bipolar control), fully wet.
        k->insert_p[i][0] = 0.0f;   // compress / size
        k->insert_p[i][1] = 0.0f;   // crunch / damping
        k->insert_p[i][2] = 0.5f;   // transients (0.5 neutral) / decay
        k->insert_p[i][3] = 0.0f;   // pre-delay
        k->insert_p[i][4] = 1.0f;   // dry/wet
        k->send_type[i] = DR32_EFX_NONE;
        k->insert_type[i] = DR32_EFX_NONE;
        k->send_return_ui[i] = 1.0f;
    }
}

void dr32_kit_free(dr32_kit *k) {
    dr32_fxbus_destroy(k->fx);
    k->fx = NULL;
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
    s->channels = w.channels;
    s->sample_rate = w.sample_rate;
    snprintf(s->path, sizeof(s->path), "%s", path);
    return DR32_WAV_OK;
}

void dr32_kit_note_on(dr32_kit *k, int note, int velocity) {
    if (note < 0 || note > 127) return;
    int pad = k->note_to_pad[note];
    if (pad < 0) return;
    dr32_pad_slot *s = &k->pads[pad];

    // Choke arbitration, per the native DrumChainMidiNode: among note-ons that
    // arrive at the SAME time in the same nonzero group, the HIGHEST incoming
    // MIDI note wins and the lower ones are killed. Sequential hits behave the
    // usual way (the newer note chokes the older).
    //
    // "Same time" is approximated as "same render block", which is the finest
    // grain available to us: the host hands us a block's MIDI before rendering.
    int grp = s->params.choke_group;
    if (grp > 0) {
        for (int i = 0; i < DR32_PADS; i++) {
            if (i == pad) continue;
            dr32_pad_slot *o = &k->pads[i];
            if (o->params.choke_group != grp || !o->voice.active) continue;
            if (o->voice.block == k->block && o->voice.note > note) {
                // A higher note already won this block: the incoming note loses.
                return;
            }
            dr32_voice_choke(&o->voice);
        }
    }

    dr32_voice_start(&s->voice, &s->params, s->sample, s->frames, s->channels,
                     s->sample_rate, velocity);
    s->voice.note = note;              // arbitration uses the INCOMING note
    s->voice.block = k->block;
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

static inline float db_to_gain(float db) {
    // -70 dB is the format's "off", and native floors the send there.
    if (db <= -70.0f) return 0.0f;
    return powf(10.0f, db / 20.0f);
}

void dr32_kit_render(dr32_kit *k, float *out, int frames) {
    k->block++;
    if (frames > DR32_KIT_MAX_BLOCK) frames = DR32_KIT_MAX_BLOCK;
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);

    for (int i = 0; i < DR32_PADS; i++) {
        dr32_voice *v = &k->pads[i].voice;
        if (!v->active) continue;

        const float s0 = db_to_gain(k->pads[i].params.send_db[0]);
        const float s1 = db_to_gain(k->pads[i].params.send_db[1]);

        if (!k->fx || (s0 <= 0.0f && s1 <= 0.0f)) {
            dr32_voice_render(v, out, frames);       // dry only: no detour
            continue;
        }

        // This pad feeds the send buses, so render it on its own first. Sends
        // are POST-fader (native behaviour), which is exactly what the voice
        // already produces.
        memset(k->scratch, 0, sizeof(float) * 2 * (size_t)frames);
        dr32_voice_render(v, k->scratch, frames);
        for (int f = 0; f < frames; f++) {
            float l = k->scratch[2 * f], r = k->scratch[2 * f + 1];
            out[2 * f]     += l;
            out[2 * f + 1] += r;
            if (s0 > 0.0f) dr32_fxbus_send(k->fx, 0, l * s0, r * s0);
            if (s1 > 0.0f) dr32_fxbus_send(k->fx, 1, l * s1, r * s1);
        }
    }

    if (k->fx) dr32_fxbus_process(k->fx, out, frames);

    if (k->master_gain != 1.0f) {
        for (int i = 0; i < 2 * frames; i++) out[i] *= k->master_gain;
    }
}

int dr32_kit_active_voices(const dr32_kit *k) {
    int n = 0;
    for (int i = 0; i < DR32_PADS; i++) if (k->pads[i].voice.active) n++;
    return n;
}
