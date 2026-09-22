/*
 * 6w6_engine.cpp — the 6W6 (TR-606 style) lanes as DR32 engines.
 *
 * The machine is schwung-6W6 (GPL-3.0), vendored unmodified under 6w6/: its
 * voice orchestration (sd606_engine.cpp) over the 606-Inspired-Synth-Drums
 * voices (MIT, AudioKit Pro). See kit_port.h for what "one lane per pad" keeps
 * and drops.
 *
 * ⭑ THE WHOLE ENGINE IS WRAPPED IN A NAMESPACE, as insurance rather than a
 * fix. Two ports' classes in one link would merge silently if they shared a
 * name — member functions defined in-class are inline, emitted as weak
 * symbols, and the linker keeps ONE (the DSP_Drum collision gen_engines.sh
 * renames around, in C++ form). Today nothing collides: each port keeps its
 * classes in its own namespace (SynthDrums606, sc808, cr78) and, built
 * without these wrappers, the three objects share no symbol (checked with nm,
 * 2026-09-22; tests/test_kit_ports.c passes either way). The wrapper keeps it
 * that way when a port is re-vendored with a global class in it — CW-78 and
 * 6W6 both have a `SnareVoice`, 8W8 and CW-78 a `OnePoleHP`, only namespaced.
 * The system headers are included first, outside it, so the engine's own
 * includes of them are no-ops and the standard library is not re-declared
 * inside.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

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
namespace dr32_6w6 {
#include "6w6/sd606_engine.cpp"
}
#pragma GCC diagnostic pop

#include "kit_port.h"
#include "../dr32_engine.h"

using namespace dr32_6w6;

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

const Row TUNE  = {"s6_tune",  "Tune",       "TUNE",  "%s_tune",      nullptr};
const Row DECAY = {"s6_decay", "Decay",      "DECAY", "%s_decay",     nullptr};
const Row DRIVE = {"s6_drive", "Drive",      "DRIVE", "%s_drive",     nullptr};
const Row DISTR = {"s6_dist",  "Distortion", "DIST",  "%s_dist_type", DIST};
/* Velocity is the machine's MASTER control (how far below full a soft hit
 * falls — it can also shape the hit, not only its level), here per pad. */
const Row VEL   = {"s6_vel",   "Velocity",   "VEL",   "vel_depth",    nullptr};

const Row BD_ATTACK = {"s6_bd_attack", "Attack", "ATTCK", "bd_attack", nullptr};
const Row SD_SNAPPY = {"s6_sd_snappy", "Snappy", "SNAPY", "sd_snappy", nullptr};
const Row CP_NOISE  = {"s6_cp_noise",  "Noise",  "NOISE", "cp_noise",  nullptr};

struct Lane {
    int         voice;      /* SD606_* */
    const char *id, *slug, *name;
    int         tune_kind;
    const Row  *rows[8];
};

/* The machine's lane order, which is the picker's. Tune: the kick's is
 * semitones (BassDrum.hpp tuneSemitones), every other lane's a ratio. */
const Lane LANES[] = {
    {SD606_BD, "bd", "6w6/kick",       "Bass Drum",  KP_TUNE_ST,    {&TUNE, &DECAY, &BD_ATTACK, &DRIVE, &DISTR, &VEL}},
    {SD606_SD, "sd", "6w6/snare",      "Snare",      KP_TUNE_RATIO, {&TUNE, &DECAY, &SD_SNAPPY, &DRIVE, &DISTR, &VEL}},
    {SD606_LT, "lt", "6w6/low_tom",    "Low Tom",    KP_TUNE_RATIO, {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL}},
    {SD606_HT, "ht", "6w6/hi_tom",     "Hi Tom",     KP_TUNE_RATIO, {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL}},
    {SD606_CH, "ch", "6w6/hat_closed", "Closed Hat", KP_TUNE_RATIO, {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL}},
    {SD606_OH, "oh", "6w6/hat_open",   "Open Hat",   KP_TUNE_RATIO, {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL}},
    {SD606_CY, "cy", "6w6/cymbal",     "Cymbal",     KP_TUNE_RATIO, {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL}},
    {SD606_CP, "cp", "6w6/clap",       "Clap",       KP_TUNE_RATIO, {&TUNE, &DECAY, &CP_NOISE, &DRIVE, &DISTR, &VEL}},
};
const int NL = (int)(sizeof(LANES) / sizeof(LANES[0]));
static_assert(sizeof(LANES) / sizeof(LANES[0]) == SD606_NUM_VOICES, "one engine per lane");

int nrows(const Lane &l) { int n = 0; while (n < 8 && l.rows[n]) n++; return n; }

/* A row's machine key for this lane. */
void ekey(const Lane &l, const Row &r, char *out, size_t len) { snprintf(out, len, r.ekey, l.id); }

/* The machine's default for a row, in DR32 display units (pot or enum index). */
float row_default(const Lane &l, const Row &r) {
    char k[48];
    ekey(l, r, k, sizeof k);
    const int ps = find_pot(k);
    if (ps >= 0) return (float)g_sd606_pots[ps].def;
    const int es = find_enum(k);
    return es >= 0 ? (float)g_sd606_enums[es].def : 0.0f;
}

int enum_count(const Lane &l, const Row &r) {
    char k[48];
    ekey(l, r, k, sizeof k);
    const int es = find_enum(k);
    return es >= 0 ? g_sd606_enums[es].count : 0;
}

struct Tables {
    dr32_eparam params[SD606_NUM_VOICES][DR32_ENG_MAX_PARAMS];
    float       defaults[SD606_NUM_VOICES][DR32_ENG_MAX_PARAMS];
    char        names[SD606_NUM_VOICES][32];
    char        slugs[SD606_NUM_VOICES][24];
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
            snprintf(names[l], sizeof names[l], "6W6 %s", ln.name);
            snprintf(slugs[l], sizeof slugs[l], "6w6_%s", ln.id);
        }
    }
};
const Tables T;

struct Pad {
    sd606_engine_t *e;
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
    p->e = sd606_create((float)sr);
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
    sd606_destroy(p->e);
    free(p);
}

void set(void *v, int idx, float display) {
    Pad *p = static_cast<Pad *>(v);
    const Lane &ln = LANES[p ? p->lane : 0];
    if (!p || idx < 0 || idx >= nrows(ln)) return;
    char k[48], val[16];
    ekey(ln, *ln.rows[idx], k, sizeof k);
    snprintf(val, sizeof val, "%d", (int)lrintf(display));
    sd606_set_param(p->e, k, val);
    if (ln.rows[idx] == &TUNE) apply_tune(p);
}

void note_on(void *v, float vel01, float tune_st) {
    Pad *p = static_cast<Pad *>(v);
    if (!p) return;
    if (tune_st != p->st) { p->st = tune_st; apply_tune(p); }
    int vel = (int)lrintf(vel01 * 127.0f);
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    sd606_trigger(p->e, LANES[p->lane].voice, vel);
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
    sd606_engine *e = p->e;
    switch (LANES[p->lane].voice) {
        case SD606_BD: run(p, out, n, [e, p](float &x) { return e->rt[LANES[p->lane].voice].choke_gain > 0.0f ? (x = e->bd.process(), true) : false; }); break;
        case SD606_SD: run(p, out, n, [e, p](float &x) { return e->rt[LANES[p->lane].voice].choke_gain > 0.0f ? (x = e->sd.process(), true) : false; }); break;
        case SD606_LT: run(p, out, n, [e, p](float &x) { return e->rt[LANES[p->lane].voice].choke_gain > 0.0f ? (x = e->lt.process(), true) : false; }); break;
        case SD606_HT: run(p, out, n, [e, p](float &x) { return e->rt[LANES[p->lane].voice].choke_gain > 0.0f ? (x = e->ht.process(), true) : false; }); break;
        case SD606_CH: run(p, out, n, [e, p](float &x) { return e->rt[LANES[p->lane].voice].choke_gain > 0.0f ? (x = e->ch.process(), true) : false; }); break;
        case SD606_OH: run(p, out, n, [e, p](float &x) { return e->rt[LANES[p->lane].voice].choke_gain > 0.0f ? (x = e->oh.process(), true) : false; }); break;
        case SD606_CY: run(p, out, n, [e, p](float &x) { return e->rt[LANES[p->lane].voice].choke_gain > 0.0f ? (x = e->cy.process(), true) : false; }); break;
        case SD606_CP: run(p, out, n, [e, p](float &x) { return e->rt[LANES[p->lane].voice].choke_gain > 0.0f ? (x = e->cp.process(), true) : false; }); break;
        default: memset(out, 0, sizeof(float) * (size_t)n); p->gate.active = 0; break;
    }
    return kp_gate_end(&p->gate);
}

#define OPS(L) { DR32_ENG_6W6_BASE + L, T.slugs[L], T.names[L], "s6_", nrows(LANES[L]), \
                 T.params[L], create<L>, destroy, set, note_on, choke, render, DR32_FAM_6W6 }
const dr32_engine_ops OPS_TABLE[SD606_NUM_VOICES] = {
    OPS(0), OPS(1), OPS(2), OPS(3), OPS(4), OPS(5), OPS(6), OPS(7),
};
#undef OPS

struct Models {
    dr32_model m[SD606_NUM_VOICES];
    Models() {
        for (int l = 0; l < NL; l++)
            m[l] = dr32_model{LANES[l].slug, LANES[l].name, DR32_ENG_6W6_BASE + l,
                              T.defaults[l], 0.0f, 0.0f};
    }
};
const Models M;

}  // namespace

extern "C" {

const dr32_engine_ops *dr32_6w6_engine(int lane, int *count) {
    if (count) *count = NL;
    return (lane >= 0 && lane < NL) ? &OPS_TABLE[lane] : nullptr;
}

const dr32_model *dr32_6w6_models(int *count) {
    if (count) *count = NL;
    return M.m;
}

}
