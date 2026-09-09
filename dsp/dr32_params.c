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
        /* `sample` is the key, and since 2026-09-08 the only one the UI uses:
         * the Move/User pair collapsed into ONE browser cell rooted at /data.
         * The two old spellings stay accepted because they cost a strcmp and
         * anything still holding them keeps working. */
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
        /* `end` is a UI ALIAS of length: the host's wave editor draws a trim
         * region as start..end (`wav_position` mode "end"), while Move's own
         * format — and therefore the kit on disk — stores a LENGTH. Read as
         * start+length, written back as length = end-start; nothing changes in
         * the .ablpreset. Clamped to 1 so a picture never runs past the file. */
        if (!strcmp(sub, "end")) {
            float e = p->play_start + p->play_length;
            if (e > 1.0f) e = 1.0f;
            return snprintf(buf, buf_len, "%g", (double)e);
        }
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
        if (!strcmp(sub, "punch"))
            return snprintf(buf, buf_len, "%g",
                            p->fx_type == DR32_FX_PUNCH ? (double)p->fx_p1 : 0.0);
        if (!strcmp(sub, "punch_time")) return snprintf(buf, buf_len, "%g", (double)p->fx_p2);
        /* The two per-pad sends into the HOST's global return buses. Array
         * position is the send index the host reads them in: `send_a` is
         * Send A, `send_b` is Send B (see get_param("voice_send_params") in
         * dr32.c). `send1`/`send2` are accepted as read aliases so a state blob
         * written before the rename still restores its levels. */
        if (!strcmp(sub, "send_a") || !strcmp(sub, "send1"))
            return snprintf(buf, buf_len, "%g", (double)p->send_db[0]);
        if (!strcmp(sub, "send_b") || !strcmp(sub, "send2"))
            return snprintf(buf, buf_len, "%g", (double)p->send_db[1]);
        return 0;
    }

    if (!strcmp(key, "ui_current_pad"))
        return snprintf(buf, buf_len, "%d", kit->ui_current_pad);
    if (!strcmp(key, "ui_auto_select_pad"))
        return snprintf(buf, buf_len, "%s", kit->ui_auto_select_pad ? "on" : "off");
    if (!strcmp(key, "link"))
        return snprintf(buf, buf_len, "%s", kit->link_all ? "All" : "One");
    if (!strcmp(key, "master")) return snprintf(buf, buf_len, "%g", (double)kit->master_gain);
    if (!strcmp(key, "voices")) return snprintf(buf, buf_len, "%d", dr32_kit_active_voices(kit));
    return 0;
}

/**
 * Does this per-pad field fan out under LINK?
 *
 * ⚠ THE EXCLUSIONS ARE THE WHOLE DESIGN, not caution. Link means "this knob,
 * on every pad" — so it covers the things a knob SHAPES and must not touch the
 * things that make a pad a distinct pad:
 *
 *   sample*      32 pads holding one sample is not a kit, it is a mistake that
 *                takes a kit reload to undo.
 *   note         every pad answering the same note breaks the rack outright.
 *   sending_note the same, on the way out.
 *   browse       steps through the folder the PAD's own sample sits in, so the
 *                same index means a different file per pad — fanning it out is
 *                not "the same value", it is 32 unrelated ones.
 *   play         an action, not a value.
 *
 * Everything else is a sound-shaping control and is exactly what Link is for.
 */
/** Apply one per-pad field to ONE pad. The single place that assignment
 *  happens, so LINK's fan-out and an ordinary write cannot drift apart. */
static int apply_pad_field(dr32_kit *kit, int pad, const char *sub, const char *val) {
    if (pad < 0 || pad >= DR32_PADS) return 0;
    {
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
        else if (!strcmp(sub, "end")) {          /* alias — see dr32_read_param */
            float len = f - p->play_start;
            if (len < 0.0f) len = 0.0f;
            if (len > 1.0f) len = 1.0f;
            p->play_length = len;
        }
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
        /* See the read path: `send1`/`send2` stay accepted so an older state
         * blob restores, but `send_a`/`send_b` are the names now. */
        else if (!strcmp(sub, "send_a") || !strcmp(sub, "send1")) p->send_db[0] = f;
        else if (!strcmp(sub, "send_b") || !strcmp(sub, "send2")) p->send_db[1] = f;
        /* Punch as a plain per-pad control (Josh, 2026-07-28). It is the
         * native transient shaper, so unlike a bespoke one its settings live in
         * the kit: these write Effect_Type / Effect_PunchAmount / _PunchTime and
         * round-trip into the .ablpreset.
         *
         * Amount drives the TYPE as well — at 0 the pad is a plain sampler,
         * above 0 it is a Punch pad — so a single knob turns it on. The other
         * playback effects stay dropped, so nothing else competes for fx_type. */
        else if (!strcmp(sub, "punch")) {
            p->fx_p1 = f;
            if (f > 0.0f) { p->fx_type = DR32_FX_PUNCH; if (p->fx_p2 <= 0.0f) p->fx_p2 = 0.3f; }
            else if (p->fx_type == DR32_FX_PUNCH) p->fx_type = DR32_FX_STANDARD;
        }
        else if (!strcmp(sub, "punch_time"))    p->fx_p2 = f;
        else if (!strcmp(sub, "fx_type"))       p->fx_type = dr32_fx_from_name(val);
        else if (!strcmp(sub, "fx_p1"))         p->fx_p1 = f;
        else if (!strcmp(sub, "fx_p2"))         p->fx_p2 = f;
        else if (!strcmp(sub, "play"))          dr32_kit_note_on(kit, s->note, atoi(val));
        return 1;
    }
}

static int link_fans_out(const char *sub) {
    static const char *const never[] = {
        "sample", "sample_move", "sample_user", "note", "sending_note",
        "browse", "play", NULL
    };
    for (int i = 0; never[i]; i++) if (!strcmp(sub, never[i])) return 0;
    return strncmp(sub, "ui_", 3) != 0;   /* editor state is never per-pad */
}

int dr32_apply_param(dr32_kit *kit, const char *key, const char *val) {
    if (!kit || !key || !val) return 0;

    const char *sub;
    int pad = split_pad_key(kit, key, &sub);
    if (pad >= 0) {
        int r = apply_pad_field(kit, pad, sub, val);
        /*
         * LINK: one turn sets that parameter on EVERY pad.
         *
         * The fan-out re-enters the SAME setter, once per pad — not a second
         * copy of the assignment table. A copy is how a param ends up settable
         * one way and not the other, which this file's own send_slot_index
         * comment already records happening once.
         */
        if (r && kit->link_all && link_fans_out(sub)) {
            for (int i = 0; i < DR32_PADS; i++)
                if (i != pad) apply_pad_field(kit, i, sub, val);
        }
        return r;
    }

    if (!strcmp(key, "link")) {
        kit->link_all = (!strcmp(val, "All") || atoi(val) == 1);
        return 1;
    }
    if (!strcmp(key, "ui_current_pad")) {
        int v = atoi(val);
        kit->ui_current_pad = (v < 0) ? 0 : (v >= DR32_PADS ? DR32_PADS - 1 : v);
        return 1;
    }
    /* A host that EMITS the note can name the pad outright — no correlation.
     *
     * `ui_live_press` exists because the canvas cannot say WHICH pad: a grid
     * position is not a pad, so it vouches "a finger did that" and the note
     * decides. That costs a race — the vouch and the note arrive on different
     * paths and must meet inside DR32_LIVE_MATCH_BLOCKS. Measured on device
     * 2026-07-30: 2 of 16 presses missed it (~12.5%), at a hit->change latency
     * of 55 ms against a 58 ms window.
     *
     * A SEQUENCER host has the one thing the canvas lacks: it emits the note,
     * so it knows exactly which. dAVEBOx writes that note here and focus moves
     * deterministically — no window, no arming, nothing to lose a race with.
     * (davebox declares this via `child_press_note_param` on the pads level.)
     *
     * Still the NOTE, never a pad index: note -> pad stays ours. A host that
     * derived the pad itself could only ever address 1-16, with the rows
     * mis-strided — the exact bug the alias design was built to prevent.
     *
     * Sequenced notes never reach here: davebox calls it only from its live
     * pad-press paths, which its playback path does not go through. */
    if (!strcmp(key, "ui_live_note")) {
        kit->host_vouches = 1;              /* see dr32_kit.h */
        if (!kit->ui_auto_select_pad) return 1;
        int n = atoi(val);
        if (n < 0 || n > 127) return 1;
        int pad = kit->note_to_pad[n];
        if (pad < 0 || pad >= DR32_PADS) return 1;   /* unmapped: not our note */
        kit->ui_current_pad = pad;
        /* Consume any vouch state. Under co-run BOTH mechanisms fire — the
         * canvas still observes the press and vouches — and last writer wins.
         * Clearing here stops a vouch that resolves later from crediting a
         * SEQUENCED note over the authoritative answer we just set. */
        kit->live_armed = 0;
        kit->last_hit_pad = -1;
        return 1;
    }

    if (!strcmp(key, "ui_live_press")) {
        /* ⚠⚠ TWO writers, not one. The canvas is the obvious one; the other is
         * dAVEBOx sound mode, which hosts DR32 in a chain slot and where our
         * canvas NEVER RUNS (it harvests bank_editor._test.BANKS and restores
         * the globals), so it writes this itself from its own pad handler. It
         * finds the key via `child_press_param` on the pads level of
         * module.json — see CLAUDE.md. Changing the meaning of this key, or of
         * the live_armed/last_hit_pad correlation below, breaks that SILENTLY:
         * the vouch still arrives, nothing matches it, focus just stops
         * following and nothing is logged. Measured: the window below is
         * 20 blocks x 2.902ms = 58.0ms, and a late vouch is simply lost.
         *
         * The canvas saw a physical pad press. It cannot say WHICH pad — a grid
         * position is not a pad — so the note decides, and this only vouches
         * that a finger was involved.
         *
         * The note usually beats this signal here (it comes straight off the
         * MIDI stream, while this crosses a process boundary), so look back
         * first and only arm forward if nothing recent matches. Handling just
         * one order would drop roughly half the presses. */
        kit->host_vouches = 1;              /* see dr32_kit.h */
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
