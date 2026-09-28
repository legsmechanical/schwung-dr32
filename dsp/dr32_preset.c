#include "dr32_preset.h"
#include "dr32_json.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Sample URI roots. `ableton:/packs/abl-core-library` is the factory Core
// Library, which lives OUTSIDE the user library — a resolver that only knows
// user-library silently drops every factory sample.
static const struct { const char *prefix; const char *dir; } URI_ROOTS[] = {
    { "ableton:/user-library",             "/data/UserData/UserLibrary" },
    { "ableton:/packs/abl-core-library",   "/data/CoreLibrary" },
};

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int dr32_resolve_uri(const char *uri, char *out, int out_len) {
    if (!uri || !out || out_len <= 0) return 0;
    for (size_t i = 0; i < sizeof(URI_ROOTS) / sizeof(URI_ROOTS[0]); i++) {
        size_t plen = strlen(URI_ROOTS[i].prefix);
        if (strncmp(uri, URI_ROOTS[i].prefix, plen) != 0) continue;

        int n = snprintf(out, out_len, "%s", URI_ROOTS[i].dir);
        if (n < 0 || n >= out_len) return 0;
        // percent-decode the remainder (%20 etc.)
        const char *s = uri + plen;
        while (*s && n < out_len - 1) {
            if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
                out[n++] = (char)((hexval(s[1]) << 4) | hexval(s[2]));
                s += 3;
            } else {
                out[n++] = *s++;
            }
        }
        out[n] = '\0';
        return 1;
    }
    return 0;                       // unknown root
}

static dr32_filter_type filter_from_name(const char *s) {
    if (!s) return DR32_FILT_LP24;
    if (!strcmp(s, "Lowpass 12dB")) return DR32_FILT_LP12;
    if (!strcmp(s, "Highpass"))     return DR32_FILT_HP24;
    if (!strcmp(s, "Peak"))         return DR32_FILT_PEAK;
    return DR32_FILT_LP24;          // "Lowpass" is the 24 dB slope
}

static dr32_mod_target mod_from_name(const char *s) {
    if (!s) return DR32_MOD_FILTER;
    if (!strcmp(s, "Attack")) return DR32_MOD_ATTACK;
    if (!strcmp(s, "Hold"))   return DR32_MOD_HOLD;
    if (!strcmp(s, "Decay"))  return DR32_MOD_DECAY;
    if (!strcmp(s, "FX1"))    return DR32_MOD_FX1;
    if (!strcmp(s, "FX2"))    return DR32_MOD_FX2;
    return DR32_MOD_FILTER;
}

/** The two exposed controls for the active effect. The JSON always carries ALL
 *  nine effects' params, so we pick the pair belonging to Effect_Type. */
static void effect_params(const dr32_json *p, const char *type, float *p1, float *p2) {
    *p1 = 0.0f;
    *p2 = 0.0f;
    if (!type) return;
    struct { const char *name, *k1, *k2; } map[] = {
        { "Stretch",   "Effect_StretchFactor",          "Effect_StretchGrainSize" },
        { "Loop",      "Effect_LoopOffset",             "Effect_LoopLength" },
        { "Pitch Env", "Effect_PitchEnvelopeAmount",    "Effect_PitchEnvelopeDecay" },
        { "Punch",     "Effect_PunchAmount",            "Effect_PunchTime" },
        { "8-bit",     "Effect_EightBitResamplingRate", "Effect_EightBitFilterDecay" },
        { "FM",        "Effect_FmAmount",               "Effect_FmFrequency" },
        { "Ring Mod",  "Effect_RingModAmount",          "Effect_RingModFrequency" },
        { "Sub Osc",   "Effect_SubOscAmount",           "Effect_SubOscFrequency" },
        { "Noise",     "Effect_NoiseAmount",            "Effect_NoiseFrequency" },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(type, map[i].name)) continue;
        *p1 = (float)dr32_json_num(p, map[i].k1, 0.0);
        *p2 = (float)dr32_json_num(p, map[i].k2, 0.0);
        return;
    }
}

/* ---------- PREPARE and APPLY -------------------------------------------
 *
 * A kit load is two jobs with opposite constraints. Reading the preset and
 * decoding up to 32 WAVs is SLOW (8-83 ms measured on the device, 2026-09-27)
 * and touches nothing of the kit. Writing the result into the kit is FAST and
 * must happen on the audio thread, between blocks, because the render reads
 * those pads. So prepare() does the first into a plan, on any thread, and
 * apply() does the second. dr32_preset_load is the two back to back, so the
 * synchronous load and the browser's threaded one are the same code. */

enum { PS_NONE = 0, PS_NEW, PS_KEEP };

typedef struct {
    dr32_pad params;           /* the whole pad as the preset defines it */
    int      sample;           /* PS_*: nothing, a fresh decode, or the one held */
    float   *data;             /* PS_NEW: owned until apply adopts it */
    size_t   frames;
    int      channels, rate;
    char     path[DR32_MAX_PATH];
    long     size, mtime;
} plan_pad;

struct dr32_kit_plan {
    plan_pad pad[DR32_PADS];
    /* The note writes, in FILE order: dr32_kit_set_note displaces whatever a
     * note routed before, so the order is part of the result. */
    struct { int slot, note, route; } chain[DR32_PADS];
    int count;
    dr32_preset_report rep;
};

void dr32_preset_stamps(const dr32_kit *kit, dr32_pad_stamp *held) {
    for (int i = 0; i < DR32_PADS; i++) {
        const dr32_pad_slot *s = &kit->pads[i];
        held[i].has = s->sample && s->path[0];
        if (held[i].has) memcpy(held[i].path, s->path, sizeof(held[i].path));
        else held[i].path[0] = '\0';
        held[i].size = s->src_size;
        held[i].mtime = s->src_mtime;
    }
}

void dr32_kit_plan_free(dr32_kit_plan *plan) {
    if (!plan) return;
    for (int i = 0; i < DR32_PADS; i++) free(plan->pad[i].data);
    free(plan);
}

/** Fill one pad's sample: the one the pad already holds (the decode memo — see
 *  sample_is_current in dr32_kit.c; the same size+mtime rule, decided here
 *  from a snapshot so no kit is read), or a fresh decode. */
static int plan_sample(plan_pad *pp, const dr32_pad_stamp *held, const char *file) {
    struct stat st;
    snprintf(pp->path, sizeof(pp->path), "%s", file);
    if (held && held->has && !strcmp(held->path, file) &&
        stat(file, &st) == 0 && (long)st.st_size == held->size && (long)st.st_mtime == held->mtime) {
        pp->sample = PS_KEEP;
        pp->size = held->size;
        pp->mtime = held->mtime;
        return 1;
    }
    dr32_wav w;
    if (dr32_wav_load(file, &w) != DR32_WAV_OK) return 0;
    pp->sample = PS_NEW;
    pp->data = w.data;
    pp->frames = w.frames;
    pp->channels = w.channels;
    pp->rate = w.sample_rate;
    /* Stat AFTER the read: a file rewritten between the two then looks stale
     * next time and reloads, which is the safe direction to be wrong in. */
    if (stat(file, &st) == 0) { pp->size = (long)st.st_size; pp->mtime = (long)st.st_mtime; }
    return 1;
}

dr32_kit_plan *dr32_preset_prepare(const char *path, const dr32_pad_stamp *held,
                                   int (*stop)(void *), void *ctx) {
    if (!path || !*path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (8 << 20)) { fclose(f); return NULL; }
    char *text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(f); return NULL; }
    size_t n = fread(text, 1, (size_t)size, f);
    text[n] = '\0';
    fclose(f);

    dr32_json *doc = dr32_json_parse(text);
    free(text);
    if (!doc) return NULL;

    const dr32_json *rack = dr32_json_find_kind(doc, "drumRack");
    const dr32_json *chains = rack ? dr32_json_get(rack, "chains") : NULL;
    dr32_kit_plan *plan = chains ? (dr32_kit_plan *)calloc(1, sizeof(*plan)) : NULL;
    if (!plan) { dr32_json_free(doc); return NULL; }

    /* A loaded preset defines every pad: each starts at the defaults, and any
     * pad no entry fills is left empty (PS_NONE). */
    for (int i = 0; i < DR32_PADS; i++) dr32_pad_defaults(&plan->pad[i].params);

    // ⭐ PADS ARE SEATED BY NOTE, NOT BY FILE ORDER (Josh, 2026-09-22: "can't
    // we just use the pad number ... note assignment isn't something we expose
    // to the user"). A kit file lists its pads in any order — Core Library's
    // Glide Kit lists notes 41 39 37 36 40 ... — and the pad a note lands on
    // on the Move is fixed by the note. Seating file entry i at pad i made the
    // PAD number (the big number, the header, which pad the knobs edit, and
    // child_note_base's promise to the host that pad i is note 36+i) disagree
    // with the pad actually hit. So an entry receiving note 36+s sits at pad s;
    // an entry whose note is outside 36..67, or already taken, fills the pads
    // left over, in file order. Routing is still by the note either way.
    // ⓘ A state blob saved before this numbered pads in FILE order and is not
    // remapped (Josh: "i don't care about backward compatibility or prior
    // sets"): on a kit whose file is out of note order, its edits land by pad
    // number.
    int count = dr32_json_count(chains);
    if (count > DR32_PADS) count = DR32_PADS;
    plan->count = count;
    signed char slot_of[DR32_PADS];
    /* Notes an entry of THIS kit already routes. A duplicate note does not take
     * the routing from the pad seated by it (the first entry for a note is the
     * one at that note's physical pad); it keeps its note, unrouted. */
    char routed[128];
    memset(routed, 0, sizeof(routed));
    {
        char taken[DR32_PADS], seated[DR32_PADS];
        memset(taken, 0, sizeof(taken));
        memset(seated, 0, sizeof(seated));
        for (int i = 0; i < count; i++) {
            const dr32_json *zone = dr32_json_get(dr32_json_at(chains, i), "drumZoneSettings");
            const int s = (int)dr32_json_num(zone, "receivingNote", DR32_FIRST_NOTE + i) - DR32_FIRST_NOTE;
            if (s >= 0 && s < DR32_PADS && !taken[s]) {
                slot_of[i] = (signed char)s;
                taken[s] = seated[i] = 1;
            }
        }
        /* Everything not seated by its note takes the free pads in order. */
        int free_at = 0;
        for (int i = 0; i < count; i++) {
            if (seated[i]) continue;
            while (taken[free_at]) free_at++;
            slot_of[i] = (signed char)free_at;
            taken[free_at] = 1;
        }
    }

    dr32_preset_report *rep = &plan->rep;
    for (int i = 0; i < count; i++) {
        const int slot = slot_of[i];
        plan->chain[i].slot = slot;
        plan->chain[i].note = -1;
        const dr32_json *chain = dr32_json_at(chains, i);
        if (!chain) continue;
        dr32_pad *pad = &plan->pad[slot].params;

        const dr32_json *zone = dr32_json_get(chain, "drumZoneSettings");
        int note = (int)dr32_json_num(zone, "receivingNote", DR32_FIRST_NOTE + i);
        pad->sending_note = (int)dr32_json_num(zone, "sendingNote", 60);
        const dr32_json *choke = dr32_json_get(zone, "chokeGroup");
        pad->choke_group = (choke && choke->type == DR32_JSON_NUMBER) ? (int)choke->num : 0;
        plan->chain[i].note = note;
        plan->chain[i].route = !(note >= 0 && note < 128 && routed[note]);
        if (plan->chain[i].route && note >= 0 && note < 128) routed[note] = 1;

        const dr32_json *mixer = dr32_json_get(chain, "mixer");
        pad->volume_db  = (float)dr32_json_num(mixer, "volume", 0.0);
        pad->pan        = (float)dr32_json_num(mixer, "pan", 0.0);   // -50..+50
        pad->speaker_on = dr32_json_bool(mixer, "speakerOn", 1);
        // Native kits have exactly one send (the single return chain), so it
        // maps to send 1; send 2 is DR32's extension and starts off.
        pad->send_db[0] = -70.0f;
        pad->send_db[1] = -70.0f;
        const dr32_json *sends = dr32_json_get(mixer, "sends");
        const dr32_json *s0 = dr32_json_at(sends, 0);
        if (s0) pad->send_db[0] = (float)dr32_json_num(s0, "amount", -70.0);

        const dr32_json *cell = dr32_json_find_kind(chain, "drumCell");
        if (!cell) continue;
        const dr32_json *p = dr32_json_get(cell, "parameters");

        pad->play_start    = (float)dr32_json_num(p, "Voice_PlaybackStart", 0.0);
        pad->play_length   = (float)dr32_json_num(p, "Voice_PlaybackLength", 1.0);
        pad->transpose     = (float)dr32_json_num(p, "Voice_Transpose", 0.0);
        pad->detune        = (float)dr32_json_num(p, "Voice_Detune", 0.0);
        pad->gain          = (float)dr32_json_num(p, "Voice_Gain", 1.0);
        pad->cell_volume_db= (float)dr32_json_num(p, "Volume", 0.0);
        pad->vel_to_volume = (float)dr32_json_num(p, "Voice_VelocityToVolume", 0.35);
        pad->attack        = (float)dr32_json_num(p, "Voice_Envelope_Attack", 0.0001);
        pad->hold          = (float)dr32_json_num(p, "Voice_Envelope_Hold", 0.3);
        pad->decay         = (float)dr32_json_num(p, "Voice_Envelope_Decay", 1.0);
        pad->filter_on     = dr32_json_bool(p, "Voice_Filter_On", 1);
        pad->cutoff        = (float)dr32_json_num(p, "Voice_Filter_Frequency", 22000.0);
        pad->resonance     = (float)dr32_json_num(p, "Voice_Filter_Resonance", 0.0);
        pad->peak_gain     = (float)dr32_json_num(p, "Voice_Filter_PeakGain", 1.0);
        pad->mod_amount    = (float)dr32_json_num(p, "Voice_ModulationAmount", 0.0);
        pad->pitch_to_env  = dr32_json_bool(p, "Voice_PitchToEnvelopeModulation", 0);

        const char *mode = dr32_json_str(p, "Voice_Envelope_Mode", "A-H-D");
        pad->env_mode = dr32_env_mode_parse(mode);
        pad->filter_type = filter_from_name(dr32_json_str(p, "Voice_Filter_Type", "Lowpass"));
        pad->mod_target  = mod_from_name(dr32_json_str(p, "Voice_ModulationTarget", "Filter"));

        const char *fx = dr32_json_str(p, "Effect_Type", "Standard");
        if (!dr32_json_bool(p, "Effect_On", 1)) fx = "Standard";
        pad->fx_type = dr32_fx_from_name(fx);
        effect_params(p, fx, &pad->fx_p1, &pad->fx_p2);

        const dr32_json *dd = dr32_json_get(cell, "deviceData");
        const char *uri = dr32_json_str(dd, "sampleUri", NULL);
        rep->pads++;
        if (!uri) { rep->empty++; continue; }

        char file[DR32_MAX_PATH];
        if (!dr32_resolve_uri(uri, file, sizeof(file))) {
            rep->unresolved++;
            continue;
        }
        /* Superseded (a newer kit was asked for): stop reading. */
        if (stop && stop(ctx)) { dr32_json_free(doc); dr32_kit_plan_free(plan); return NULL; }
        if (plan_sample(&plan->pad[slot], held ? &held[slot] : NULL, file)) rep->loaded++;
        else rep->failed++;
    }
    dr32_json_free(doc);
    return plan;
}

void dr32_preset_apply(dr32_kit *kit, dr32_kit_plan *plan, dr32_preset_report *rep) {
    if (rep) *rep = plan->rep;

    // Start from a clean kit: a loaded preset defines every pad.
    //
    // ⚠ The SAMPLES are replaced at the END, and a pad whose sample is the one
    // it already holds (PS_KEEP) is not touched at all — that is the decode
    // memo: reloading the same kit used to re-decode all 16 WAVs on the SPI
    // callback (measured on device, ~3.5 ms against a 2.9 ms block).
    dr32_kit_all_off(kit);
    for (int i = 0; i < DR32_PADS; i++) {
        /* A Move kit is samples only, so every synth pad becomes a sample pad
         * again — before the params land, so dropping it cannot touch them. */
        dr32_kit_drop_engine(kit, i);
        kit->pads[i].params = plan->pad[i].params;
        dr32_kit_set_note(kit, i, DR32_FIRST_NOTE + i);
    }
    for (int i = 0; i < plan->count; i++) {
        const int slot = plan->chain[i].slot, note = plan->chain[i].note;
        if (note < 0) continue;
        if (plan->chain[i].route) {
            dr32_kit_set_note(kit, slot, note);
        } else {
            const int old = kit->pads[slot].note;           /* its placeholder, 36+slot */
            if (old >= 0 && old < 128 && kit->note_to_pad[old] == slot) kit->note_to_pad[old] = -1;
            kit->pads[slot].note = note;
        }
    }

    for (int i = 0; i < DR32_PADS; i++) {
        plan_pad *pp = &plan->pad[i];
        const dr32_pad_slot *s = &kit->pads[i];
        if (pp->sample == PS_NEW) {
            dr32_kit_adopt_sample(kit, i, pp->data, pp->frames, pp->channels, pp->rate,
                                  pp->path, pp->size, pp->mtime);
            pp->data = NULL;                      /* the kit owns it now */
        } else if (pp->sample == PS_KEEP && s->sample && !strcmp(s->path, pp->path) &&
                   s->src_size == pp->size && s->src_mtime == pp->mtime) {
            /* Still exactly what the snapshot saw: keep the buffer. */
        } else {
            /* Empty — or a PS_KEEP whose pad changed since the snapshot (a
             * sample written while the kit was being read), which is now the
             * wrong audio: better an empty pad than someone else's sample. */
            if (pp->sample == PS_KEEP && rep) { rep->loaded--; rep->failed++; }
            dr32_kit_load_sample(kit, i, NULL);
        }
    }
}

int dr32_preset_load(dr32_kit *kit, const char *path, dr32_preset_report *rep) {
    if (rep) memset(rep, 0, sizeof(*rep));
    if (!kit || !path || !*path) return 0;
    static dr32_pad_stamp held[DR32_PADS];   /* 17 KB: not on the callback's stack */
    dr32_preset_stamps(kit, held);
    dr32_kit_plan *plan = dr32_preset_prepare(path, held, NULL, NULL);
    if (!plan) return 0;
    dr32_preset_apply(kit, plan, rep);
    dr32_kit_plan_free(plan);
    return 1;
}
