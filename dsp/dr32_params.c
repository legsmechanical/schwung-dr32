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
/* "pad<N>_<sub>" addresses a pad explicitly. "pad_<sub>" — no digits — is the
 * ALIAS: it addresses whichever pad currently has focus.
 *
 * This is the mrdrums pattern (its `pad_vol` alias in front of `p01_vol`), and
 * it exists because the canvas cannot see pad presses: the host consumes them
 * before the canvas MIDI dispatch, so the UI can never learn which pad was hit.
 * With an alias the UI does not have to — it binds to fixed keys and the DSP
 * redirects them to kit->ui_current_pad, which the DSP moves itself because it
 * DOES see the notes (it plays them). */
static int split_pad_key(const dr32_kit *k, const char *key, const char **rest) {
    if (strncmp(key, "pad", 3) != 0) return -1;
    const char *p = key + 3;
    if (*p == '_') {                       /* alias: the focused pad */
        *rest = p + 1;
        int cur = k ? k->ui_current_pad : 0;
        return (cur >= 0 && cur < DR32_PADS) ? cur : 0;
    }
    int idx = 0, digits = 0;
    while (*p >= '0' && *p <= '9') { idx = idx * 10 + (*p - '0'); p++; digits++; }
    if (!digits || *p != '_') return -1;
    *rest = p + 1;
    return (idx >= 0 && idx < DR32_PADS) ? idx : -1;
}

/** Which of a send's five generic slots a per-type control name addresses.
 *
 *  The names have to be DISTINCT keys — the host rejects a whole hierarchy that
 *  contains any duplicate key — but several of them mean the same underlying
 *  parameter, so this is the one table both the read and the apply path use.
 *  They used to be duplicated inline in each, which is exactly how a key ends up
 *  settable but not readable, and a knob that reads zero looks like dead UI
 *  rather than a missing case.
 *
 *  ⚠ Slot 0 and 1 are 0..1 for the reverbs and a count of SIXTEENTHS (1..16)
 *  for the Delay. Slot 4 was the vestigial `mix` from the kit-insert era. */
static int send_slot_index(const char *name) {
    if (!strcmp(name, "size")     || !strcmp(name, "time_l") || !strcmp(name, "p1")) return 0;
    if (!strcmp(name, "damp")     || !strcmp(name, "time_r") || !strcmp(name, "p2")) return 1;
    if (!strcmp(name, "decay")    || !strcmp(name, "feedback") || !strcmp(name, "p3")) return 2;
    if (!strcmp(name, "predelay") || !strcmp(name, "tone")   || !strcmp(name, "p4")) return 3;
    if (!strcmp(name, "pingpong") || !strcmp(name, "p5")) return 4;
    if (!strcmp(name, "sync")     || !strcmp(name, "p6")) return 5;
    if (!strcmp(name, "ms_l")     || !strcmp(name, "p7")) return 6;
    if (!strcmp(name, "ms_r")     || !strcmp(name, "p8")) return 7;
    return -1;
}

/** Which of the Drum Bus's five controls a key addresses, -1 if none.
 *
 *  ⚠ attack and sustain are BIPOLAR here (-1..+1, neutral 0), unlike everything
 *  else on this bus. `mix` is the parallel blend — it was on the Drum Bus when
 *  it was a selectable insert, and went missing when the stage was lifted onto
 *  the master mix. */
static int bus_slot_index(const char *key) {
    if (!strncmp(key, "bus_", 4)) {
        const char *f = key + 4;
        if (!strcmp(f, "comp")    || !strcmp(f, "compress")) return 0;
        if (!strcmp(f, "crunch"))                            return 1;
        if (!strcmp(f, "attack"))                            return 2;
        if (!strcmp(f, "sustain"))                           return 3;
        if (!strcmp(f, "mix"))                               return 4;
    }
    return -1;
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
    int pad = split_pad_key(kit, key, &sub);
    if (pad >= 0) {
        const dr32_pad_slot *s = &kit->pads[pad];
        const dr32_pad *p = &s->params;
        /* Peak table for the waveform view: `bins` min/max pairs across the
         * whole file, as a flat CSV scaled to -100..100.
         *
         * Contract copied from the waveform-editor tool, which asks the DSP for
         * "waveform:<start>,<end>" and gets min/max pairs back — the canvas has
         * no way to read audio, so the DSP has to publish it. CSV rather than
         * that tool's JSON: it is a third the size and split(",") is cheaper
         * than JSON.parse in QuickJS. The value channel is 64 KB
         * (SHADOW_PARAM_VALUE_LEN), so 128 pairs is nowhere near the limit.
         *
         * An empty pad returns nothing, which the canvas draws as a flat line. */
        if (!strcmp(sub, "waveform")) {
            if (!s->sample || !s->frames || buf_len < 8) return snprintf(buf, buf_len, "%s", "");
            const int bins = 128;
            const int ch = (s->channels > 0) ? s->channels : 1;
            int off = 0;
            for (int b = 0; b < bins; b++) {
                size_t a0 = (size_t)((double)b       / bins * (double)s->frames);
                size_t a1 = (size_t)((double)(b + 1) / bins * (double)s->frames);
                if (a1 <= a0) a1 = a0 + 1;
                if (a1 > s->frames) a1 = s->frames;
                float lo = 0.0f, hi = 0.0f;
                for (size_t f = a0; f < a1; f++) {
                    /* Mono-sum: the view is about shape, not channel detail. */
                    float v = 0.0f;
                    for (int c = 0; c < ch; c++) v += s->sample[f * (size_t)ch + (size_t)c];
                    v /= (float)ch;
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
                int li = (int)(lo * 100.0f), hj = (int)(hi * 100.0f);
                if (li < -100) li = -100;
                if (hj > 100) hj = 100;
                int n = snprintf(buf + off, (size_t)(buf_len - off), "%s%d,%d",
                                 b ? "," : "", li, hj);
                if (n <= 0 || off + n >= buf_len) break;
                off += n;
            }
            return off;
        }
        if (!strcmp(sub, "sample") || !strcmp(sub, "sample_move")
            || !strcmp(sub, "sample_user"))  return snprintf(buf, buf_len, "%s", s->path);
        if (!strcmp(sub, "loaded"))      return snprintf(buf, buf_len, "%d", s->sample ? 1 : 0);
        /* Folder browse. The cast is deliberate: the browse cache is a lazily
         * filled mirror of the filesystem, not part of the kit's value, and
         * `kit` is never actually a const object. Refreshing on read keeps one
         * code path instead of a read that can disagree with a write — and it
         * only touches the disk when the focused pad's FOLDER changes, not per
         * frame. */
        if (!strcmp(sub, "browse"))
            return snprintf(buf, buf_len, "%d",
                            dr32_kit_browse_index((dr32_kit *)kit, pad));
        if (!strcmp(sub, "browse_count"))
            return snprintf(buf, buf_len, "%d",
                            dr32_kit_browse_count((dr32_kit *)kit, pad));
        /* The folder's names, newline-separated, for the canvas picker list.
         * Newline because a comma is legal in a filename and a CSV would split
         * such a name in half. Extensions are stripped: they are all audio and
         * the suffix only eats width in a 128 px list. Truncated rather than
         * failed if the value channel would overflow — a short list beats a
         * dropped one, and 512 names is far below the 64 KB limit anyway. */
        if (!strcmp(sub, "browse_names")) {
            dr32_kit *mk = (dr32_kit *)kit;
            int n = dr32_kit_browse_count(mk, pad);
            int off = 0;
            for (int i = 0; i < n; i++) {
                const char *nm = mk->browse[i];
                int len = (int)strlen(nm);
                const char *dot = strrchr(nm, '.');
                if (dot && dot != nm) len = (int)(dot - nm);
                int w = snprintf(buf + off, (size_t)(buf_len - off), "%s%.*s",
                                 i ? "\n" : "", len, nm);
                if (w <= 0 || off + w >= buf_len) break;
                off += w;
            }
            if (off == 0 && buf_len > 0) buf[0] = '\0';
            return off;
        }
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

    if (!strncmp(key, "send", 4)) {
        const char *q = key + 4;
        int slot = (*q >= '1' && *q <= '2') ? (*q - '1') : -1;
        if (slot >= 0 && q[1] == '_') {
            const char *f2 = q + 2;
            const float *cache = kit->send_p[slot];
            /* The ONE param the whole send page's visibility hangs off.
             *
             * The host's visible_if takes a SINGLE condition on a SINGLE param
             * (shadow_ui.c: equals / not_equals / gt / lt / truthy — no AND, no
             * lists), so "armed type is Delay AND it is running free" cannot be
             * written directly. Publishing the page's mode as its own read-only
             * param makes every row a single equality again. */
            if (!strcmp(f2, "mode")) {
                if (kit->send_type[slot] != DR32_EFX_DELAY)
                    return snprintf(buf, buf_len, "%s", "Verb");
                return snprintf(buf, buf_len, "%s",
                                cache[5] >= 0.5f ? "Sync" : "Free");
            }
            /* Sync reads back as a NAME so the enum round-trips through the
             * menu the same way the type does. */
            if (!strcmp(f2, "sync"))
                return snprintf(buf, buf_len, "%s", cache[5] >= 0.5f ? "Sync" : "Free");
            int idx = send_slot_index(f2);
            if (idx >= 0) return snprintf(buf, buf_len, "%g", (double)cache[idx]);
            if (!strcmp(f2, "return"))
                return snprintf(buf, buf_len, "%g", (double)kit->send_return_ui[slot]);
            if (!strcmp(f2, "type"))
                return snprintf(buf, buf_len, "%s", dr32_efx_name(kit->send_type[slot]));
        }
    }

    {
        int bidx = bus_slot_index(key);
        if (bidx >= 0) return snprintf(buf, buf_len, "%g", (double)kit->bus_p[bidx]);
    }

    if (!strcmp(key, "ui_current_pad"))
        return snprintf(buf, buf_len, "%d", kit->ui_current_pad);
    if (!strcmp(key, "ui_auto_select_pad"))
        return snprintf(buf, buf_len, "%s", kit->ui_auto_select_pad ? "on" : "off");
    if (!strcmp(key, "master")) return snprintf(buf, buf_len, "%g", (double)kit->master_gain);
    if (!strcmp(key, "voices")) return snprintf(buf, buf_len, "%d", dr32_kit_active_voices(kit));
    return 0;
}

int dr32_apply_param(dr32_kit *kit, const char *key, const char *val) {
    if (!kit || !key || !val) return 0;

    const char *sub;
    int pad = split_pad_key(kit, key, &sub);
    if (pad >= 0) {
        dr32_pad_slot *s = &kit->pads[pad];
        dr32_pad *p = &s->params;
        float f = (float)atof(val);

        // Assigning a sample NEVER sounds by itself. The browser's live_preview
        // assigns each highlighted file (and restores the old one on Back), so
        // hitting the pad plays whatever is currently previewed — at the pad's
        // real velocity, which an auto-audition could not reproduce.
        // "Sample" is a PICKER of two roots (Move / User); the filepath type
        // takes one root each, so they are two keys meaning the same thing.
        if      (!strcmp(sub, "sample") || !strcmp(sub, "sample_move")
                 || !strcmp(sub, "sample_user"))  dr32_kit_load_sample(kit, pad, val);
        else if (!strcmp(sub, "browse"))          dr32_kit_browse_select(kit, pad, atoi(val));
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

    // --- FX buses: send1_*/send2_*
    if (!strncmp(key, "send", 4)) {
        const char *p = key + 4;
        int slot = (*p >= '1' && *p <= '2') ? (*p - '1') : -1;
        if (slot >= 0 && p[1] == '_' && kit->fx) {
            const char *f2 = p + 2;
            float v = (float)atof(val);
            dr32_fxbus *fx = kit->fx;
            // Params are stored per slot so any one of them can be set alone.
            float *cache = kit->send_p[slot];
            if (!strcmp(f2, "type")) {
                dr32_efx_type t = dr32_efx_from_name(val);
                // Load that type's musical starting point. Selecting an effect
                // should sound like something immediately, not inherit the
                // previous effect's knob positions.
                if (t != DR32_EFX_NONE) dr32_efx_defaults(t, cache);
                dr32_fxbus_set_send_type(fx, slot, t);
                kit->send_type[slot] = t;
                dr32_fxbus_set_send_params(fx, slot, cache, DR32_SEND_PARAMS);
                return 1;
            }
            if (!strcmp(f2, "return")) {
                dr32_fxbus_set_send_return(fx, slot, v);
                kit->send_return_ui[slot] = v;
                return 1;
            }
            if (!strcmp(f2, "sync")) {
                /* Name or number: the canvas writes the label, a restored state
                 * or a script may write 0/1. */
                if (!strcmp(val, "Sync"))      cache[5] = 1.0f;
                else if (!strcmp(val, "Free")) cache[5] = 0.0f;
                else                           cache[5] = (v >= 0.5f) ? 1.0f : 0.0f;
                dr32_fxbus_set_send_params(fx, slot, cache, DR32_SEND_PARAMS);
                return 1;
            }
            int idx = send_slot_index(f2);
            if (idx >= 0) {
                cache[idx] = v;
                dr32_fxbus_set_send_params(fx, slot, cache, DR32_SEND_PARAMS);
                return 1;
            }
        }
    }

    // --- the always-on Drum Bus: bus_*
    {
        int bidx = bus_slot_index(key);
        if (bidx >= 0) {
            /* Cache first, apply second: a missing fx bus must not make the
             * value vanish, or a state restore on an instance that failed to
             * allocate would silently drop the whole page. */
            kit->bus_p[bidx] = (float)atof(val);
            if (kit->fx)
                dr32_fxbus_set_bus_params(kit->fx, kit->bus_p[0], kit->bus_p[1],
                                          kit->bus_p[2], kit->bus_p[3], kit->bus_p[4]);
            return 1;
        }
    }

    if (!strcmp(key, "ui_current_pad")) {
        int v = atoi(val);
        kit->ui_current_pad = (v < 0) ? 0 : (v >= DR32_PADS ? DR32_PADS - 1 : v);
        return 1;
    }
    if (!strcmp(key, "ui_live_press")) {
        /* The canvas saw a physical pad press. It cannot say WHICH pad — a grid
         * position is not a pad — so the note decides, and this only vouches
         * that a finger was involved.
         *
         * The note usually beats this signal here (it comes straight off the
         * MIDI stream, while this crosses a process boundary), so look back
         * first and only arm forward if nothing recent matches. Handling just
         * one order would drop roughly half the presses. */
        if (!kit->ui_auto_select_pad) return 1;
        if (kit->last_hit_pad >= 0 &&
            (kit->block - kit->last_hit_block) <= DR32_LIVE_MATCH_BLOCKS) {
            kit->ui_current_pad = kit->last_hit_pad;
            kit->live_armed = 0;
            /* Consume it. A note may vouch for ONE press: leaving it claimable
             * let a second press inside the window re-match the same note, so a
             * press on a dead pad (the left 4x4 plays nothing, but the host
             * still forwards it) could grab whatever the SEQUENCER had just
             * played and yank focus there. Caught by tests/test_kit.c. */
            kit->last_hit_pad = -1;
        } else {
            kit->live_armed = 1;
            kit->live_arm_block = kit->block;
        }
        return 1;
    }
    if (!strcmp(key, "ui_auto_select_pad")) {
        // The host's filepath browser_hooks suspend this while a browser is
        // open (MODULES.md documents the pattern), so accept both spellings.
        kit->ui_auto_select_pad = (!strcmp(val, "on") || atoi(val) == 1);
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
