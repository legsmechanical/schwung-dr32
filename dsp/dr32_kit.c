#include "dr32_kit.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strcasecmp */

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
    k->ui_current_pad = 0;
    k->ui_auto_select_pad = 1;      // playing a pad focuses it, as mrdrums does
    k->live_armed = 0;
    k->live_arm_block = 0;
    k->last_hit_pad = -1;
    k->last_hit_block = 0;
    for (int i = 0; i < 2; i++) {
        k->send_p[i][0] = 0.5f;   // size
        k->send_p[i][1] = 0.3f;   // damping
        k->send_p[i][2] = 0.5f;   // decay
        k->send_p[i][3] = 0.0f;   // pre-delay OFF by default (it was 125 ms)
        for (int j = 4; j < DR32_SEND_PARAMS; j++) k->send_p[i][j] = 0.0f;
        k->send_type[i] = DR32_EFX_NONE;
        k->send_return_ui[i] = 1.0f;
    }

    /* Drum Bus: always on, and starting NEUTRAL so it is inaudible and bypassed
     * until a knob moves. Attack and Sustain are bipolar -1..+1, neutral 0. */
    k->bus_p[0] = 0.0f;   // compress
    k->bus_p[1] = 0.0f;   // crunch
    k->bus_p[2] = 0.0f;   // attack   (bipolar)
    k->bus_p[3] = 0.0f;   // sustain  (bipolar)
    k->bus_p[4] = 1.0f;   // mix — fully processed; only ever takes the bus away
    k->bpm = 120.0f;

    // Send 1 starts as a Plate — the drum reverb — so raising a pad's Send 1 is
    // immediately useful. It costs nothing until a pad actually feeds it (the
    // bus skips processing while idle), and every pad send starts at -70 = off,
    // so nothing is audible until asked for.
    if (k->fx) {
        dr32_efx_defaults(DR32_EFX_PLATE, k->send_p[0]);
        k->send_type[0] = DR32_EFX_PLATE;
        dr32_fxbus_set_send_type(k->fx, 0, DR32_EFX_PLATE);
        dr32_fxbus_set_send_params(k->fx, 0, k->send_p[0], DR32_SEND_PARAMS);
        dr32_fxbus_set_send_return(k->fx, 0, 1.0f);
        dr32_fxbus_set_bus_params(k->fx, k->bus_p[0], k->bus_p[1],
                                  k->bus_p[2], k->bus_p[3], k->bus_p[4]);
    }
}

void dr32_kit_set_bpm(dr32_kit *k, float bpm) {
    if (!k) return;
    k->bpm = bpm;
    if (k->fx) dr32_fxbus_set_bpm(k->fx, bpm);
}

/* ---------- folder browse ------------------------------------------------
 *
 * The file browser hands DR32 a finished path and nothing else, so walking the
 * samples beside the current one means reading the directory here. Host thread
 * only: this opens directories and dr32_kit_browse_select() loads a WAV, both
 * of which dr32_kit_load_sample() already does from the same thread.
 *
 * One directory is cached at a time. Switching pads inside a kit almost always
 * stays in one folder, so the scan is rare; a scan on every knob step would be
 * pointless work, and scanning the whole user tree is out of the question at
 * ~3.8 GB. */

static void browse_free(dr32_kit *k) {
    for (int i = 0; i < k->browse_n; i++) free(k->browse[i]);
    free(k->browse);
    k->browse = NULL;
    k->browse_n = 0;
    k->browse_dir[0] = '\0';
}

static int is_audio_name(const char *n) {
    const char *d = strrchr(n, '.');
    if (!d) return 0;
    return !strcasecmp(d, ".wav") || !strcasecmp(d, ".aif") || !strcasecmp(d, ".aiff");
}

static int cmp_name(const void *a, const void *b) {
    /* Case-insensitive so the order matches what a browser shows. */
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

/** Directory part of `path`. Returns 0 when there is none. */
static int split_dir(const char *path, char *out, size_t cap) {
    if (!path || !path[0]) return 0;
    const char *s = strrchr(path, '/');
    if (!s || s == path) return 0;
    size_t n = (size_t)(s - path);
    if (n >= cap) return 0;
    memcpy(out, path, n);
    out[n] = '\0';
    return 1;
}

static const char *base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/** Point the cache at the folder holding `pad`'s sample. Returns entry count. */
static int browse_ensure(dr32_kit *k, int pad) {
    if (pad < 0 || pad >= DR32_PADS) return 0;
    char dir[DR32_MAX_PATH];
    if (!split_dir(k->pads[pad].path, dir, sizeof(dir))) { browse_free(k); return 0; }
    if (k->browse && !strcmp(k->browse_dir, dir)) return k->browse_n;   /* cache hit */

    browse_free(k);
    DIR *d = opendir(dir);
    if (!d) return 0;
    char **v = (char **)calloc(DR32_BROWSE_MAX, sizeof(char *));
    if (!v) { closedir(d); return 0; }
    int n = 0;
    struct dirent *e;
    while (n < DR32_BROWSE_MAX && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;          /* dotfiles and . / .. */
        if (!is_audio_name(e->d_name)) continue;
        char *dup = (char *)malloc(strlen(e->d_name) + 1);
        if (!dup) break;
        strcpy(dup, e->d_name);
        v[n++] = dup;
    }
    closedir(d);
    qsort(v, (size_t)n, sizeof(char *), cmp_name);
    k->browse = v;
    k->browse_n = n;
    snprintf(k->browse_dir, sizeof(k->browse_dir), "%s", dir);
    return n;
}

int dr32_kit_browse_count(dr32_kit *k, int pad) {
    return k ? browse_ensure(k, pad) : 0;
}

int dr32_kit_browse_index(dr32_kit *k, int pad) {
    if (!k) return -1;
    int n = browse_ensure(k, pad);
    if (n <= 0) return -1;
    const char *cur = base_name(k->pads[pad].path);
    for (int i = 0; i < n; i++) if (!strcmp(k->browse[i], cur)) return i;
    return -1;
}

int dr32_kit_browse_select(dr32_kit *k, int pad, int idx) {
    if (!k) return -1;
    int n = browse_ensure(k, pad);
    if (n <= 0) return -1;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    if (idx == dr32_kit_browse_index(k, pad)) return idx;   /* already there */

    /* Build the full path against the CACHED directory, not the pad's current
     * one: load_sample is about to overwrite that path. */
    char full[DR32_MAX_PATH];
    int w = snprintf(full, sizeof(full), "%s/%s", k->browse_dir, k->browse[idx]);
    if (w <= 0 || (size_t)w >= sizeof(full)) return -1;

    /* The directory string survives the load (load_sample rewrites pad->path,
     * and browse_dir is a separate buffer), so the cache stays valid and the
     * next step does not rescan. */
    dr32_kit_load_sample(k, pad, full);
    return idx;
}

void dr32_kit_free(dr32_kit *k) {
    browse_free(k);
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
    /* Focus follows this note if nothing is sequencing (see transport_running),
     * or if a host vouched that a finger caused it. A note alone, with a
     * transport running, cannot: a live hit and a sequenced one are identical
     * here (measured on device), so following every note let playback drag the
     * editor around. The vouch (ui_live_press) supplies the missing bit.
     *
     * Record the hit either way — the press signal may still be in flight, and
     * set_param("ui_live_press") looks back at this. (An earlier attempt to
     * have the host tag the note MOVE_MIDI_SOURCE_PAD reached nothing even with
     * the gate removed; that plumbing was reverted. Don't re-tread it.) */
    k->last_hit_pad = pad;
    k->last_hit_block = k->block;
    if (k->ui_auto_select_pad && !k->transport_running && !k->host_vouches) {
        /* Nothing is sequencing, so this note came from a hand. Follow it
         * outright — no vouch needed, on any host. (See transport_running in
         * dr32_kit.h.) A vouch that is still in flight for this same press is
         * consumed too, so it cannot re-arm for the next sequenced note. */
        k->ui_current_pad = pad;
        k->live_armed = 0;
        k->last_hit_pad = -1;
    } else if (k->live_armed && k->ui_auto_select_pad &&
               (k->block - k->live_arm_block) <= DR32_LIVE_MATCH_BLOCKS) {
        k->ui_current_pad = pad;
        k->live_armed = 0;
        k->last_hit_pad = -1;   /* consumed — see the note in dr32_params.c */
    }

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
            if (s0 > 0.0f) dr32_fxbus_send(k->fx, 0, f, l * s0, r * s0);
            if (s1 > 0.0f) dr32_fxbus_send(k->fx, 1, f, l * s1, r * s1);
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
