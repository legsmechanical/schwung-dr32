/*
 * cw78_engine.cpp — the CW-78 (CR-78 style) lanes as DR32 engines.
 *
 * The machine is schwung-cw-78 (GPL-3.0), vendored unmodified under cw78/:
 * its circuit models and voice orchestration (cr78_engine.cpp). See
 * kit_port.h for what "one lane per pad" keeps and drops, and 6w6_engine.cpp
 * for why the engine is wrapped in a namespace. The machine's rhythm player
 * is not run: DR32 is played from Move's pads and sequencer.
 *
 * ⭑ THE NOISE SOURCE IS PER PAD. The hardware has ONE noise transistor bussed
 * to the snare's snap, the hats, cymbal, maracas and tambourine, so two of
 * those landing together hear the same noise. Here each pad owns a machine
 * and so its own source; two noise pads are no longer correlated the way the
 * machine's lanes are.
 */
#include <math.h>
#include <stdarg.h>
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
namespace dr32_cw78 {
#include "cw78/cr78_engine.cpp"
}
#pragma GCC diagnostic pop

#include "kit_port.h"
#include "../dr32_engine.h"

using namespace dr32_cw78;

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

const Row TUNE  = {"c7_tune",  "Tune",       "TUNE",  "%s_tune",      nullptr};
const Row DECAY = {"c7_decay", "Decay",      "DECAY", "%s_decay",     nullptr};
const Row DRIVE = {"c7_drive", "Drive",      "DRIVE", "%s_drive",     nullptr};
const Row DISTR = {"c7_dist",  "Distortion", "DIST",  "%s_dist_type", DIST};
/* Velocity is the machine's MASTER control, here per pad: on the CR-78 it
 * drives the trigger voltage into every lane's front-end diode as well as the
 * accent VCA, so it is part of the sound and stays the machine's. */
const Row VEL   = {"c7_vel",   "Velocity",   "VEL",   "vel_depth",    nullptr};

const Row SD_SNAPPY = {"c7_sd_snappy", "Snappy", "SNAPY", "sd_snappy", nullptr};
const Row GU_RATE   = {"c7_gu_rate",   "Rate",   "RATE",  "gu_rate",   nullptr};

struct Lane {
    int         voice;      /* CR78_* */
    const char *id, *slug, *name;
    int         tune_kind;
    const Row  *rows[8];
};

/* The machine's lane order, which is the picker's. Tune is semitones on the
 * pitched lanes and a ratio on the other six — the machine's own kIsPitched,
 * which create_lane checks against this table. */
#define STD(x) {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL}
const Lane LANES[] = {
    {CR78_BD, "bd", "cw78/kick",      "Bass Drum",  KP_TUNE_ST,    STD()},
    {CR78_SD, "sd", "cw78/snare",     "Snare",      KP_TUNE_ST,    {&TUNE, &DECAY, &SD_SNAPPY, &DRIVE, &DISTR, &VEL}},
    {CR78_RS, "rs", "cw78/rimshot",   "Rim Shot",   KP_TUNE_ST,    STD()},
    {CR78_HH, "hh", "cw78/hihat",     "Hi-Hat",     KP_TUNE_RATIO, STD()},
    {CR78_CY, "cy", "cw78/cymbal",    "Cymbal",     KP_TUNE_RATIO, STD()},
    {CR78_MA, "ma", "cw78/maracas",   "Maracas",    KP_TUNE_RATIO, STD()},
    {CR78_CL, "cl", "cw78/claves",    "Claves",     KP_TUNE_ST,    STD()},
    {CR78_HB, "hb", "cw78/hi_bongo",  "Hi Bongo",   KP_TUNE_ST,    STD()},
    {CR78_LB, "lb", "cw78/low_bongo", "Low Bongo",  KP_TUNE_ST,    STD()},
    {CR78_LC, "lc", "cw78/low_conga", "Low Conga",  KP_TUNE_ST,    STD()},
    {CR78_CB, "cb", "cw78/cowbell",   "Cowbell",    KP_TUNE_ST,    STD()},
    {CR78_TB, "tb", "cw78/tambourine","Tambourine", KP_TUNE_RATIO, STD()},
    {CR78_GU, "gu", "cw78/guiro",     "Guiro",      KP_TUNE_RATIO, {&TUNE, &DECAY, &GU_RATE, &DRIVE, &DISTR, &VEL}},
    {CR78_MB, "mb", "cw78/metal_beat","Metal Beat", KP_TUNE_RATIO, STD()},
};
#undef STD
const int NL = (int)(sizeof(LANES) / sizeof(LANES[0]));
static_assert(sizeof(LANES) / sizeof(LANES[0]) == CR78_NUM_VOICES, "one engine per lane");

int nrows(const Lane &l) { int n = 0; while (n < 8 && l.rows[n]) n++; return n; }

/* A row's machine key for this lane. */
void ekey(const Lane &l, const Row &r, char *out, size_t len) { snprintf(out, len, r.ekey, l.id); }

/* The machine's default for a row, in DR32 display units (pot or enum index). */
float row_default(const Lane &l, const Row &r) {
    char k[48];
    ekey(l, r, k, sizeof k);
    const int ps = find_pot(k);
    if (ps >= 0) return (float)g_cr78_pots[ps].def;
    const int es = find_enum(k);
    return es >= 0 ? (float)g_cr78_enums[es].def : 0.0f;
}

int enum_count(const Lane &l, const Row &r) {
    char k[48];
    ekey(l, r, k, sizeof k);
    const int es = find_enum(k);
    return es >= 0 ? g_cr78_enums[es].count : 0;
}

struct Tables {
    dr32_eparam params[CR78_NUM_VOICES][DR32_ENG_MAX_PARAMS];
    float       defaults[CR78_NUM_VOICES][DR32_ENG_MAX_PARAMS];
    char        names[CR78_NUM_VOICES][32];
    char        slugs[CR78_NUM_VOICES][24];
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
            snprintf(names[l], sizeof names[l], "CW-78 %s", ln.name);
            snprintf(slugs[l], sizeof slugs[l], "cw78_%s", ln.id);
        }
    }
};
const Tables T;

struct Pad {
    cr78_engine_t *e;
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
    p->e = cr78_create((float)sr);
    if (!p->e) { free(p); return nullptr; }
    p->lane = lane;
    /* The lane table's Tune kind must be the machine's own reading of it. */
    if ((LANES[lane].tune_kind == KP_TUNE_ST) != kIsPitched[LANES[lane].voice]) {
        cr78_destroy(p->e);
        free(p);
        return nullptr;
    }
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
    cr78_destroy(p->e);
    free(p);
}

void set(void *v, int idx, float display) {
    Pad *p = static_cast<Pad *>(v);
    const Lane &ln = LANES[p ? p->lane : 0];
    if (!p || idx < 0 || idx >= nrows(ln)) return;
    char k[48], val[16];
    ekey(ln, *ln.rows[idx], k, sizeof k);
    snprintf(val, sizeof val, "%d", (int)lrintf(display));
    cr78_set_param(p->e, k, val);
    if (ln.rows[idx] == &TUNE) apply_tune(p);
}

void note_on(void *v, float vel01, float tune_st) {
    Pad *p = static_cast<Pad *>(v);
    if (!p) return;
    if (tune_st != p->st) { p->st = tune_st; apply_tune(p); }
    int vel = (int)lrintf(vel01 * 127.0f);
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    cr78_trigger(p->e, LANES[p->lane].voice, vel);
    kp_gate_hit(&p->gate);
}

void choke(void *v) {
    Pad *p = static_cast<Pad *>(v);
    if (p) choke_lane(p->e, LANES[p->lane].voice);    /* the machine's own fade */
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
    cr78_engine *e = p->e;
    /* A lane that says it has finished is finished — checked PER SAMPLE, as
     * the machine's own lane guard does (choke gain up AND the circuit
     * active); a lane that stops mid-block renders exact zeros after, not the
     * circuit's residue. */
#define LIVE(obj) (e->rt[LANES[p->lane].voice].choke_gain > 0.0f && e->obj.active())
#define LANE(V, obj) case V: \
        run(p, out, n, [e, p](float &x) { return LIVE(obj) ? (x = (float)e->obj.process(), true) : false; }); break;
    /* The noise source is ticked once per sample whatever the lanes do, before
     * any of them; per pad, a noise pad ticks its own while it runs. */
#define NLANE(V, obj) case V: \
        run(p, out, n, [e, p](float &x) { const double nz = e->noise.process(); \
                                          return LIVE(obj) ? (x = (float)e->obj.process(nz), true) : false; }); break;
    switch (LANES[p->lane].voice) {
        LANE (CR78_BD, bdv)  NLANE(CR78_SD, sdv)  LANE (CR78_RS, rsv)
        NLANE(CR78_HH, hhv)  NLANE(CR78_CY, cyv)  NLANE(CR78_MA, mav)
        LANE (CR78_CL, clv)  LANE (CR78_HB, hbv)  LANE (CR78_LB, lbv)
        LANE (CR78_LC, lcv)  LANE (CR78_CB, cbv)  NLANE(CR78_TB, tbv)
        LANE (CR78_GU, guv)  LANE (CR78_MB, mbv)
        default: memset(out, 0, sizeof(float) * (size_t)n); p->gate.active = 0; break;
    }
#undef LIVE
#undef LANE
#undef NLANE
    return kp_gate_end(&p->gate);
}

#define OPS(L) { DR32_ENG_CW78_BASE + L, T.slugs[L], T.names[L], "c7_", nrows(LANES[L]), \
                 T.params[L], create<L>, destroy, set, note_on, choke, render, DR32_FAM_CW78 }
const dr32_engine_ops OPS_TABLE[CR78_NUM_VOICES] = {
    OPS(0), OPS(1), OPS(2),  OPS(3),  OPS(4),  OPS(5),  OPS(6),  OPS(7),
    OPS(8), OPS(9), OPS(10), OPS(11), OPS(12), OPS(13),
};
#undef OPS

struct Models {
    dr32_model m[CR78_NUM_VOICES];
    Models() {
        for (int l = 0; l < NL; l++)
            m[l] = dr32_model{LANES[l].slug, LANES[l].name, DR32_ENG_CW78_BASE + l,
                              T.defaults[l], 0.0f, 0.0f};
    }
};
const Models M;

}  // namespace

extern "C" {

const dr32_engine_ops *dr32_cw78_engine(int lane, int *count) {
    if (count) *count = NL;
    return (lane >= 0 && lane < NL) ? &OPS_TABLE[lane] : nullptr;
}

const dr32_model *dr32_cw78_models(int *count) {
    if (count) *count = NL;
    return M.m;
}

}
