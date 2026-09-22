/*
 * urchin_engine.cpp — URCHIN's three voices as DR32 engines.
 *
 * The DSP is Punk Labs' OneTrick URCHIN 1.0.2 (GPL-3.0-or-later), vendored from
 * schwung-urchin: faust/ is its source, generated/ is Faust's output
 * (scripts/gen_engines.sh), factory_bank.h its kits in display units.
 *
 * ⭑ THREE ENGINES, NOT ONE, because they are three DSPs with three different
 * parameter sets:
 *   - DRUM   (kick, toms): a physically modelled shell with a resonant head.
 *   - SNARE: the SAME source built with ENABLE_SNARE_FEATURES, so it carries
 *            snare wires the drum build does not, and one extra control, Rim
 *            (Strike_Rimshot: a clonk layered on, the shell ring lifted).
 *   - CYMBAL (hats, cymbals): a bank of comb-filtered noise partials — nothing
 *            above the Sample_* rows is shared with a drum. Closed is the
 *            hat-pedal articulation, a decay scaler from x1 down to x0.05.
 * Each gets its own key prefix, so its knobs are its own in the hierarchy
 * (a repeated key anywhere kills the host's whole metadata load).
 *
 * ⭑ A PAD HERE IS A WHOLE VOICE. URCHIN proper seats sixteen pads on nine
 * voices, which is why its articulations had to be per-pad values written at
 * each hit. In DR32 every pad owns its instance, so Rim and Closed are just
 * parameters.
 *
 * WHAT DR32 TAKES OVER: Voice_Gain / Voice_Pan / Voice_Reverb are pinned (0 dB,
 * centre, 0) — the pad's DR32 Volume and Pan do those jobs, and the reverb was
 * the kit master stage, which is not here. A model's gain and pan become the
 * pad's starting Volume and Pan. Velocity stays inside the model (URCHIN has no
 * gain-dynamics control: velocity is strike energy).
 *
 * 🔴 PITCH: a drum's pad offset is folded into the UNSMOOTHED `Tuning` zone as
 * a frequency ratio. The obvious home, `Transpose`, feeds shared.lib's
 * pitchShift, which is smoothed (10 ms T60) — an offset written there GLIDES
 * over the attack, a bloop on a kick. A cymbal has no Tuning, so its offset
 * does ride Transpose; under a soft inharmonic attack that settle is inaudible.
 */
#include <cmath>
#include <new>

#include "faust_voice.h"
#include "urchin/generated/urchin_drum.hpp"
#include "urchin/generated/urchin_snare.hpp"
#include "urchin/generated/urchin_cymbal.hpp"
#include "urchin/factory_bank.h"

#include "../dr32_engine.h"

namespace {

/* One parameter: its DR32 face, its zone, the zone's unit factor, and the
 * column it lives in in URCHIN_FACTORY's per-voice run. */
struct Row { dr32_eparam p; const char *zone; float scale; int col; };

/* ⚠ ORDER IS THE PAGE ORDER AND THE STATE ORDER. Append; never insert.
 * Pages follow schwung-urchin's own (Drum / Shell / Chop). `col` indexes the
 * port's DRUM_PARAMS row. */
#define DRUM_ROWS(P) \
    /* --- Drum: the hit. --- */ \
    {{P "pitch",      "Pitch",       "PITCH",  40,  240,  120, 1, "hz", "Drum"},  "Tuning",            1.0f,    0}, \
    {{P "decay",      "Decay",       "DECAY", 150, 5000,  500, 1, "ms", "Drum"},  "Decay",             0.001f,  6}, \
    {{P "beater",     "Beater Damp", "BEATR",   0,  100,   50, 1, "%",  "Drum"},  "Beater_Dampening",  1.0f,    7}, \
    {{P "detune",     "Detune",      "DETUN",   0,   32,    8, 1, "st", "Drum"},  "Detune_Range",      1.0f,    5}, \
    {{P "punch",      "Punch",       "PUNCH",   0,  100,    0, 1, "%",  "Drum"},  "Voice_Punchiness",  1.0f,    8}, \
    {{P "strike",     "Strike Amt",  "STRKE",   0,  100,  100, 1, "%",  "Drum"},  "Mix_Strike",        1.0f,    9}, \
    {{P "strike_br",  "Strike Tone", "STONE",   0,  100,   75, 1, "%",  "Drum"},  "Strike_Brightness", 1.0f,   10}, \
    /* --- Shell: the body and the resonant head. --- */ \
    {{P "shell",      "Shell Size",  "SHELL",   5,   24,   12, 1, "in", "Shell"}, "Shell_Depth",       1.0f,    1}, \
    {{P "shell_damp", "Shell Damp",  "SDAMP",   0,  100,   30, 1, "%",  "Shell"}, "Shell_Damp",        1.0f,    2}, \
    {{P "ring",       "Ring",        "RING",    0,  100,   35, 1, "%",  "Shell"}, "Mix_Ring",          1.0f,   11}, \
    {{P "reso_head",  "Reso Head",   "RHEAD",   0,    1,    1, 1, NULL, "Shell"}, "Reso_Head",         1.0f,    3}, \
    {{P "reso_tune",  "Reso Tune",   "RTUNE",  -6,    6,    0, 1, "st", "Shell"}, "Reso_Tuning",       1.0f,    4}, \
    {{P "reso_mix",   "Reso Mix",    "RMIX",    0,  100,  100, 1, "%",  "Shell"}, "Mix_Reso",          1.0f,   12}, \
    CHOP_ROWS(P, 13, 14, 15, 16, 17, 18)

/* --- Chop: URCHIN's per-voice "sampler tail", the lo-fi half. Filters are
 * kHz and lengths seconds in the DSP; Hz and ms here so an integer knob has
 * somewhere to go. --- */
#define CHOP_ROWS(P, c0, c1, c2, c3, c4, c5) \
    {{P "lowpass",    "Lowpass",     "LOPAS", 2000, 20000, 20000, 1, "hz", "Chop"}, "Sample_Cutoff",   0.001f, c0}, \
    {{P "tone_freq",  "Tone Freq",   "TFREQ", 1000, 12000,  1500, 1, "hz", "Chop"}, "Sample_ToneFreq", 0.001f, c1}, \
    {{P "tone_gain",  "Tone Gain",   "TGAIN",    0,     6,     0, 1, "dB", "Chop"}, "Sample_ToneGain", 1.0f,   c2}, \
    {{P "slice",      "Slice",       "SLICE",    0,  3000,     0, 1, "ms", "Chop"}, "Sample_Length",   0.001f, c3}, \
    {{P "chop_speed", "Chop Speed",  "SPEED",  -12,    12,     0, 1, "st", "Chop"}, "Sample_Speed",    1.0f,   c4}, \
    {{P "late",       "Late",        "LATE",     0,    20,     0, 1, "ms", "Chop"}, "Lateness",        1.0f,   c5}

const Row DRUM[] = { DRUM_ROWS("ud_") };

/* The snare: the drum's rows under its own prefix, plus Rim, which rides on
 * the Drum page as its eighth knob (it is the only snare-specific control
 * Faust leaves alive — see schwung-urchin). Rim has no bank column: every
 * model starts it from the model's own value below. */
const Row SNARE[] = {
    DRUM_ROWS("us_"),
    {{"us_rim", "Rim", "RIM", 0, 100, 0, 1, "%", "Drum"}, "Strike_Rimshot", 0.01f, -1},
};

/* The cymbal's page carries Closed in place of a drum's Detune. `col`
 * indexes the port's CYMBAL_PARAMS row. */
const Row CYMBAL[] = {
    {{"uc_size",      "Size",        "SIZE",   10,   24,   14, 1, "in", "Cymbal"}, "Cymbal_Size",       1.0f,  0},
    {{"uc_crash",     "Crash",       "CRASH",   0,  100,   50, 1, "%",  "Cymbal"}, "Cymbal_Crash",      1.0f,  1},
    {{"uc_damp",      "Damp",        "DAMP",    0,  100,    0, 1, "%",  "Cymbal"}, "Damp",              1.0f,  2},
    {{"uc_closed",    "Closed",      "CLOSD",   0,  100,    0, 1, "%",  "Cymbal"}, "Closed",            0.01f, -1},
    {{"uc_punch",     "Punch",       "PUNCH",   0,  100,    0, 1, "%",  "Cymbal"}, "Voice_Punchiness",  1.0f,  3},
    {{"uc_strike",    "Strike Amt",  "STRKE",   0,  100,  100, 1, "%",  "Cymbal"}, "Mix_Strike",        1.0f,  4},
    {{"uc_strike_br", "Strike Tone", "STONE",   0,  100,   75, 1, "%",  "Cymbal"}, "Strike_Brightness", 1.0f,  5},
    CHOP_ROWS("uc_", 6, 7, 8, 9, 10, 11),
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static_assert(COUNT(SNARE) <= DR32_ENG_MAX_PARAMS, "too many params");

/* URCHIN_FACTORY run widths and the gain/pan columns DR32 takes over. */
enum { DRUM_W = 24, CYM_W = 17,
       DRUM_GAIN = 19, DRUM_PAN = 20, CYM_GAIN = 12, CYM_PAN = 13 };

struct Engine {
    const Row  *rows;
    int         n;
    dr32_eparam params[DR32_ENG_MAX_PARAMS];
    void init(const Row *r, int count) {
        rows = r; n = count;
        for (int i = 0; i < n; i++) params[i] = r[i].p;
    }
};

struct Tables {
    Engine drum, snare, cymbal;
    Tables() { drum.init(DRUM, COUNT(DRUM)); snare.init(SNARE, COUNT(SNARE)); cymbal.init(CYMBAL, COUNT(CYMBAL)); }
};
const Tables T;

struct Voice {
    Dr32FaustVoice fv;
    const Engine  *eng;
    int            kind;               /* DR32_ENG_URCHIN_* */
    FAUSTFLOAT    *zone[DR32_ENG_MAX_PARAMS];
    float          value[DR32_ENG_MAX_PARAMS];
    FAUSTFLOAT    *z_tuning, *z_transpose;
    int            pitch_row;          /* DRUM/SNARE: the Tuning row, else -1 */
    int            closed_row;         /* CYMBAL: the Closed row, else -1 */
};

void *create_kind(int kind, dsp *d, const Engine *e, int sr) {
    if (!d) return nullptr;
    Voice *v = new (std::nothrow) Voice();
    if (!v) { delete d; return nullptr; }
    v->eng = e;
    v->kind = kind;
    dr32_fv_setup(&v->fv, d, sr);
    v->pitch_row = -1;
    v->closed_row = -1;
    for (int i = 0; i < e->n; i++) {
        v->zone[i] = v->fv.zones.find(e->rows[i].zone);
        v->value[i] = e->rows[i].p.def;
        dr32_fv_set_zone(v->zone[i], e->rows[i].p.def * e->rows[i].scale);
        if (!strcmp(e->rows[i].zone, "Tuning")) v->pitch_row = i;
        if (!strcmp(e->rows[i].zone, "Closed")) v->closed_row = i;
    }
    v->z_tuning = v->fv.zones.find("Tuning");
    v->z_transpose = v->fv.zones.find("Transpose");
    /* Pinned: DR32 owns level, pan and sends (see the header). */
    dr32_fv_set_zone(v->fv.zones.find("Voice_Gain"), 0.0f);
    dr32_fv_set_zone(v->fv.zones.find("Voice_Pan"), 0.0f);
    dr32_fv_set_zone(v->fv.zones.find("Voice_Reverb"), 0.0f);
    return v;
}

void *create_drum(int sr)   { return create_kind(DR32_ENG_URCHIN_DRUM,   new (std::nothrow) UrchinDrum(),   &T.drum,   sr); }
void *create_snare(int sr)  { return create_kind(DR32_ENG_URCHIN_SNARE,  new (std::nothrow) UrchinSnare(),  &T.snare,  sr); }
void *create_cymbal(int sr) { return create_kind(DR32_ENG_URCHIN_CYMBAL, new (std::nothrow) UrchinCymbal(), &T.cymbal, sr); }

void destroy(void *e) {
    Voice *v = static_cast<Voice *>(e);
    if (!v) return;
    delete v->fv.d;
    delete v;
}

void set(void *e, int idx, float display) {
    Voice *v = static_cast<Voice *>(e);
    if (!v || idx < 0 || idx >= v->eng->n) return;
    const Row &r = v->eng->rows[idx];
    float x = dr32_fv_clamp(display, r.p.min, r.p.max);
    v->value[idx] = x;
    dr32_fv_set_zone(v->zone[idx], x * r.scale);
}

void note_on(void *e, float vel01, float tune_st) {
    Voice *v = static_cast<Voice *>(e);
    if (!v) return;
    /* A choke may have borrowed a zone (a cymbal's Closed); every hit starts
     * from the pad's own values. */
    if (v->closed_row >= 0) {
        const Row &r = v->eng->rows[v->closed_row];
        dr32_fv_set_zone(v->zone[v->closed_row], v->value[v->closed_row] * r.scale);
    }
    if (v->pitch_row >= 0 && v->z_tuning) {
        /* Deliberately wider than the slider's 40..240: that is where the
         * model is a drum, and an offset may walk outside it. 20..480 is a
         * stability guard, not a musical one (the port's own clamp). */
        float hz = v->value[v->pitch_row] * powf(2.0f, tune_st / 12.0f);
        *v->z_tuning = (FAUSTFLOAT)dr32_fv_clamp(hz, 20.0f, 480.0f);
    } else {
        dr32_fv_set_zone(v->z_transpose, dr32_fv_clamp(tune_st, -12.0f, 12.0f));
    }
    dr32_fv_note_on(&v->fv, vel01);
}

/* No Choke zone survives in any URCHIN build (Faust prunes it). DR32's own
 * ramp does the audible cut; this only shortens what keeps computing, muted,
 * underneath it. A cymbal has the perfect lever — Closed, the hat pedal,
 * decay x0.05 — so a choked open hat dies the way a real one does when the
 * pedal comes down. A drum has none and simply rings out under the gate. */
void choke(void *e) {
    Voice *v = static_cast<Voice *>(e);
    if (!v) return;
    if (v->fv.z_trigger) *v->fv.z_trigger = 0.0f;
    v->fv.pending = -1.0f;
    if (v->closed_row >= 0) dr32_fv_set_zone(v->zone[v->closed_row], 1.0f);
}

int render(void *e, float *out, int n) {
    Voice *v = static_cast<Voice *>(e);
    return v ? dr32_fv_render(&v->fv, out, n) : 0;
}

/* ---- models: the port's "Init" kit (Punk Labs' own starting point) ------ */

/* Voice runs in URCHIN_FACTORY, in order: Kick, Snare A, Snare B, Low Tom,
 * Mid Tom, Hi Tom (DRUM_W values each), Hihat, Cymbal A, Cymbal B (CYM_W). */
int run_offset(int voice) { return voice < 6 ? voice * DRUM_W : 6 * DRUM_W + (voice - 6) * CYM_W; }

struct ModelSrc { const char *slug, *name; int engine; int voice; float artic; };
/* Picker order. `artic` is Rim (snare) or Closed (cymbal), 0..100 — the
 * articulations URCHIN proper writes per pad, which here make distinct
 * models of one voice: the rimshot, and the closed hat. */
const ModelSrc SRC[] = {
    {"urchin/kick",       "Kick",       DR32_ENG_URCHIN_DRUM,   0,   0},
    {"urchin/snare",      "Snare",      DR32_ENG_URCHIN_SNARE,  2,   0},
    {"urchin/rimshot",    "Rimshot",    DR32_ENG_URCHIN_SNARE,  1, 100},
    {"urchin/low_tom",    "Low Tom",    DR32_ENG_URCHIN_DRUM,   3,   0},
    {"urchin/mid_tom",    "Mid Tom",    DR32_ENG_URCHIN_DRUM,   4,   0},
    {"urchin/high_tom",   "High Tom",   DR32_ENG_URCHIN_DRUM,   5,   0},
    {"urchin/hat_closed", "Closed Hat", DR32_ENG_URCHIN_CYMBAL, 6, 100},
    {"urchin/hat_open",   "Open Hat",   DR32_ENG_URCHIN_CYMBAL, 6,   0},
    {"urchin/cymbal",     "Cymbal",     DR32_ENG_URCHIN_CYMBAL, 7,   0},
};
const int NM = COUNT(SRC);

struct Models {
    float      values[NM][DR32_ENG_MAX_PARAMS];
    dr32_model m[NM];
    Models() {
        const float *run = URCHIN_FACTORY[0].voice;
        for (int i = 0; i < NM; i++) {
            const Engine *e = SRC[i].engine == DR32_ENG_URCHIN_DRUM  ? &T.drum
                            : SRC[i].engine == DR32_ENG_URCHIN_SNARE ? &T.snare : &T.cymbal;
            const float *row = run + run_offset(SRC[i].voice);
            for (int k = 0; k < e->n; k++)
                values[i][k] = e->rows[k].col >= 0 ? row[e->rows[k].col] : SRC[i].artic;
            int cym = SRC[i].engine == DR32_ENG_URCHIN_CYMBAL;
            m[i].slug = SRC[i].slug;
            m[i].name = SRC[i].name;
            m[i].engine = SRC[i].engine;
            m[i].values = values[i];
            m[i].volume_db = row[cym ? CYM_GAIN : DRUM_GAIN];
            m[i].pan = row[cym ? CYM_PAN : DRUM_PAN] * 0.5f;   /* ±100 -> ±50 */
        }
    }
};
const Models M;

}  // namespace

extern "C" {

const dr32_engine_ops dr32_engine_urchin_drum = {
    DR32_ENG_URCHIN_DRUM, "urchin_drum", "Urchin Drum", "ud_", COUNT(DRUM), T.drum.params,
    create_drum, destroy, set, note_on, choke, render,
};
const dr32_engine_ops dr32_engine_urchin_snare = {
    DR32_ENG_URCHIN_SNARE, "urchin_snare", "Urchin Snare", "us_", COUNT(SNARE), T.snare.params,
    create_snare, destroy, set, note_on, choke, render,
};
const dr32_engine_ops dr32_engine_urchin_cymbal = {
    DR32_ENG_URCHIN_CYMBAL, "urchin_cymbal", "Urchin Cymbal", "uc_", COUNT(CYMBAL), T.cymbal.params,
    create_cymbal, destroy, set, note_on, choke, render,
};

const dr32_model *dr32_urchin_models(int *count) {
    if (count) *count = NM;
    return M.m;
}

void dr32_urchin_class_init(int sample_rate) {
    static int done = 0;
    if (done) return;
    /* Each class fills its own 256 KB sine table. */
    UrchinDrum::classInit(sample_rate);
    UrchinSnare::classInit(sample_rate);
    UrchinCymbal::classInit(sample_rate);
    done = 1;
}

}
