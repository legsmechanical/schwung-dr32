/*
 * fm_engine.cpp — DR32's own FM drum voice.
 *
 * Written for DR32 (Josh, 2026-09-22: "from scratch"), after the IDEA of the
 * Elektron Machinedrum's EFM machines: a two-operator FM voice with a pitch
 * sweep, plus a filtered noise layer. No code is taken from anywhere; in
 * particular not from ctag-fh-kiel/md-drum-synth, which prompted this and
 * carries no licence.
 *
 *   carrier   sine at Pitch, swept down from Sweep semitones above it over
 *             Sweep Decay (a Hz offset that decays exponentially, as an
 *             analogue kick's does), under the amp envelope (Decay)
 *   modulator sine at Ratio x the carrier (Track On: it follows the sweep;
 *             Off: it stays at Ratio x Pitch), phase-modulating the carrier
 *             by Mod under its own envelope (Mod Decay), with self-Feedback
 *   noise     white, through a resonant state-variable filter (LP/BP/HP),
 *             under its own envelope; Claps > 1 fires that many bursts Clap
 *             Gap apart before the last one decays over Noise Decay
 *   output    tone + noise -> Drive (soft clip) -> Low Cut (12 dB HP)
 *
 * Every "decay" is the time to fall 60 dB, so a knob's number is the length
 * you hear.
 *
 * ⭐ VELOCITY (its own page; Josh: "so velocity changes tone in a meaningful
 * way"). One law for every target: a FULL-velocity hit is exactly the knobs
 * as set, and a softer one moves away from them by the amount's share of
 * (1 - velocity). So an amount of 0 is "velocity does not touch this", and
 * turning an amount up never changes a hard hit. Targets: Level, FM depth,
 * Sweep depth, Pitch (soft hits flatter), Decay (bipolar: + soft hits
 * shorter, - soft hits longer), Noise level and Noise Freq (soft hits
 * darker). DR32's own Vel Vol starts at 0 for synth pads, as for every
 * engine.
 *
 * ⭐ COST. Per sounding sample: two table sines, one noise draw, one or two
 * SVFs. Once the tone and noise envelopes are both below -120 dB and no
 * clap burst is pending, the voice writes zeros without computing (the
 * gate then stops it 100 ms later).
 */
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "kit_port.h"
#include "../dr32_engine.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kLn1000 = 6.907755279f;      /* -60 dB */
constexpr float kLn100  = 4.605170186f;      /* -40 dB */

/* ⚠ ORDER IS THE PAGE ORDER AND THE STATE ORDER. Append; never insert. */
enum {
    P_PITCH, P_DECAY, P_SWEEP, P_SWDEC, P_TONE, P_DRIVE, P_LOWCUT,
    P_RATIO, P_MOD, P_MDEC, P_FB, P_TRACK,
    P_NOISE, P_NDEC, P_NFREQ, P_NRES, P_NMODE, P_CLAPS, P_GAP,
    P_VEL, P_VMOD, P_VSWEEP, P_VPITCH, P_VDECAY, P_VNOISE, P_VNFREQ,
    NP
};

const dr32_eparam PARAMS[NP] = {
    {"fm_pitch",   "Pitch",       "PITCH", 20.0f,  2000.0f,  55.0f,  1.0f,  "hz", "Tone",  nullptr},
    {"fm_decay",   "Decay",       "DECAY", 5.0f,   4000.0f,  400.0f, 1.0f,  "ms", "Tone",  nullptr},
    {"fm_sweep",   "Sweep",       "SWEEP", 0.0f,   48.0f,    24.0f,  1.0f,  "st", "Tone",  nullptr},
    {"fm_swdec",   "Sweep Decay", "SWDEC", 1.0f,   1000.0f,  50.0f,  1.0f,  "ms", "Tone",  nullptr},
    {"fm_tone",    "Tone Level",  "TONE",  0.0f,   100.0f,   100.0f, 1.0f,  "%",  "Tone",  nullptr},
    {"fm_drive",   "Drive",       "DRIVE", 0.0f,   100.0f,   0.0f,   1.0f,  "%",  "Tone",  nullptr},
    {"fm_lowcut",  "Low Cut",     "LOCUT", 20.0f,  8000.0f,  20.0f,  1.0f,  "hz", "Tone",  nullptr},
    {"fm_ratio",   "Ratio",       "RATIO", 0.25f,  16.0f,    2.0f,   0.01f, nullptr, "FM", nullptr},
    {"fm_mod",     "Mod",         "MOD",   0.0f,   100.0f,   20.0f,  1.0f,  "%",  "FM",    nullptr},
    {"fm_mdec",    "Mod Decay",   "MDEC",  1.0f,   2000.0f,  60.0f,  1.0f,  "ms", "FM",    nullptr},
    {"fm_fb",      "Feedback",    "FB",    0.0f,   100.0f,   0.0f,   1.0f,  "%",  "FM",    nullptr},
    {"fm_track",   "Mod Track",   "TRACK", 0.0f,   1.0f,     1.0f,   1.0f,  nullptr, "FM", "Off|On"},
    {"fm_noise",   "Noise",       "NOISE", 0.0f,   100.0f,   0.0f,   1.0f,  "%",  "Noise", nullptr},
    {"fm_ndec",    "Noise Decay", "NDEC",  1.0f,   2000.0f,  100.0f, 1.0f,  "ms", "Noise", nullptr},
    {"fm_nfreq",   "Noise Freq",  "NFREQ", 100.0f, 16000.0f, 5000.0f, 10.0f, "hz", "Noise", nullptr},
    {"fm_nres",    "Noise Res",   "NRES",  0.0f,   100.0f,   0.0f,   1.0f,  "%",  "Noise", nullptr},
    {"fm_nmode",   "Noise Filter","NFILT", 0.0f,   2.0f,     2.0f,   1.0f,  nullptr, "Noise", "LP|BP|HP"},
    {"fm_claps",   "Claps",       "CLAPS", 1.0f,   6.0f,     1.0f,   1.0f,  nullptr, "Noise", nullptr},
    {"fm_gap",     "Clap Gap",    "GAP",   2.0f,   40.0f,    10.0f,  1.0f,  "ms", "Noise", nullptr},
    {"fm_vel",     "Vel>Level",   "VLVL",  0.0f,   100.0f,   60.0f,  1.0f,  "%",  "Velocity", nullptr},
    {"fm_vmod",    "Vel>Mod",     "VMOD",  0.0f,   100.0f,   50.0f,  1.0f,  "%",  "Velocity", nullptr},
    {"fm_vsweep",  "Vel>Sweep",   "VSWP",  0.0f,   100.0f,   0.0f,   1.0f,  "%",  "Velocity", nullptr},
    {"fm_vpitch",  "Vel>Pitch",   "VPTCH", 0.0f,   12.0f,    0.0f,   0.1f,  "st", "Velocity", nullptr},
    {"fm_vdecay",  "Vel>Decay",   "VDEC",  -100.0f, 100.0f,  0.0f,   1.0f,  "%",  "Velocity", nullptr},
    {"fm_vnoise",  "Vel>Noise",   "VNSE",  0.0f,   100.0f,   0.0f,   1.0f,  "%",  "Velocity", nullptr},
    {"fm_vnfreq",  "Vel>NFreq",   "VNFRQ", 0.0f,   100.0f,   0.0f,   1.0f,  "%",  "Velocity", nullptr},
};

/* ---- the sine table: 1024 points + a guard, linear interpolation --------- */
constexpr int kTabBits = 10;
constexpr int kTab = 1 << kTabBits;
float g_sine[kTab + 1];
struct SineInit {
    SineInit() { for (int i = 0; i <= kTab; i++) g_sine[i] = std::sin(2.0f * kPi * (float) i / (float) kTab); }
};
const SineInit g_sine_init;

/* A phase is a uint32 cycle fraction; it wraps by overflow. */
inline float sine(uint32_t ph) {
    const uint32_t i = ph >> (32 - kTabBits);
    const float f = (float) (ph & ((1u << (32 - kTabBits)) - 1)) * (1.0f / (float) (1u << (32 - kTabBits)));
    return g_sine[i] + (g_sine[i + 1] - g_sine[i]) * f;
}

/* A phase offset in CYCLES (may be negative, a few cycles at most). */
inline uint32_t cycles(float c) { return (uint32_t) (int64_t) (c * 4294967296.0f); }

/* Soft clip: odd, 1 at |x| >= 3, the rational tanh shape below it. */
inline float sat(float x) {
    if (x >= 3.0f) return 1.0f;
    if (x <= -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Andy Simper's trapezoidal SVF: coefficients, then per-sample state. */
struct Svf {
    float a1 = 0, a2 = 0, a3 = 0, k = 2, ic1 = 0, ic2 = 0;
    void set(float fc, float q, float sr) {
        if (fc > 0.45f * sr) fc = 0.45f * sr;
        const float g = std::tan(kPi * fc / sr);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    /* mode 0 LP, 1 BP (unity peak), 2 HP */
    float run(float v0, int mode) {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (mode == 0) return v2;
        if (mode == 1) return k * v1;
        return v0 - k * v1 - v2;
    }
};

inline float decay_coef(float ms, float sr, float ln_drop) {
    return std::exp(-ln_drop / (ms * 0.001f * sr));
}

struct Voice {
    float sr;
    float p[NP];
    int dirty = 1;
    kp_gate gate;

    /* derived, recomputed when a knob moves */
    float c_amp = 0, c_sw = 0, c_mod = 0, c_noise = 0, c_burst = 0;
    int   gap_n = 0;
    float drive_g = 1, drive_norm = 1;
    float mod_amt = 0, fb_amt = 0, tone_lvl = 1, noise_lvl = 0;
    int   nmode = 2, lowcut_on = 0;
    Svf   nf, lc;

    /* per hit */
    float base_hz = 0, sweep_hz = 0, vel_gain = 1, vel_mod = 1, vel_noise = 1;
    float decay_mul = 1, nfreq_mul = 1;      /* velocity's, set per hit, used by derive() */
    float env_amp = 0, env_sw = 0, env_mod = 0, env_noise = 0;
    int   bursts_left = 0, burst_count = 0;
    uint32_t ph_c = 0, ph_m = 0;
    float fb1 = 0, fb2 = 0;
    uint32_t rng = 0x9e3779b9u;
    int   pending = 0;
    float pend_vel = 1, pend_tune = 0;

    explicit Voice(float fs) : sr(fs) {
        for (int i = 0; i < NP; i++) p[i] = PARAMS[i].def;
        kp_gate_init(&gate, (int) fs);
    }

    void derive() {
        dirty = 0;
        c_amp = decay_coef(p[P_DECAY] * decay_mul, sr, kLn1000);
        c_sw  = decay_coef(p[P_SWDEC], sr, kLn1000);
        c_mod = decay_coef(p[P_MDEC], sr, kLn1000);
        c_noise = decay_coef(p[P_NDEC], sr, kLn1000);
        c_burst = decay_coef(p[P_GAP], sr, kLn100);      /* a burst is -40 dB by the next */
        gap_n = (int) (p[P_GAP] * 0.001f * sr);
        const float d = p[P_DRIVE] * 0.01f;
        drive_g = 1.0f + 15.0f * d * d;
        drive_norm = 1.0f / sat(drive_g);
        /* Mod 100% = an index of 2 cycles (4 pi rad): bright, not yet noise.
         * Squared, so the low end of the knob has the resolution. */
        const float m = p[P_MOD] * 0.01f;
        mod_amt = 2.0f * m * m;
        /* Feedback 100% = 0.25 cycles of the modulator's own last output:
         * well into the noisy region, which is where hats and cymbals live. */
        fb_amt = 0.25f * p[P_FB] * 0.01f;
        tone_lvl = p[P_TONE] * 0.01f;
        const float n = p[P_NOISE] * 0.01f;
        noise_lvl = n * n;
        nmode = (int) p[P_NMODE];
        /* Res 0..100% = Q 0.5..20, exponentially. */
        nf.set(p[P_NFREQ] * nfreq_mul, 0.5f * std::pow(40.0f, p[P_NRES] * 0.01f), sr);
        lowcut_on = p[P_LOWCUT] > 20.5f;
        lc.set(p[P_LOWCUT], 0.7071f, sr);
    }

    void hit(float vel01, float tune_st) {
        /* d: how far below a full-velocity hit this one is. Every target
         * moves by amount x d, so d = 0 is always the knobs as set. */
        const float d = 1.0f - vel01;
        base_hz = p[P_PITCH] * std::pow(2.0f, (tune_st - p[P_VPITCH] * d) / 12.0f);
        sweep_hz = base_hz * (std::pow(2.0f, p[P_SWEEP] * (1.0f - p[P_VSWEEP] * 0.01f * d) / 12.0f) - 1.0f);
        vel_gain  = 1.0f - p[P_VEL] * 0.01f * d;
        vel_mod   = 1.0f - p[P_VMOD] * 0.01f * d;
        vel_noise = 1.0f - p[P_VNOISE] * 0.01f * d;
        /* Decay: +100% makes a silent-velocity hit a quarter as long, -100%
         * four times as long (two octaves of time either way). */
        decay_mul = std::pow(2.0f, -2.0f * p[P_VDECAY] * 0.01f * d);
        /* Noise Freq: 100% puts a silent-velocity hit's filter 4 octaves down. */
        nfreq_mul = std::pow(2.0f, -4.0f * p[P_VNFREQ] * 0.01f * d);
        c_amp = decay_coef(p[P_DECAY] * decay_mul, sr, kLn1000);
        nf.set(p[P_NFREQ] * nfreq_mul, 0.5f * std::pow(40.0f, p[P_NRES] * 0.01f), sr);
        env_amp = env_sw = env_mod = env_noise = 1.0f;
        bursts_left = (int) p[P_CLAPS] - 1;
        burst_count = gap_n;
        ph_c = ph_m = 0;
        fb1 = fb2 = 0.0f;
    }

    float noise() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (int32_t) rng * (1.0f / 2147483648.0f);
    }

    void block(float *out, int n) {
        const float to_inc = 4294967296.0f / sr;
        const float ratio = p[P_RATIO];
        const int track = p[P_TRACK] >= 0.5f;
        const float idx = mod_amt * vel_mod;
        const float out_g = 0.8f * vel_gain;
        for (int i = 0; i < n; i++) {
            if (env_amp < 1e-6f && env_noise < 1e-6f && bursts_left == 0) {
                out[i] = 0.0f;
                kp_gate_frame(&gate, 0.0f);
                continue;
            }
            /* tone */
            const float f = base_hz + sweep_hz * env_sw;
            const float fm_hz = ratio * (track ? f : base_hz);
            const float m = sine(ph_m + cycles(fb_amt * 0.5f * (fb1 + fb2)));
            fb2 = fb1; fb1 = m;
            const float c = sine(ph_c + cycles(idx * env_mod * m));
            ph_m += (uint32_t) (fm_hz * to_inc);
            ph_c += (uint32_t) (f * to_inc);
            float x = c * env_amp * tone_lvl;
            env_amp *= c_amp; env_sw *= c_sw; env_mod *= c_mod;

            /* noise, with the clap's bursts ahead of its tail */
            if (noise_lvl > 0.0f) x += nf.run(noise(), nmode) * env_noise * noise_lvl * vel_noise;
            if (bursts_left > 0) {
                env_noise *= c_burst;
                if (--burst_count <= 0) { env_noise = 1.0f; bursts_left--; burst_count = gap_n; }
            } else {
                env_noise *= c_noise;
            }

            if (drive_g > 1.0f) x = sat(x * drive_g) * drive_norm;
            if (lowcut_on) x = lc.run(x, 2);
            x *= out_g;
            out[i] = x;
            kp_gate_frame(&gate, x);
        }
    }
};

void *create(int sr) { return new (std::nothrow) Voice((float) sr); }

void destroy(void *e) { delete static_cast<Voice *>(e); }

void set(void *e, int idx, float display) {
    Voice *v = static_cast<Voice *>(e);
    if (!v || idx < 0 || idx >= NP) return;
    const dr32_eparam &ep = PARAMS[idx];
    v->p[idx] = display < ep.min ? ep.min : (display > ep.max ? ep.max : display);
    v->dirty = 1;
}

void note_on(void *e, float vel01, float tune_st) {
    Voice *v = static_cast<Voice *>(e);
    if (!v) return;
    v->pend_vel = vel01 < 0.0f ? 0.0f : (vel01 > 1.0f ? 1.0f : vel01);
    v->pend_tune = tune_st;
    v->pending = 1;
    kp_gate_hit(&v->gate);
}

/* DR32's own ramp is the choke; the gate stops the voice once it is quiet. */
void choke(void *e) { (void) e; }

int render(void *e, float *out, int n) {
    Voice *v = static_cast<Voice *>(e);
    if (!v || !v->gate.active) {
        std::memset(out, 0, sizeof(float) * (size_t) n);
        return 0;
    }
    if (v->dirty) v->derive();
    if (v->pending) { v->pending = 0; v->hit(v->pend_vel, v->pend_tune); }
    v->block(out, n);
    return kp_gate_end(&v->gate);
}

/* ---- models: DR32's own starting points --------------------------------- */

struct Preset { const char *slug, *name; float v[NP]; };
/* The hard hit is the first 19 columns; the velocity page says how softer
 * hits depart from it.
 *                        pitch decay swp swdec tone drv lowcut | ratio mod mdec fb trk | noise ndec nfreq nres nmode claps gap | vlvl vmod vswp vptch vdec vnse vnfrq */
const Preset PRESETS[] = {
    {"fm/kick",    "Kick",       {50,  450,  30, 60,  100, 20, 20,    2.0f,  25, 40,   0,  1,   8,   10,   4000, 0,  0, 1, 10,   60, 50, 50, 0, 20, 50, 50}},
    {"fm/snare",   "Snare",      {185, 180,  12, 20,  70,  10, 120,   1.47f, 40, 60,   10, 1,   70,  200,  1800, 10, 2, 1, 10,   60, 50, 40, 2, 30, 60, 60}},
    {"fm/tom",     "Tom",        {110, 400,  7,  80,  100, 0,  40,    1.0f,  15, 80,   0,  1,   5,   30,   3000, 0,  0, 1, 10,   60, 40, 50, 2, 30, 30, 30}},
    {"fm/clap",    "Clap",       {800, 20,   0,  1,   0,   15, 300,   1.0f,  0,  1,    0,  1,   100, 250,  1200, 30, 1, 4, 11,   60, 0,  0,  0, 20, 0,  60}},
    {"fm/rim",     "Rim",        {480, 40,   5,  5,   100, 0,  200,   3.21f, 50, 15,   20, 1,   20,  8,    6000, 0,  2, 1, 10,   60, 50, 0,  1, 0,  30, 40}},
    {"fm/cowbell", "Cowbell",    {540, 350,  0,  1,   100, 30, 250,   1.48f, 35, 300,  0,  0,   0,   10,   5000, 0,  2, 1, 10,   60, 30, 0,  0, 20, 0,  0}},
    {"fm/chat",    "Closed Hat", {1200, 60,  0,  1,   60,  0,  6000,  3.37f, 80, 100,  70, 0,   60,  50,   8000, 0,  2, 1, 10,   60, 30, 0,  0, 30, 40, 50}},
    {"fm/ohat",    "Open Hat",   {1200, 500, 0,  1,   60,  0,  6000,  3.37f, 80, 400,  70, 0,   60,  450,  8000, 0,  2, 1, 10,   60, 30, 0,  0, 30, 40, 50}},
    {"fm/cymbal",  "Cymbal",     {900, 1500, 0,  1,   70,  0,  3000,  2.76f, 90, 1200, 60, 0,   50,  1200, 7000, 0,  2, 1, 10,   60, 40, 0,  0, 40, 40, 50}},
};
const int NM = (int) (sizeof(PRESETS) / sizeof(PRESETS[0]));

struct Models {
    dr32_model m[NM];
    Models() {
        for (int i = 0; i < NM; i++)
            m[i] = dr32_model{PRESETS[i].slug, PRESETS[i].name, DR32_ENG_FM, PRESETS[i].v, 0.0f, 0.0f};
    }
};
const Models M;

}  // namespace

extern "C" {

const dr32_engine_ops dr32_engine_fm = {
    DR32_ENG_FM, "fm", "FM Drum", "fm_", NP, PARAMS,
    create, destroy, set, note_on, choke, render, DR32_FAM_FM,
};

const dr32_model *dr32_fm_models(int *count) {
    if (count) *count = NM;
    return M.m;
}

}
