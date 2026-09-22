/*
 * 8w8_engine.cpp — the 8W8 (TR-808 style) lanes as DR32 engines.
 *
 * The machine is schwung-8W8 (GPL-3.0), vendored unmodified under 8w8/: its
 * circuit models and voice orchestration (sc808_engine.cpp). See kit_port.h
 * for what "one lane per pad" keeps and drops, and 6w6_engine.cpp for why the
 * engine is wrapped in a namespace.
 *
 * ⭑ THE METAL BANK IS PER PAD. On the hardware one free-running six-square
 * bank feeds both hats, the cymbal and the cowbell, and the machine ticks it
 * once per sample for all four. Here each pad owns a whole machine, so a hat
 * pad has its own bank and it runs while the pad sounds. Two hat PADS
 * therefore no longer share oscillators the way the machine's two hats do —
 * the price of playing lanes on separate pads.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wunused-private-field"
#else
/* The machine's engine struct holds its lane tables, whose types are in an
 * anonymous namespace. Compiled as its own file that is silent; #included,
 * GCC counts the struct as a header's. It is defined in this TU alone. */
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#endif
namespace dr32_8w8 {
#include "8w8/sc808_engine.cpp"
}
#pragma GCC diagnostic pop

#include "kit_port.h"
#include "../dr32_engine.h"

using namespace dr32_8w8;

namespace {

const char *const DIST = "Diode|Clip|SAT|BFZ|PDIST|Fold|Crush";

/* ⚠ ORDER IS THE PAGE ORDER AND THE STATE ORDER within a lane. One page for
 * the whole machine ("Voice"): the shared knobs show on every lane, a lane's
 * own extra only on that lane (tools/gen_engine_ui.mjs gates it). */
struct Row {
    const char *key;        /* DR32 key                          */
    const char *name, *short_name;
    const char *ekey;       /* the machine's key; "%s" = lane id  */
    const char *options;    /* enum, or NULL                      */
};

const Row TUNE  = {"e8_tune",  "Tune",       "TUNE",  "%s_tune",      nullptr};
const Row DECAY = {"e8_decay", "Decay",      "DECAY", "%s_decay",     nullptr};
const Row DRIVE = {"e8_drive", "Drive",      "DRIVE", "%s_drive",     nullptr};
const Row DISTR = {"e8_dist",  "Distortion", "DIST",  "%s_dist_type", DIST};
/* Velocity is the machine's MASTER control, here per pad. On most 8W8 lanes
 * it is a trigger VOLTAGE (a harder hit is a different sound), which is why it
 * stays the machine's and is not DR32's post-voice Vel Vol. */
const Row VEL   = {"e8_vel",   "Velocity",   "VEL",   "vel_depth",    nullptr};

const Row BD_ATTACK = {"e8_bd_attack", "Attack", "ATTCK", "bd_attack", nullptr};
const Row BD_TONE   = {"e8_bd_tone",   "Tone",   "TONE",  "bd_tone",   nullptr};
const Row SD_SNAPPY = {"e8_sd_snappy", "Snappy", "SNAPY", "sd_snappy", nullptr};
const Row MA_ATTACK = {"e8_ma_attack", "Attack", "ATTCK", "ma_attack", nullptr};

struct Lane {
    int         voice;      /* SC808_* */
    const char *id, *slug, *name;
    int         tune_kind;
    const Row  *rows[8];
};

/* The machine's lane order, which is the picker's. Tune is semitones on
 * every lane but the four metal ones, where it is the bank's ratio. */
#define STD(x) {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL}
const Lane LANES[] = {
    {SC808_BD, "bd", "8w8/kick",       "Bass Drum",  KP_TUNE_ST,    {&TUNE, &DECAY, &BD_ATTACK, &BD_TONE, &DRIVE, &DISTR, &VEL}},
    {SC808_SD, "sd", "8w8/snare",      "Snare",      KP_TUNE_ST,    {&TUNE, &DECAY, &SD_SNAPPY, &DRIVE, &DISTR, &VEL}},
    {SC808_LT, "lt", "8w8/low_tom",    "Low Tom",    KP_TUNE_ST,    STD()},
    {SC808_MT, "mt", "8w8/mid_tom",    "Mid Tom",    KP_TUNE_ST,    STD()},
    {SC808_HT, "ht", "8w8/hi_tom",     "Hi Tom",     KP_TUNE_ST,    STD()},
    {SC808_LC, "lc", "8w8/low_conga",  "Low Conga",  KP_TUNE_ST,    STD()},
    {SC808_MC, "mc", "8w8/mid_conga",  "Mid Conga",  KP_TUNE_ST,    STD()},
    {SC808_HC, "hc", "8w8/hi_conga",   "Hi Conga",   KP_TUNE_ST,    STD()},
    {SC808_RS, "rs", "8w8/rimshot",    "Rim Shot",   KP_TUNE_ST,    STD()},
    {SC808_CL, "cl", "8w8/claves",     "Claves",     KP_TUNE_ST,    STD()},
    {SC808_MA, "ma", "8w8/maracas",    "Maracas",    KP_TUNE_ST,    {&TUNE, &DECAY, &MA_ATTACK, &DRIVE, &DISTR, &VEL}},
    {SC808_CP, "cp", "8w8/clap",       "Hand Clap",  KP_TUNE_ST,    STD()},
    {SC808_CB, "cb", "8w8/cowbell",    "Cowbell",    KP_TUNE_RATIO, STD()},
    {SC808_CH, "ch", "8w8/hat_closed", "Closed Hat", KP_TUNE_RATIO, STD()},
    {SC808_OH, "oh", "8w8/hat_open",   "Open Hat",   KP_TUNE_RATIO, STD()},
    {SC808_CY, "cy", "8w8/cymbal",     "Cymbal",     KP_TUNE_RATIO, STD()},
};
#undef STD
const int NL = (int)(sizeof(LANES) / sizeof(LANES[0]));
static_assert(sizeof(LANES) / sizeof(LANES[0]) == SC808_NUM_VOICES, "one engine per lane");

int nrows(const Lane &l) { int n = 0; while (n < 8 && l.rows[n]) n++; return n; }

/* A row's machine key for this lane. */
void ekey(const Lane &l, const Row &r, char *out, size_t len) { snprintf(out, len, r.ekey, l.id); }

/* The machine's default for a row, in DR32 display units (pot or enum index). */
float row_default(const Lane &l, const Row &r) {
    char k[48];
    ekey(l, r, k, sizeof k);
    const int ps = find_pot(k);
    if (ps >= 0) return (float)g_sc808_pots[ps].def;
    const int es = find_enum(k);
    return es >= 0 ? (float)g_sc808_enums[es].def : 0.0f;
}

int enum_count(const Lane &l, const Row &r) {
    char k[48];
    ekey(l, r, k, sizeof k);
    const int es = find_enum(k);
    return es >= 0 ? g_sc808_enums[es].count : 0;
}

struct Tables {
    dr32_eparam params[SC808_NUM_VOICES][DR32_ENG_MAX_PARAMS];
    float       defaults[SC808_NUM_VOICES][DR32_ENG_MAX_PARAMS];
    char        names[SC808_NUM_VOICES][32];
    char        slugs[SC808_NUM_VOICES][24];
    Tables() {
        for (int l = 0; l < NL; l++) {
            const Lane &ln = LANES[l];
            for (int i = 0; i < nrows(ln); i++) {
                const Row &r = *ln.rows[i];
                const int ne = r.options ? enum_count(ln, r) : 0;
                params[l][i] = dr32_eparam{r.key, r.name, r.short_name, 0.0f,
                                           ne ? (float)(ne - 1) : 127.0f, row_default(ln, r),
                                           1.0f, nullptr, "Voice", r.options};
                defaults[l][i] = params[l][i].def;
            }
            snprintf(names[l], sizeof names[l], "8W8 %s", ln.name);
            snprintf(slugs[l], sizeof slugs[l], "8w8_%s", ln.id);
        }
    }
};
const Tables T;

struct Pad {
    sc808_engine_t *e;
    int     lane;
    int     tune_slot;
    float   st;            /* the pad's transpose, applied to Tune          */
    float   vol;           /* the machine's master volume at its default   */
    kp_gate gate;
};

void apply_tune(Pad *p) {
    const int s = p->tune_slot;
    if (s < 0) return;
    p->e->potv[s] = kp_tune(pot_value(s, p->e->pot[s]), LANES[p->lane].tune_kind, p->st);
}

void *create_lane(int lane, int sr) {
    Pad *p = (Pad *)calloc(1, sizeof(Pad));
    if (!p) return nullptr;
    p->e = sc808_create((float)sr);
    if (!p->e) { free(p); return nullptr; }
    p->lane = lane;
    char k[48];
    snprintf(k, sizeof k, "%s_tune", LANES[lane].id);
    p->tune_slot = find_pot(k);
    p->vol = p->e->potv[p->e->p_volume];
    kp_gate_init(&p->gate, sr);
    return p;
}

template <int L> void *create(int sr) { return create_lane(L, sr); }

void destroy(void *v) {
    Pad *p = static_cast<Pad *>(v);
    if (!p) return;
    sc808_destroy(p->e);
    free(p);
}

void set(void *v, int idx, float display) {
    Pad *p = static_cast<Pad *>(v);
    const Lane &ln = LANES[p ? p->lane : 0];
    if (!p || idx < 0 || idx >= nrows(ln)) return;
    char k[48], val[16];
    ekey(ln, *ln.rows[idx], k, sizeof k);
    snprintf(val, sizeof val, "%d", (int)lrintf(display));
    sc808_set_param(p->e, k, val);
    if (ln.rows[idx] == &TUNE) apply_tune(p);
}

void note_on(void *v, float vel01, float tune_st) {
    Pad *p = static_cast<Pad *>(v);
    if (!p) return;
    if (tune_st != p->st) { p->st = tune_st; apply_tune(p); }
    int vel = (int)lrintf(vel01 * 127.0f);
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    sc808_trigger(p->e, LANES[p->lane].voice, vel);
    kp_gate_hit(&p->gate);
}

void choke(void *v) {
    Pad *p = static_cast<Pad *>(v);
    if (p) choke_voice(p->e, LANES[p->lane].voice);   /* the machine's own 2 ms fade */
}

/* One block of the pad's lane. `process(raw)` returns false once the lane is
 * not live — then, like the machine's lane guard, the sample is an exact 0
 * and the lane's drive stage is not run at all (a bitcrush holds its last
 * sample). A lane that is not live stays so until its next hit, so the pad
 * stops there rather than waiting out the silence gate. */
template <typename F>
void run(Pad *p, float *out, int n, F process) {
    const int lane = LANES[p->lane].voice;
    int dead = 0;
    for (int i = 0; i < n; i++) {
        float raw = 0.0f, s = 0.0f;
        if (process(raw)) {
            s = voice_sample(p->e, lane, raw) * p->vol;
            if (!(s > -8.0f && s < 8.0f)) s = 0.0f;   /* the machine's own NaN guard */
        } else {
            dead = 1;
        }
        out[i] = s;
        kp_gate_frame(&p->gate, s);
    }
    if (dead || p->e->rt[lane].choke_gain <= 0.0f) p->gate.active = 0;
}

int render(void *v, float *out, int n) {
    Pad *p = static_cast<Pad *>(v);
    if (!p || !p->gate.active) {
        memset(out, 0, sizeof(float) * (size_t)n);
        return 0;
    }
    sc808_engine *e = p->e;
    /* A lane that says it has finished is finished — checked PER SAMPLE, as
     * the machine's own lane guard does (choke gain up AND the circuit
     * active); a lane that stops mid-block renders exact zeros after, not the
     * circuit's residue. */
#define LIVE(obj) (e->rt[LANES[p->lane].voice].choke_gain > 0.0f && e->obj.active())
#define LANE(V, obj) case V: \
        run(p, out, n, [e, p](float &x) { return LIVE(obj) ? (x = (float)e->obj.process(), true) : false; }); break;
    /* The bank is ticked once per sample whatever the lanes do, BEFORE them;
     * per pad, the pad ticks it while the pad runs. */
#define METAL(V, obj) case V: \
        run(p, out, n, [e, p](float &x) { const double b = e->mbank.tick(); \
                                          return LIVE(obj) ? (x = (float)e->obj.process(b), true) : false; }); break;
    switch (LANES[p->lane].voice) {
        LANE(SC808_BD, bdc)  LANE(SC808_SD, sdc)
        LANE(SC808_LT, ltc)  LANE(SC808_MT, mtc)  LANE(SC808_HT, htc)
        LANE(SC808_LC, lcc)  LANE(SC808_MC, mcc)  LANE(SC808_HC, hcc)
        LANE(SC808_RS, rs)   LANE(SC808_CL, clc)  LANE(SC808_MA, mac)
        LANE(SC808_CP, cpc)
        case SC808_CB:
            run(p, out, n, [e, p](float &x) { e->mbank.tick(); if (!LIVE(cbc)) return false;
                                              x = (float)e->cbc.process(); return true; });
            break;
        METAL(SC808_CH, chc) METAL(SC808_OH, ohc) METAL(SC808_CY, cyc)
        default: memset(out, 0, sizeof(float) * (size_t)n); p->gate.active = 0; break;
    }
#undef LIVE
#undef LANE
#undef METAL
    return kp_gate_end(&p->gate);
}

#define OPS(L) { DR32_ENG_8W8_BASE + L, T.slugs[L], T.names[L], "e8_", nrows(LANES[L]), \
                 T.params[L], create<L>, destroy, set, note_on, choke, render, DR32_FAM_8W8 }
const dr32_engine_ops OPS_TABLE[SC808_NUM_VOICES] = {
    OPS(0), OPS(1), OPS(2),  OPS(3),  OPS(4),  OPS(5),  OPS(6),  OPS(7),
    OPS(8), OPS(9), OPS(10), OPS(11), OPS(12), OPS(13), OPS(14), OPS(15),
};
#undef OPS

struct Models {
    dr32_model m[SC808_NUM_VOICES];
    Models() {
        for (int l = 0; l < NL; l++)
            m[l] = dr32_model{LANES[l].slug, LANES[l].name, DR32_ENG_8W8_BASE + l,
                              T.defaults[l], 0.0f, 0.0f};
    }
};
const Models M;

}  // namespace

extern "C" {

const dr32_engine_ops *dr32_8w8_engine(int lane, int *count) {
    if (count) *count = NL;
    return (lane >= 0 && lane < NL) ? &OPS_TABLE[lane] : nullptr;
}

const dr32_model *dr32_8w8_models(int *count) {
    if (count) *count = NL;
    return M.m;
}

}
