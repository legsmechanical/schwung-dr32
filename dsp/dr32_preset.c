#include "dr32_preset.h"
#include "dr32_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int dr32_preset_load(dr32_kit *kit, const char *path, dr32_preset_report *rep) {
    if (rep) memset(rep, 0, sizeof(*rep));
    if (!kit || !path || !*path) return 0;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (8 << 20)) { fclose(f); return 0; }
    char *text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(f); return 0; }
    size_t n = fread(text, 1, (size_t)size, f);
    text[n] = '\0';
    fclose(f);

    dr32_json *doc = dr32_json_parse(text);
    free(text);
    if (!doc) return 0;

    const dr32_json *rack = dr32_json_find_kind(doc, "drumRack");
    if (!rack) { dr32_json_free(doc); return 0; }
    const dr32_json *chains = dr32_json_get(rack, "chains");
    if (!chains) { dr32_json_free(doc); return 0; }

    // Start from a clean kit: a loaded preset defines every pad.
    //
    // ⚠ The SAMPLES are cleared at the END, not here, and that ordering is the
    // whole point of dr32_kit_load_sample's decode memo. Clearing up front frees
    // every buffer, so by the time the real samples load there is nothing left to
    // compare against and the memo can never hit — which is exactly what it did
    // before this change: reloading the same kit re-decoded all 16 WAVs on the
    // SPI callback (measured on device, ~3.5 ms against a 2.9 ms block).
    //
    // Params and note mapping ARE still reset here. Only the buffers wait, and
    // every pad the preset does not fill is cleared below, so "a loaded preset
    // defines every pad" still holds exactly.
    dr32_kit_all_off(kit);
    for (int i = 0; i < DR32_PADS; i++) {
        /* A Move kit is samples only, so every synth pad becomes a sample pad
         * again. HERE, before the params below are read in: dropping it later,
         * inside load_sample, would be after this pad's values were set. */
        dr32_kit_drop_engine(kit, i);
        dr32_pad_defaults(&kit->pads[i].params);
        dr32_kit_set_note(kit, i, DR32_FIRST_NOTE + i);
    }
    /* Which pads this preset actually gave a sample. Anything still 0 after the
     * loop is cleared — an absent, unresolvable or failed sampleUri all land
     * there, so a pad that used to hold audio does not keep it. */
    char filled[DR32_PADS];
    memset(filled, 0, sizeof(filled));

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

    for (int i = 0; i < count; i++) {
        const dr32_json *chain = dr32_json_at(chains, i);
        if (!chain) continue;
        const int slot = slot_of[i];
        dr32_pad *pad = &kit->pads[slot].params;

        const dr32_json *zone = dr32_json_get(chain, "drumZoneSettings");
        int note = (int)dr32_json_num(zone, "receivingNote", DR32_FIRST_NOTE + i);
        pad->sending_note = (int)dr32_json_num(zone, "sendingNote", 60);
        const dr32_json *choke = dr32_json_get(zone, "chokeGroup");
        pad->choke_group = (choke && choke->type == DR32_JSON_NUMBER) ? (int)choke->num : 0;
        if (note >= 0 && note < 128 && routed[note]) {
            const int old = kit->pads[slot].note;           /* its placeholder, 36+slot */
            if (old >= 0 && old < 128 && kit->note_to_pad[old] == slot) kit->note_to_pad[old] = -1;
            kit->pads[slot].note = note;
        } else {
            dr32_kit_set_note(kit, slot, note);
            if (note >= 0 && note < 128) routed[note] = 1;
        }

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
        pad->env_mode = (mode && !strcmp(mode, "A-S-R")) ? DR32_ENV_ASR : DR32_ENV_AHD;
        pad->filter_type = filter_from_name(dr32_json_str(p, "Voice_Filter_Type", "Lowpass"));
        pad->mod_target  = mod_from_name(dr32_json_str(p, "Voice_ModulationTarget", "Filter"));

        const char *fx = dr32_json_str(p, "Effect_Type", "Standard");
        if (!dr32_json_bool(p, "Effect_On", 1)) fx = "Standard";
        pad->fx_type = dr32_fx_from_name(fx);
        effect_params(p, fx, &pad->fx_p1, &pad->fx_p2);

        // Sample last: loading resets the pad's voice.
        const dr32_json *dd = dr32_json_get(cell, "deviceData");
        const char *uri = dr32_json_str(dd, "sampleUri", NULL);
        if (rep) rep->pads++;
        if (!uri) { if (rep) rep->empty++; continue; }

        char file[DR32_MAX_PATH];
        if (!dr32_resolve_uri(uri, file, sizeof(file))) {
            if (rep) rep->unresolved++;
            continue;
        }
        if (dr32_kit_load_sample(kit, slot, file) == DR32_WAV_OK) {
            filled[slot] = 1;
            if (rep) rep->loaded++;
        } else if (rep) {
            rep->failed++;
        }
    }

    /* Every pad this preset did not fill goes empty — including pads past
     * `count`, and pads whose sampleUri was missing, unresolvable or failed to
     * load. This is the clear that used to happen up front. */
    for (int i = 0; i < DR32_PADS; i++)
        if (!filled[i]) dr32_kit_load_sample(kit, i, NULL);

    dr32_json_free(doc);
    return 1;
}
