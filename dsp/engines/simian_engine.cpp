/*
 * simian_engine.cpp — the SIMIAN voice as a DR32 engine.
 *
 * The DSP is Punk Labs' OneTrick SIMIAN 2.0.2 (GPL-3.0-or-later), vendored
 * from schwung-simian: faust/ is its source, generated/ is Faust's output
 * (scripts/gen_engines.sh), factory_bank.h its 35 kits in display units.
 *
 * One SIMIAN voice is a tuned oscillator that morphs from a triangle to a
 * struck cymbal, a resonant-lowpass noise source, and a click, crossfaded and
 * swept by one envelope. Nothing but its numbers makes it a kick or a hat, so
 * a MODEL here is one row of the port's "Basic" kit.
 *
 * WHAT DR32 TAKES OVER. The voice's own Gain, Pan and reverb send are pinned
 * (0 dB, centre, 0): the pad's DR32 Volume and Pan do those jobs, and SIMIAN's
 * reverb lived in the kit master stage, which is not here. A model's Gain and
 * Pan become the pad's starting Volume and Pan instead. The mod and pitch
 * wheels (DJ filter, staccato) are not wired: DR32 has no per-kit wheel
 * handling for them to ride on.
 *
 * ⚠ GainDynamics STAYS the engine's ("Vel Gain"). It is not a volume stage
 * bolted on after the voice: it scales the level going INTO SIMIAN's
 * saturation, so velocity changes the voice's colour as well as its size.
 * Replacing it with DR32's post-voice Vel Vol would change the instrument.
 */
#include <new>

#include "faust_voice.h"
#include "simian/generated/simian_voice.hpp"
#include "simian/factory_bank.h"

#include "../dr32_engine.h"

namespace {

/* ⚠ ORDER IS THE PAGE ORDER (tools/gen_engine_ui.mjs lays knobs out in this
 * order, eight to a bank) AND THE STATE ORDER. Append; never insert. */
struct Row { dr32_eparam p; const char *zone; int bank; /* SIMIAN_FACTORY column */ };

const Row ROWS[] = {
    /* --- Tone: the oscillator and the envelope that sweeps it. --- */
    {{"sm_pitch",    "Pitch",    "PITCH",  30, 4400, 220, 1, "hz", "Tone"}, "OscPitch",               0},
    {{"sm_wave",     "Wave",     "WAVE",    0,  100,   0, 1, "%", "Tone"},  "OscWaveform",            1},
    {{"sm_bend",     "Bend",     "BEND",  -36,   36,   0, 1, "st", "Tone"}, "OscEnvBend",             2},
    {{"sm_bend_dyn", "Bend Dyn", "BDYN",    0,  100, 100, 1, "%", "Tone"},  "OscEnvBendDynamics",     3},
    {{"sm_decay",    "Decay",    "DECAY",  10, 2000, 200, 1, "ms", "Tone"}, "EnvDecay",               8},
    {{"sm_punch",    "Punch",    "PUNCH",   0,  100,   0, 1, "%", "Tone"},  "EnvPunch",               9},
    {{"sm_noise",    "Tone/Nse", "T/NSE",   0,  100,  25, 1, "%", "Tone"},  "FadeToneNoise",         10},
    {{"sm_click",    "Click",    "CLICK",   0,  100,  50, 1, "%", "Tone"},  "FadeClick",             11},
    /* --- Noise: its resonant lowpass, and the voice's colour. --- */
    {{"sm_cutoff",   "Cutoff",   "CUTOF", 440, 15000, 10000, 1, "hz", "Noise"}, "LowpassFreq",         4},
    {{"sm_res",      "Res",      "RES",     0,  100,   0, 1, "%", "Noise"},  "LowpassQ",               5},
    {{"sm_lp_bend",  "LP Bend",  "LPBND", -36,   36,   0, 1, "st", "Noise"}, "LowpassEnvBend",         6},
    {{"sm_lp_dyn",   "LP Dyn",   "LPDYN",   0,  100, 100, 1, "%", "Noise"},  "LowpassEnvBendDynamics", 7},
    {{"sm_sat",      "Sat",      "SAT",     0,  100,   0, 1, "%", "Noise"},  "Saturation",            15},
    {{"sm_vel_gain", "Vel Gain", "VGAIN",   0,  100, 100, 1, "%", "Noise"},  "GainDynamics",          14},
};
const int N = (int)(sizeof(ROWS) / sizeof(ROWS[0]));
static_assert(sizeof(ROWS) / sizeof(ROWS[0]) <= DR32_ENG_MAX_PARAMS, "too many params");

/* SIMIAN_FACTORY columns DR32 takes over rather than exposing. */
enum { COL_GAIN = 12, COL_PAN = 13 };

struct Tables {
    dr32_eparam params[DR32_ENG_MAX_PARAMS];
    Tables() { for (int i = 0; i < N; i++) params[i] = ROWS[i].p; }
};
const Tables T;

struct Voice {
    Dr32FaustVoice fv;
    FAUSTFLOAT    *zone[DR32_ENG_MAX_PARAMS];
    FAUSTFLOAT    *z_key, *z_choke;
};

void *create(int sr) {
    Voice *v = new (std::nothrow) Voice();
    if (!v) return nullptr;
    SimianVoice *d = new (std::nothrow) SimianVoice();
    if (!d) { delete v; return nullptr; }
    dr32_fv_setup(&v->fv, d, sr);
    for (int i = 0; i < N; i++) {
        v->zone[i] = v->fv.zones.find(ROWS[i].zone);
        dr32_fv_set_zone(v->zone[i], ROWS[i].p.def);
    }
    v->z_key = v->fv.zones.find("key");
    v->z_choke = v->fv.zones.find("Choke");
    /* Pinned: DR32 owns level, pan and sends (see the header). */
    dr32_fv_set_zone(v->fv.zones.find("Gain"), 0.0f);
    dr32_fv_set_zone(v->fv.zones.find("Pan"), 0.0f);
    dr32_fv_set_zone(v->fv.zones.find("SendReverb"), 0.0f);
    return v;
}

void destroy(void *e) {
    Voice *v = static_cast<Voice *>(e);
    if (!v) return;
    delete v->fv.d;
    delete v;
}

void set(void *e, int idx, float display) {
    Voice *v = static_cast<Voice *>(e);
    if (!v || idx < 0 || idx >= N) return;
    dr32_fv_set_zone(v->zone[idx], dr32_fv_clamp(display, ROWS[idx].p.min, ROWS[idx].p.max));
}

void note_on(void *e, float vel01, float tune_st) {
    Voice *v = static_cast<Voice *>(e);
    if (!v) return;
    dr32_fv_set_zone(v->z_choke, 0.0f);      /* a hit cancels a choke */
    /* `key` is SIMIAN's own relative pitch offset and it is NOT smoothed, so
     * a pad's tune lands on the first sample. ±108 st is the zone's range. */
    dr32_fv_set_zone(v->z_key, dr32_fv_clamp(tune_st, -108.0f, 108.0f));
    dr32_fv_note_on(&v->fv, vel01);
}

void choke(void *e) {
    Voice *v = static_cast<Voice *>(e);
    if (!v) return;
    /* SIMIAN's own 20 ms choke, so a choked voice goes quiet and stops being
     * computed; DR32's 3 ms ramp is what the listener hears. */
    dr32_fv_set_zone(v->z_choke, 1.0f);
    if (v->fv.z_trigger) *v->fv.z_trigger = 0.0f;
    v->fv.pending = -1.0f;
}

int render(void *e, float *out, int n) {
    Voice *v = static_cast<Voice *>(e);
    return v ? dr32_fv_render(&v->fv, out, n) : 0;
}

/* ---- models: the port's "Basic" kit, one voice per model ---------------- */

struct ModelSrc { const char *slug, *name; int voice; };
/* Picker order. `voice` is the SIMIAN_FACTORY voice row (Kick, Rimshot,
 * Snare, Clap, Low Tom, HH Closed, Mid Tom, HH Open, High Tom, Cymbal). */
const ModelSrc SRC[] = {
    {"simian/kick",      "Kick",       0},
    {"simian/snare",     "Snare",      2},
    {"simian/rimshot",   "Rimshot",    1},
    {"simian/clap",      "Clap",       3},
    {"simian/low_tom",   "Low Tom",    4},
    {"simian/mid_tom",   "Mid Tom",    6},
    {"simian/high_tom",  "High Tom",   8},
    {"simian/hat_closed","Closed Hat", 5},
    {"simian/hat_open",  "Open Hat",   7},
    {"simian/cymbal",    "Cymbal",     9},
};
const int NM = (int)(sizeof(SRC) / sizeof(SRC[0]));

struct Models {
    float      values[NM][DR32_ENG_MAX_PARAMS];
    dr32_model m[NM];
    Models() {
        const simian_preset_t &basic = SIMIAN_FACTORY[0];
        for (int i = 0; i < NM; i++) {
            const float *row = basic.voice[SRC[i].voice];
            for (int k = 0; k < N; k++) values[i][k] = row[ROWS[k].bank];
            m[i].slug = SRC[i].slug;
            m[i].name = SRC[i].name;
            m[i].engine = DR32_ENG_SIMIAN;
            m[i].values = values[i];
            m[i].volume_db = row[COL_GAIN];
            m[i].pan = row[COL_PAN] * 0.5f;     /* SIMIAN ±100 -> DR32 ±50 */
        }
    }
};
const Models M;

}  // namespace

extern "C" {

const dr32_engine_ops dr32_engine_simian = {
    DR32_ENG_SIMIAN, "simian", "Simian", "sm_", N, T.params,
    create, destroy, set, note_on, choke, render, DR32_FAM_SIMIAN,
};

const dr32_model *dr32_simian_models(int *count) {
    if (count) *count = NM;
    return M.m;
}

void dr32_simian_class_init(int sample_rate) {
    static int done = 0;
    if (done) return;
    SimianVoice::classInit(sample_rate);     /* fills the shared cymbal table */
    done = 1;
}

}
