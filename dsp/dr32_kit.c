#include "dr32_kit.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strcasecmp */
#include <sys/stat.h>    /* the sample-decode memo — see dr32_kit_load_sample */

void dr32_kit_init(dr32_kit *k) {
    memset(k, 0, sizeof(*k));
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
    k->browse_wire_seen = 0;      /* new folder: the next write is a baseline */
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

int dr32_kit_browse_step(dr32_kit *k, int pad, int wire) {
    if (!k) return -1;
    int n = browse_ensure(k, pad);
    if (n <= 0) { k->browse_wire = wire; k->browse_wire_seen = 1; return -1; }

    int cur = dr32_kit_browse_index(k, pad);
    if (cur < 0) cur = 0;

    /* The FIRST write after the folder changed is a baseline, not a move — the
     * host's knob still holds whatever it showed for the previous pad, and
     * treating that difference as a delta would jump the selection. */
    if (!k->browse_wire_seen) {
        k->browse_wire = wire;
        k->browse_wire_seen = 1;
        return cur;
    }

    int delta = wire - k->browse_wire;
    k->browse_wire = wire;
    if (delta == 0) return cur;

    /* No clamp here on purpose: dr32_kit_browse_select clamps, and it is the
     * one place that should. A second copy passed every test with it removed —
     * mutation-testing showed it was guarding nothing. */
    return dr32_kit_browse_select(k, pad, cur + delta);
}

int dr32_kit_browse_index_sync(dr32_kit *k, int pad) {
    int i = dr32_kit_browse_index(k, pad);
    /* ⚠ NEVER hand -1 to the host. `browse` is declared min 0, so a negative
     * value is out of range. -1 means the pad's own sample was not found in the
     * listing — a truncated folder, or a sample that has moved — and 0 is the
     * honest answer there, from which stepping still works. */
    return (i < 0) ? 0 : i;
}

/*
 * ⚠⚠ THE READ MUST NOT TOUCH THE DELTA BASELINE. I made it do exactly that and
 * it caused the very jumping it was meant to cure.
 *
 * The reasoning was that the host adopts the value it reads back into its knob,
 * so the baseline should follow. It does NOT: the host keeps a persistent
 * `knobStates[key]`, seeded ONCE from a read when the knob is first touched and
 * stepped by the detent from then on. Reads only feed the DISPLAY.
 *
 * So the host's value marches on independently while the index stays small, and
 * a baseline resynced to the index makes every delta `hostValue - index` — a
 * jump whose size is the gap between them. The baseline may only be moved by a
 * WRITE, which is the one event that tells us what the host actually holds.
 */

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

/* Is `path` byte-for-byte what this pad already holds?
 *
 * A kit recall re-reads all 16 pads, and the WAV decode was ~3.5 ms of it —
 * ON THE SPI CALLBACK, against a 2.9 ms block period (measured on device
 * 2026-09-08 via param-slow). Recalling the same kit decodes 16 identical
 * files every time.
 *
 * ⚠ THIS IS DELIBERATELY NOT "skip the reload when the kit path matches".
 * dr32_state_read loads the kit FIRST precisely because it replaces every pad
 * wholesale, and the saved blob carries only the user's DELTAS from that
 * baseline; skipping the reload would apply those deltas to whatever the user
 * had edited since, turning a restore into a merge. The memo is at the DECODE,
 * which no semantics depend on: pad params still reset from the kit JSON, the
 * pad is still "replaced", the bytes are simply not read twice.
 *
 * Size+mtime, not path alone, so editing a sample in place still reloads it —
 * otherwise "reload the kit" would be the one gesture that could not pick up
 * an edited file. One stat per pad is microseconds against a full decode. */
static int sample_is_current(const dr32_pad_slot *s, const char *path) {
    if (!s->sample || !s->path[0]) return 0;
    if (strcmp(s->path, path) != 0) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;        /* gone or unreadable → reload */
    return (long)st.st_size == s->src_size && (long)st.st_mtime == s->src_mtime;
}

int dr32_kit_load_sample(dr32_kit *k, int pad, const char *path) {
    if (pad < 0 || pad >= DR32_PADS) return DR32_WAV_ERR_OPEN;
    dr32_pad_slot *s = &k->pads[pad];

    /* Already holding exactly this file — keep the decoded buffer. Returns
     * before the retire below, so the audio thread's pointer is untouched and
     * no voice is silenced: re-decoding identical audio was the only thing
     * being skipped. */
    if (path && path[0] && sample_is_current(s, path)) return DR32_WAV_OK;

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
    /* Clear the decode stamp with the buffer it describes. sample_is_current
     * already requires a non-NULL sample, so this is belt-and-braces — but a
     * stamp outliving its buffer is exactly the kind of stale pair that starts
     * matching again by accident after a struct reuse. */
    s->src_size = 0;
    s->src_mtime = 0;

    if (!path || !path[0]) return DR32_WAV_OK;     // clearing the pad

    dr32_wav w;
    int err = dr32_wav_load(path, &w);
    if (err != DR32_WAV_OK) return err;

    s->sample = w.data;
    s->frames = w.frames;
    s->channels = w.channels;
    s->sample_rate = w.sample_rate;
    snprintf(s->path, sizeof(s->path), "%s", path);
    /* Stamp what we just decoded, for sample_is_current above. Stat AFTER the
     * read: a file rewritten between the two then looks stale next time and
     * reloads, which is the safe direction to be wrong in. */
    {
        struct stat st;
        if (stat(path, &st) == 0) { s->src_size = (long)st.st_size; s->src_mtime = (long)st.st_mtime; }
        else                      { s->src_size = 0; s->src_mtime = 0; }
    }
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

void dr32_kit_render(dr32_kit *k, float *out, int frames) {
    k->block++;
    if (frames > DR32_KIT_MAX_BLOCK) frames = DR32_KIT_MAX_BLOCK;
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);

    /* Every voice straight into the mix. There is no send detour left: a pad's
     * `send_db[]` is read by the HOST now (voice_send_params, see dr32.c) and
     * applied on its side into its own return buses, so nothing here has to
     * render a pad twice. */
    for (int i = 0; i < DR32_PADS; i++) {
        dr32_voice *v = &k->pads[i].voice;
        if (v->active) dr32_voice_render(v, out, frames);
    }

    if (k->master_gain != 1.0f) {
        for (int i = 0; i < 2 * frames; i++) out[i] *= k->master_gain;
    }
}

/* One float sample pair -> the host's int16 destination, ACCUMULATING and
 * saturating. Accumulate because the destinations alias: two pads on one host
 * bus share a pointer and their sum is supposed to happen here. */
static inline void mix_f32_to_i16(int16_t *dst, const float *src, int n) {
    for (int i = 0; i < n; i++) {
        float v = src[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        int32_t sum = (int32_t)dst[i] + (int32_t)(v * 32767.0f);
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        dst[i] = (int16_t)sum;
    }
}

void dr32_kit_render_split(dr32_kit *k, int16_t *const *voice_out, int n_voices,
                           int16_t *main_out, int frames) {
    if (!k || !voice_out) return;
    k->block++;
    if (frames > DR32_KIT_MAX_BLOCK) frames = DR32_KIT_MAX_BLOCK;

    /* ⚠ NOTHING THE HOST OWNS IS CLEARED HERE. It cleared the distinct
     * destinations before calling, and clearing one ourselves would wipe
     * another pad's audio out of a buffer they share. `split_dry` is OURS and
     * is zeroed per block exactly as dr32_kit_render zeroes `out`. */
    memset(k->split_dry, 0, sizeof(float) * 2 * (size_t)frames);

    for (int i = 0; i < DR32_PADS; i++) {
        dr32_voice *v = &k->pads[i].voice;
        if (!v->active) continue;

        int16_t *dst = (i < n_voices && voice_out[i]) ? voice_out[i] : NULL;
        if (dst && dst != main_out) {
            /*
             * ROUTED OUT. Rendered alone into the scratch, because this pad's
             * audio has to leave as int16 into a buffer it may SHARE with
             * another pad — so it is converted and accumulated, never written.
             * Master gain is applied here because this pad will not reach the
             * summing below.
             */
            memset(k->scratch, 0, sizeof(float) * 2 * (size_t)frames);
            dr32_voice_render(v, k->scratch, frames);
            for (int n = 0; n < 2 * frames; n++) k->scratch[n] *= k->master_gain;
            mix_f32_to_i16(dst, k->scratch, 2 * frames);
        } else {
            /* Stays in the kit: straight into the mix, exactly as
             * dr32_kit_render does it — dr32_voice_render accumulates, and
             * `split_dry` was zeroed once above just like that path's `out`. */
            dr32_voice_render(v, k->split_dry, frames);
        }
    }

    /* The same single application of master_gain, in the same place, that
     * dr32_kit_render uses. */
    if (k->master_gain != 1.0f) {
        for (int n = 0; n < 2 * frames; n++) k->split_dry[n] *= k->master_gain;
    }
    if (main_out) mix_f32_to_i16(main_out, k->split_dry, 2 * frames);
}

int dr32_kit_active_voices(const dr32_kit *k) {
    int n = 0;
    for (int i = 0; i < DR32_PADS; i++) if (k->pads[i].voice.active) n++;
    return n;
}
