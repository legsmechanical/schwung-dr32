/*
 * fm_engine.cpp — DR32's own FM drum voices: FOUR engines on ONE core.
 *
 * Written for DR32 (Josh, 2026-09-22: "from scratch"), after the IDEA of the
 * Elektron Machinedrum's EFM machines: a two-operator FM voice with a pitch
 * sweep, plus a filtered noise layer. No code is taken from anywhere; in
 * particular not from ctag-fh-kiel/md-drum-synth, which prompted this and
 * carries no licence.
 *
 * ⭐ WHY FOUR ENGINES (Josh, 2026-09-22, after hearing the first cut: "pitch
 * is way too compressed at the minimum side ... makes it harder to dial in
 * lower pitches and faster decays"; "are there tweaks we can bake to
 * ranges/available params so that they're more suitable to the drum type").
 * The host's knob is LINEAR — every detent is half a percent of the range
 * (knob_engine.mjs) — so a range wide enough for every drum leaves each drum
 * a few detents. One engine per drum TYPE gives each its own ranges and only
 * the knobs it uses; they share this one core. All four are FM (Josh: "keep
 * all the engines fm-based"):
 *
 *   FM Kick   Kick, Tom            low pitch, short sweeps, a click of noise
 *   FM Snare  Snare, Clap, Rim     mid pitch, noise-led, BURSTS (the clap's
 *                                  repeats; "Claps" read as a clap sound)
 *   FM Metal  hats, cymbal, bell   THREE operator pairs at the 808's
 *                                  inharmonic ratios, driven hard: a dense
 *                                  cluster, not the one pair's bell
 *   FM Perc   the weird one        wide ranges ON PURPOSE, plus a Mangle page
 *                                  (noise FM, ring, crush, bits)
 *
 * The core:
 *   tone      FM/Snare/Perc: a sine carrier at Pitch, swept from Sweep
 *             semitones away (BIPOLAR: - rises into the note) over Sweep
 *             Decay, phase-modulated by a sine at Ratio x the carrier (Track
 *             On: it follows the sweep) under its own envelope (Mod Decay),
 *             with self-Feedback.
 *             Metal: three such pairs, carriers at Pitch x {1, 1.8, 2.63},
 *             modulators at Pitch x {1.48, 2.55, 3.90} — the six ratios of
 *             the 808's cymbal oscillators (a published circuit fact), each
 *             raised to Spread, so 0 collapses them to one pitch. Each
 *             pair's carrier is also bent by the one before it, and feedback
 *             runs twice as deep: that CROSS-modulation is what turns three
 *             bells into a wash (measured: the share of the spectrum above
 *             4 kHz within 20 dB of its peak went from 0.13-0.17 on the first
 *             engine's hats to 0.6-0.9; tests/test_fm.c pins it).
 *   noise     white, through a resonant SVF (LP/BP/HP), under its own
 *             envelope; Bursts > 1 fires that many, Burst Gap apart, before
 *             the last one decays over Noise Decay.
 *   mangle    (Perc) Noise FM: the filtered noise phase-modulates the
 *             carrier. Ring: the tone times the noise. Crush: sample-and-hold
 *             to a lower rate. Bits: quantise.
 *   output    (the Output page; Josh asked for a "Mixer page") Mix balances
 *             the layers (left = tone only, centre = both, right = noise
 *             only) -> Drive (soft clip) -> Low Cut and High Cut (12 dB
 *             each). ⚠ NOT named "Mix": DR32's own per-pad Mix page sits in
 *             the same nav list, and two "Mix" pages on one pad is a guess.
 *
 * Every "decay" is the time to fall 60 dB, so a knob's number is the length
 * you hear. Each layer's own Level sits on knob 1 of its page (Josh).
 *
 * ⭐ VELOCITY (its own page; Josh: "so velocity changes tone in a meaningful
 * way"). One law for every target: a FULL-velocity hit is exactly the knobs
 * as set, and a softer one moves away from them by the amount's share of
 * (1 - velocity). So an amount of 0 is "velocity does not touch this", and
 * turning an amount up never changes a hard hit.
 *
 * ⭐ COST. Per sounding sample: two table sines (six for Metal), one noise
 * draw, up to three SVFs. Once the tone and noise envelopes are both below
 * -120 dB and no burst is pending, the voice writes zeros without computing
 * (the gate then stops it 100 ms later).
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

/* ---- the core's parameters ----------------------------------------------
 * Every engine's table maps its knobs onto these. A slot an engine does not
 * show keeps the core default below (Bursts 1, no mangling, cuts open). */
enum {
    C_TONE, C_PITCH, C_DECAY, C_SWEEP, C_SWDEC,
    C_RATIO, C_MOD, C_MDEC, C_FB, C_TRACK, C_SPREAD,
    C_NOISE, C_NDEC, C_NFREQ, C_NRES, C_NMODE, C_BURSTS, C_GAP,
    C_NZFM, C_RING, C_CRUSH, C_BITS,
    C_MIX, C_DRIVE, C_LOWCUT, C_HICUT,
    C_VEL, C_VMOD, C_VSWEEP, C_VPITCH, C_VDECAY, C_VNOISE, C_VNFREQ,
    NC
};

const float CORE_DEF[NC] = {
    100, 200, 300, 0, 50,
    1, 0, 60, 0, 1, 67,
    0, 100, 5000, 0, 2, 1, 10,
    0, 0, 0, 16,
    0, 0, 20, 20000,
    60, 50, 0, 0, 0, 0, 0,
};

enum Kind { K_FM, K_METAL };

/* One engine: its knob table, which core slot each knob drives, its tone
 * source. */
struct Engine {
    Kind kind;
    int n;
    const dr32_eparam *params;
    const int *slot;
    float hicut_off;     /* High Cut at or above this = off */
};

/* ---- the four tables ----------------------------------------------------
 * ⚠ A table's ORDER IS ITS PAGE ORDER (and the model arrays below follow it).
 * Keys are engine-prefixed (fk_, fs_, fx_, fp_): one hierarchy holds them all
 * and a repeated key drops ALL the host's metadata (CLAUDE.md). The suffix
 * after the prefix is the same knob on every engine — tests rely on it. */

#define VEL_PAGE(P) \
    {P "vel",    "Vel>Level", "VLVL",  0.0f,    100.0f, 60.0f, 1.0f, "%",  "Velocity", nullptr}, \
    {P "vmod",   "Vel>Mod",   "VMOD",  0.0f,    100.0f, 50.0f, 1.0f, "%",  "Velocity", nullptr}

#define VEL_REST(P) \
    {P "vpitch", "Vel>Pitch", "VPTCH", 0.0f,    12.0f,  0.0f,  0.1f, "st", "Velocity", nullptr}, \
    {P "vdecay", "Vel>Decay", "VDEC",  -100.0f, 100.0f, 0.0f,  1.0f, "%",  "Velocity", nullptr}, \
    {P "vnoise", "Vel>Noise", "VNSE",  0.0f,    100.0f, 0.0f,  1.0f, "%",  "Velocity", nullptr}, \
    {P "vnfreq", "Vel>NFreq", "VNFRQ", 0.0f,    100.0f, 0.0f,  1.0f, "%",  "Velocity", nullptr}

#define VEL_SWEEP(P) \
    {P "vsweep", "Vel>Sweep", "VSWP",  0.0f,    100.0f, 0.0f,  1.0f, "%",  "Velocity", nullptr}

#define MIX_PAGE(P, LCMAX, HCMIN) \
    {P "mix",    "Mix",       "MIX",   -100.0f, 100.0f, 0.0f,  1.0f, "%",  "Output", nullptr}, \
    {P "drive",  "Drive",     "DRIVE", 0.0f,    100.0f, 0.0f,  1.0f, "%",  "Output", nullptr}, \
    {P "lowcut", "Low Cut",   "LOCUT", 20.0f,   LCMAX,  20.0f, 1.0f, "hz", "Output", nullptr}, \
    {P "hicut",  "High Cut",  "HICUT", HCMIN,   20000.0f, 20000.0f, 10.0f, "hz", "Output", nullptr}

#define VEL_SLOTS_FULL  C_VEL, C_VMOD, C_VSWEEP, C_VPITCH, C_VDECAY, C_VNOISE, C_VNFREQ
#define MIX_SLOTS       C_MIX, C_DRIVE, C_LOWCUT, C_HICUT

/* FM Kick: Kick, Tom. Pitch to 300 Hz is ~1 Hz a detent. */
const dr32_eparam KICK_P[] = {
    {"fk_tone",   "Tone Level",  "LEVEL", 0.0f,   100.0f,  100.0f, 1.0f,  "%",  "Tone",  nullptr},
    {"fk_pitch",  "Pitch",       "PITCH", 20.0f,  300.0f,  50.0f,  1.0f,  "hz", "Tone",  nullptr},
    {"fk_decay",  "Decay",       "DECAY", 10.0f,  1500.0f, 400.0f, 1.0f,  "ms", "Tone",  nullptr},
    {"fk_sweep",  "Sweep",       "SWEEP", -48.0f, 48.0f,   24.0f,  1.0f,  "st", "Tone",  nullptr},
    {"fk_swdec",  "Sweep Decay", "SWDEC", 1.0f,   200.0f,  50.0f,  1.0f,  "ms", "Tone",  nullptr},
    {"fk_ratio",  "Ratio",       "RATIO", 0.25f,  8.0f,    2.0f,   0.01f, nullptr, "FM", nullptr},
    {"fk_mod",    "Mod",         "MOD",   0.0f,   100.0f,  20.0f,  1.0f,  "%",  "FM",    nullptr},
    {"fk_mdec",   "Mod Decay",   "MDEC",  1.0f,   500.0f,  40.0f,  1.0f,  "ms", "FM",    nullptr},
    {"fk_fb",     "Feedback",    "FB",    0.0f,   100.0f,  0.0f,   1.0f,  "%",  "FM",    nullptr},
    {"fk_track",  "Mod Track",   "TRACK", 0.0f,   1.0f,    1.0f,   1.0f,  nullptr, "FM", "Off|On"},
    {"fk_noise",  "Noise Level", "LEVEL", 0.0f,   100.0f,  0.0f,   1.0f,  "%",  "Noise", nullptr},
    {"fk_ndec",   "Noise Decay", "NDEC",  1.0f,   200.0f,  10.0f,  1.0f,  "ms", "Noise", nullptr},
    {"fk_nfreq",  "Noise Freq",  "NFREQ", 100.0f, 16000.0f, 4000.0f, 10.0f, "hz", "Noise", nullptr},
    {"fk_nres",   "Noise Res",   "NRES",  0.0f,   100.0f,  0.0f,   1.0f,  "%",  "Noise", nullptr},
    {"fk_nmode",  "Noise Filter","NFILT", 0.0f,   2.0f,    0.0f,   1.0f,  nullptr, "Noise", "LP|BP|HP"},
    MIX_PAGE("fk_", 1000.0f, 200.0f),
    VEL_PAGE("fk_"), VEL_SWEEP("fk_"), VEL_REST("fk_"),
};
const int KICK_S[] = {
    C_TONE, C_PITCH, C_DECAY, C_SWEEP, C_SWDEC,
    C_RATIO, C_MOD, C_MDEC, C_FB, C_TRACK,
    C_NOISE, C_NDEC, C_NFREQ, C_NRES, C_NMODE,
    MIX_SLOTS, VEL_SLOTS_FULL,
};

/* FM Snare: Snare, Clap, Rim. */
const dr32_eparam SNARE_P[] = {
    {"fs_tone",   "Tone Level",  "LEVEL", 0.0f,   100.0f,  70.0f,  1.0f,  "%",  "Tone",  nullptr},
    {"fs_pitch",  "Pitch",       "PITCH", 60.0f,  1000.0f, 185.0f, 1.0f,  "hz", "Tone",  nullptr},
    {"fs_decay",  "Decay",       "DECAY", 5.0f,   1000.0f, 180.0f, 1.0f,  "ms", "Tone",  nullptr},
    {"fs_sweep",  "Sweep",       "SWEEP", -48.0f, 48.0f,   12.0f,  1.0f,  "st", "Tone",  nullptr},
    {"fs_swdec",  "Sweep Decay", "SWDEC", 1.0f,   200.0f,  20.0f,  1.0f,  "ms", "Tone",  nullptr},
    {"fs_ratio",  "Ratio",       "RATIO", 0.25f,  16.0f,   1.47f,  0.01f, nullptr, "FM", nullptr},
    {"fs_mod",    "Mod",         "MOD",   0.0f,   100.0f,  40.0f,  1.0f,  "%",  "FM",    nullptr},
    {"fs_mdec",   "Mod Decay",   "MDEC",  1.0f,   1000.0f, 60.0f,  1.0f,  "ms", "FM",    nullptr},
    {"fs_fb",     "Feedback",    "FB",    0.0f,   100.0f,  10.0f,  1.0f,  "%",  "FM",    nullptr},
    {"fs_track",  "Mod Track",   "TRACK", 0.0f,   1.0f,    1.0f,   1.0f,  nullptr, "FM", "Off|On"},
    {"fs_noise",  "Noise Level", "LEVEL", 0.0f,   100.0f,  70.0f,  1.0f,  "%",  "Noise", nullptr},
    {"fs_ndec",   "Noise Decay", "NDEC",  1.0f,   1000.0f, 200.0f, 1.0f,  "ms", "Noise", nullptr},
    {"fs_nfreq",  "Noise Freq",  "NFREQ", 100.0f, 16000.0f, 1800.0f, 10.0f, "hz", "Noise", nullptr},
    {"fs_nres",   "Noise Res",   "NRES",  0.0f,   100.0f,  10.0f,  1.0f,  "%",  "Noise", nullptr},
    {"fs_nmode",  "Noise Filter","NFILT", 0.0f,   2.0f,    2.0f,   1.0f,  nullptr, "Noise", "LP|BP|HP"},
    {"fs_bursts", "Bursts",      "BRSTS", 1.0f,   6.0f,    1.0f,   1.0f,  nullptr, "Noise", nullptr},
    {"fs_gap",    "Burst Gap",   "GAP",   2.0f,   40.0f,   10.0f,  1.0f,  "ms", "Noise", nullptr},
    MIX_PAGE("fs_", 2000.0f, 500.0f),
    VEL_PAGE("fs_"), VEL_SWEEP("fs_"), VEL_REST("fs_"),
};
const int SNARE_S[] = {
    C_TONE, C_PITCH, C_DECAY, C_SWEEP, C_SWDEC,
    C_RATIO, C_MOD, C_MDEC, C_FB, C_TRACK,
    C_NOISE, C_NDEC, C_NFREQ, C_NRES, C_NMODE, C_BURSTS, C_GAP,
    MIX_SLOTS, VEL_SLOTS_FULL,
};

/* FM Metal: hats, cymbal, cowbell. No sweep: a hat does not bend. */
const dr32_eparam METAL_P[] = {
    {"fx_tone",   "Tone Level",  "LEVEL", 0.0f,   100.0f,  100.0f, 1.0f,  "%",  "Tone",  nullptr},
    {"fx_pitch",  "Pitch",       "PITCH", 100.0f, 1000.0f, 400.0f, 1.0f,  "hz", "Tone",  nullptr},
    {"fx_decay",  "Decay",       "DECAY", 5.0f,   1500.0f, 80.0f,  1.0f,  "ms", "Tone",  nullptr},
    {"fx_spread", "Spread",      "SPRD",  0.0f,   100.0f,  67.0f,  1.0f,  "%",  "Metal", nullptr},
    {"fx_mod",    "Mod",         "MOD",   0.0f,   100.0f,  70.0f,  1.0f,  "%",  "Metal", nullptr},
    {"fx_mdec",   "Mod Decay",   "MDEC",  1.0f,   1500.0f, 200.0f, 1.0f,  "ms", "Metal", nullptr},
    {"fx_fb",     "Feedback",    "FB",    0.0f,   100.0f,  40.0f,  1.0f,  "%",  "Metal", nullptr},
    {"fx_noise",  "Noise Level", "LEVEL", 0.0f,   100.0f,  40.0f,  1.0f,  "%",  "Noise", nullptr},
    {"fx_ndec",   "Noise Decay", "NDEC",  1.0f,   1500.0f, 60.0f,  1.0f,  "ms", "Noise", nullptr},
    {"fx_nfreq",  "Noise Freq",  "NFREQ", 1000.0f, 16000.0f, 8000.0f, 10.0f, "hz", "Noise", nullptr},
    {"fx_nres",   "Noise Res",   "NRES",  0.0f,   100.0f,  0.0f,   1.0f,  "%",  "Noise", nullptr},
    {"fx_nmode",  "Noise Filter","NFILT", 0.0f,   2.0f,    2.0f,   1.0f,  nullptr, "Noise", "LP|BP|HP"},
    MIX_PAGE("fx_", 10000.0f, 1000.0f),
    VEL_PAGE("fx_"), VEL_REST("fx_"),
};
const int METAL_S[] = {
    C_TONE, C_PITCH, C_DECAY,
    C_SPREAD, C_MOD, C_MDEC, C_FB,
    C_NOISE, C_NDEC, C_NFREQ, C_NRES, C_NMODE,
    MIX_SLOTS,
    C_VEL, C_VMOD, C_VPITCH, C_VDECAY, C_VNOISE, C_VNFREQ,
};

/* FM Perc: the first engine's wide ranges, kept for the strange, plus Mangle. */
const dr32_eparam PERC_P[] = {
    {"fp_tone",   "Tone Level",  "LEVEL", 0.0f,   100.0f,  100.0f, 1.0f,  "%",  "Tone",  nullptr},
    {"fp_pitch",  "Pitch",       "PITCH", 20.0f,  2000.0f, 300.0f, 1.0f,  "hz", "Tone",  nullptr},
    {"fp_decay",  "Decay",       "DECAY", 5.0f,   4000.0f, 300.0f, 1.0f,  "ms", "Tone",  nullptr},
    {"fp_sweep",  "Sweep",       "SWEEP", -48.0f, 48.0f,   0.0f,   1.0f,  "st", "Tone",  nullptr},
    {"fp_swdec",  "Sweep Decay", "SWDEC", 1.0f,   1000.0f, 50.0f,  1.0f,  "ms", "Tone",  nullptr},
    {"fp_ratio",  "Ratio",       "RATIO", 0.25f,  16.0f,   1.5f,   0.01f, nullptr, "FM", nullptr},
    {"fp_mod",    "Mod",         "MOD",   0.0f,   100.0f,  30.0f,  1.0f,  "%",  "FM",    nullptr},
    {"fp_mdec",   "Mod Decay",   "MDEC",  1.0f,   2000.0f, 80.0f,  1.0f,  "ms", "FM",    nullptr},
    {"fp_fb",     "Feedback",    "FB",    0.0f,   100.0f,  0.0f,   1.0f,  "%",  "FM",    nullptr},
    {"fp_track",  "Mod Track",   "TRACK", 0.0f,   1.0f,    1.0f,   1.0f,  nullptr, "FM", "Off|On"},
    {"fp_noise",  "Noise Level", "LEVEL", 0.0f,   100.0f,  0.0f,   1.0f,  "%",  "Noise", nullptr},
    {"fp_ndec",   "Noise Decay", "NDEC",  1.0f,   2000.0f, 100.0f, 1.0f,  "ms", "Noise", nullptr},
    {"fp_nfreq",  "Noise Freq",  "NFREQ", 100.0f, 16000.0f, 5000.0f, 10.0f, "hz", "Noise", nullptr},
    {"fp_nres",   "Noise Res",   "NRES",  0.0f,   100.0f,  0.0f,   1.0f,  "%",  "Noise", nullptr},
    {"fp_nmode",  "Noise Filter","NFILT", 0.0f,   2.0f,    1.0f,   1.0f,  nullptr, "Noise", "LP|BP|HP"},
    {"fp_bursts", "Bursts",      "BRSTS", 1.0f,   6.0f,    1.0f,   1.0f,  nullptr, "Noise", nullptr},
    {"fp_gap",    "Burst Gap",   "GAP",   2.0f,   40.0f,   10.0f,  1.0f,  "ms", "Noise", nullptr},
    {"fp_nzfm",   "Noise FM",    "NZFM",  0.0f,   100.0f,  0.0f,   1.0f,  "%",  "Mangle", nullptr},
    {"fp_ring",   "Ring",        "RING",  0.0f,   100.0f,  0.0f,   1.0f,  "%",  "Mangle", nullptr},
    {"fp_crush",  "Crush",       "CRUSH", 0.0f,   100.0f,  0.0f,   1.0f,  "%",  "Mangle", nullptr},
    {"fp_bits",   "Bits",        "BITS",  2.0f,   16.0f,   16.0f,  1.0f,  nullptr, "Mangle", nullptr},
    MIX_PAGE("fp_", 8000.0f, 200.0f),
    VEL_PAGE("fp_"), VEL_SWEEP("fp_"), VEL_REST("fp_"),
};
const int PERC_S[] = {
    C_TONE, C_PITCH, C_DECAY, C_SWEEP, C_SWDEC,
    C_RATIO, C_MOD, C_MDEC, C_FB, C_TRACK,
    C_NOISE, C_NDEC, C_NFREQ, C_NRES, C_NMODE, C_BURSTS, C_GAP,
    C_NZFM, C_RING, C_CRUSH, C_BITS,
    MIX_SLOTS, VEL_SLOTS_FULL,
};

#define COUNT(a) ((int) (sizeof(a) / sizeof((a)[0])))
static_assert(COUNT(KICK_P) == COUNT(KICK_S), "kick table/slots");
static_assert(COUNT(SNARE_P) == COUNT(SNARE_S), "snare table/slots");
static_assert(COUNT(METAL_P) == COUNT(METAL_S), "metal table/slots");
static_assert(COUNT(PERC_P) == COUNT(PERC_S), "perc table/slots");

const Engine ENG_KICK  = {K_FM,    COUNT(KICK_P),  KICK_P,  KICK_S,  19999.0f};
const Engine ENG_SNARE = {K_FM,    COUNT(SNARE_P), SNARE_P, SNARE_S, 19999.0f};
const Engine ENG_METAL = {K_METAL, COUNT(METAL_P), METAL_P, METAL_S, 19999.0f};
const Engine ENG_PERC  = {K_FM,    COUNT(PERC_P),  PERC_P,  PERC_S,  19999.0f};

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

/* Metal's pairs: carrier and modulator ratios to Pitch, at Spread 100% of
 * the knob's "natural" point (see derive). */
constexpr int kPairs = 3;
const float METAL_C[kPairs] = {1.000f, 1.800f, 2.630f};
const float METAL_M[kPairs] = {1.483f, 2.546f, 3.897f};

struct Voice {
    const Engine *eng;
    float sr;
    float c[NC];            /* the core's values; the engine's knobs write through `slot` */
    int dirty = 1;
    kp_gate gate;

    /* derived, recomputed when a knob moves */
    float c_amp = 0, c_sw = 0, c_mod = 0, c_noise = 0, c_burst = 0;
    int   gap_n = 0;
    float drive_g = 1, drive_norm = 1;
    float mod_amt = 0, fb_amt = 0, tone_lvl = 1, noise_lvl = 0;
    float nzfm_amt = 0, ring_amt = 0, bits_q = 0, cross_amt = 0;
    int   crush_n = 1;
    int   nmode = 2, lowcut_on = 0, hicut_on = 0;
    float mc[kPairs] = {1, 1, 1}, mm[kPairs] = {1, 1, 1};
    Svf   nf, lc, hc;

    /* per hit */
    float base_hz = 0, sweep_hz = 0, vel_gain = 1, vel_mod = 1, vel_noise = 1;
    float decay_mul = 1, nfreq_mul = 1;      /* velocity's, set per hit, used by derive() */
    float env_amp = 0, env_sw = 0, env_mod = 0, env_noise = 0;
    int   bursts_left = 0, burst_count = 0;
    uint32_t ph_c[kPairs] = {0, 0, 0}, ph_m[kPairs] = {0, 0, 0};
    float fb1[kPairs] = {0, 0, 0}, fb2[kPairs] = {0, 0, 0};
    float hold = 0; int hold_n = 0;
    float last_c = 0;
    uint32_t rng = 0x9e3779b9u;
    int   pending = 0;
    float pend_vel = 1, pend_tune = 0;

    Voice(const Engine *e, float fs) : eng(e), sr(fs) {
        for (int i = 0; i < NC; i++) c[i] = CORE_DEF[i];
        for (int i = 0; i < e->n; i++) c[e->slot[i]] = e->params[i].def;
        kp_gate_init(&gate, (int) fs);
    }

    void derive() {
        dirty = 0;
        c_amp = decay_coef(c[C_DECAY] * decay_mul, sr, kLn1000);
        c_sw  = decay_coef(c[C_SWDEC], sr, kLn1000);
        c_mod = decay_coef(c[C_MDEC], sr, kLn1000);
        c_noise = decay_coef(c[C_NDEC], sr, kLn1000);
        c_burst = decay_coef(c[C_GAP], sr, kLn100);      /* a burst is -40 dB by the next */
        gap_n = (int) (c[C_GAP] * 0.001f * sr);
        const float d = c[C_DRIVE] * 0.01f;
        drive_g = 1.0f + 15.0f * d * d;
        drive_norm = 1.0f / sat(drive_g);
        /* FM: Mod 100% = an index of 2 cycles (4 pi rad): bright, not yet
         * noise. Metal: 4 cycles, where three pairs smear into a wash.
         * Squared, so the low end of the knob has the resolution. */
        const float m = c[C_MOD] * 0.01f;
        mod_amt = (eng->kind == K_METAL ? 4.0f : 2.0f) * m * m;
        /* Feedback 100% = 0.25 cycles of the modulator's own last output:
         * well into the noisy region. */
        fb_amt = (eng->kind == K_METAL ? 0.5f : 0.25f) * c[C_FB] * 0.01f;
        /* Metal's cross-modulation follows Mod: 100% bends each carrier by
         * a cycle of the one before. */
        cross_amt = eng->kind == K_METAL ? 1.0f * m * m : 0.0f;
        /* Mix: left of centre fades the noise out, right fades the tone. */
        const float mix = c[C_MIX] * 0.01f;
        tone_lvl = c[C_TONE] * 0.01f * (mix > 0.0f ? 1.0f - mix : 1.0f);
        const float n = c[C_NOISE] * 0.01f;
        noise_lvl = n * n * (mix < 0.0f ? 1.0f + mix : 1.0f);
        nmode = (int) c[C_NMODE];
        /* Res 0..100% = Q 0.5..20, exponentially. */
        nf.set(c[C_NFREQ] * nfreq_mul, 0.5f * std::pow(40.0f, c[C_NRES] * 0.01f), sr);
        lowcut_on = c[C_LOWCUT] > 20.5f;
        lc.set(c[C_LOWCUT], 0.7071f, sr);
        hicut_on = c[C_HICUT] < eng->hicut_off;
        hc.set(c[C_HICUT], 0.7071f, sr);
        /* Spread 0..100% raises the ratios to 0..1.5: 0 is one pitch, 67% the
         * 808's own spacing, above it wider still. */
        const float sp = 1.5f * c[C_SPREAD] * 0.01f;
        for (int k = 0; k < kPairs; k++) { mc[k] = std::pow(METAL_C[k], sp); mm[k] = std::pow(METAL_M[k], sp); }
        /* Mangle. Noise FM 100% = 2 cycles of phase from the filtered noise.
         * Crush 100% holds each sample for 64. Bits 16 is off. */
        const float z = c[C_NZFM] * 0.01f;
        nzfm_amt = 2.0f * z * z;
        ring_amt = c[C_RING] * 0.01f;
        crush_n = (int) std::lround(std::pow(64.0f, c[C_CRUSH] * 0.01f));
        bits_q = c[C_BITS] >= 15.5f ? 0.0f : std::pow(2.0f, c[C_BITS] - 1.0f);
    }

    void hit(float vel01, float tune_st) {
        /* d: how far below a full-velocity hit this one is. Every target
         * moves by amount x d, so d = 0 is always the knobs as set. */
        const float d = 1.0f - vel01;
        base_hz = c[C_PITCH] * std::pow(2.0f, (tune_st - c[C_VPITCH] * d) / 12.0f);
        sweep_hz = base_hz * (std::pow(2.0f, c[C_SWEEP] * (1.0f - c[C_VSWEEP] * 0.01f * d) / 12.0f) - 1.0f);
        vel_gain  = 1.0f - c[C_VEL] * 0.01f * d;
        vel_mod   = 1.0f - c[C_VMOD] * 0.01f * d;
        vel_noise = 1.0f - c[C_VNOISE] * 0.01f * d;
        /* Decay: +100% makes a silent-velocity hit a quarter as long, -100%
         * four times as long (two octaves of time either way). */
        decay_mul = std::pow(2.0f, -2.0f * c[C_VDECAY] * 0.01f * d);
        /* Noise Freq: 100% puts a silent-velocity hit's filter 4 octaves down. */
        nfreq_mul = std::pow(2.0f, -4.0f * c[C_VNFREQ] * 0.01f * d);
        c_amp = decay_coef(c[C_DECAY] * decay_mul, sr, kLn1000);
        nf.set(c[C_NFREQ] * nfreq_mul, 0.5f * std::pow(40.0f, c[C_NRES] * 0.01f), sr);
        env_amp = env_sw = env_mod = env_noise = 1.0f;
        bursts_left = (int) c[C_BURSTS] - 1;
        burst_count = gap_n;
        for (int k = 0; k < kPairs; k++) { ph_c[k] = ph_m[k] = 0; fb1[k] = fb2[k] = 0.0f; }
        hold = 0.0f; hold_n = 0; last_c = 0.0f;
    }

    float noise() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (int32_t) rng * (1.0f / 2147483648.0f);
    }

    /* One FM pair: carrier at f, modulator at fm_hz. Returns the carrier. */
    inline float pair(int k, float f, float fm_hz, float idx, float extra, float to_inc) {
        const float m = sine(ph_m[k] + cycles(fb_amt * 0.5f * (fb1[k] + fb2[k])));
        fb2[k] = fb1[k]; fb1[k] = m;
        const float out = sine(ph_c[k] + cycles(idx * env_mod * m + extra));
        ph_m[k] += (uint32_t) (fm_hz * to_inc);
        ph_c[k] += (uint32_t) (f * to_inc);
        return out;
    }

    void block(float *out, int n) {
        const float to_inc = 4294967296.0f / sr;
        const float ratio = c[C_RATIO];
        const int track = c[C_TRACK] >= 0.5f;
        const int metal = eng->kind == K_METAL;
        const float idx = mod_amt * vel_mod;
        const float out_g = 0.8f * vel_gain;
        const int need_noise = noise_lvl > 0.0f || nzfm_amt > 0.0f || ring_amt > 0.0f;
        for (int i = 0; i < n; i++) {
            if (env_amp < 1e-6f && env_noise < 1e-6f && bursts_left == 0) {
                out[i] = 0.0f;
                kp_gate_frame(&gate, 0.0f);
                continue;
            }
            /* noise first: Perc's Noise FM and Ring read it */
            const float nz = need_noise ? nf.run(noise(), nmode) : 0.0f;

            /* tone */
            float t;
            if (metal) {
                /* Each pair's carrier is also bent by the pair before it:
                 * the three stop being three bells and become one wash. */
                t = 0.0f;
                float prev = last_c;
                for (int k = 0; k < kPairs; k++) {
                    prev = pair(k, base_hz * mc[k], base_hz * mm[k], idx, cross_amt * env_mod * prev, to_inc);
                    t += prev;
                }
                last_c = prev;
                t *= (1.0f / kPairs) * 1.6f;
            } else {
                const float f = base_hz + sweep_hz * env_sw;
                t = pair(0, f, ratio * (track ? f : base_hz), idx, nzfm_amt * nz, to_inc);
                if (ring_amt > 0.0f) t *= 1.0f - ring_amt + ring_amt * 2.0f * nz;
            }
            float x = t * env_amp * tone_lvl;
            env_amp *= c_amp; env_sw *= c_sw; env_mod *= c_mod;

            /* noise, with the bursts ahead of its tail */
            x += nz * env_noise * noise_lvl * vel_noise;
            if (bursts_left > 0) {
                env_noise *= c_burst;
                if (--burst_count <= 0) { env_noise = 1.0f; bursts_left--; burst_count = gap_n; }
            } else {
                env_noise *= c_noise;
            }

            if (crush_n > 1) {
                if (hold_n <= 0) { hold = x; hold_n = crush_n; }
                hold_n--;
                x = hold;
            }
            if (bits_q > 0.0f) x = std::floor(x * bits_q + 0.5f) / bits_q;
            if (drive_g > 1.0f) x = sat(x * drive_g) * drive_norm;
            if (lowcut_on) x = lc.run(x, 2);
            if (hicut_on) x = hc.run(x, 0);
            x *= out_g;
            out[i] = x;
            kp_gate_frame(&gate, x);
        }
    }
};

template <const Engine *E>
void *create(int sr) { return new (std::nothrow) Voice(E, (float) sr); }

void destroy(void *e) { delete static_cast<Voice *>(e); }

void set(void *e, int idx, float display) {
    Voice *v = static_cast<Voice *>(e);
    if (!v || idx < 0 || idx >= v->eng->n) return;
    const dr32_eparam &ep = v->eng->params[idx];
    v->c[v->eng->slot[idx]] = display < ep.min ? ep.min : (display > ep.max ? ep.max : display);
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

/* ---- models: DR32's own starting points ---------------------------------
 * Each lists only what differs from its engine's defaults, by CORE slot. */

struct Set { int slot; float v; };
struct Preset { const char *slug, *name; int engine; const Engine *eng; Set s[24]; };

const Preset PRESETS[] = {
    /* FM Kick */
    {"fm/kick", "Kick", DR32_ENG_FM_KICK, &ENG_KICK, {
        {C_PITCH, 50}, {C_DECAY, 450}, {C_SWEEP, 30}, {C_SWDEC, 60}, {C_DRIVE, 20},
        {C_RATIO, 2.0f}, {C_MOD, 25}, {C_MDEC, 40},
        {C_NOISE, 8}, {C_NDEC, 10}, {C_NFREQ, 4000}, {C_NMODE, 0},
        {C_VEL, 60}, {C_VMOD, 50}, {C_VSWEEP, 50}, {C_VDECAY, 20}, {C_VNOISE, 50}, {C_VNFREQ, 50}, {-1, 0}}},
    {"fm/tom", "Tom", DR32_ENG_FM_KICK, &ENG_KICK, {
        {C_PITCH, 110}, {C_DECAY, 400}, {C_SWEEP, 7}, {C_SWDEC, 80},
        {C_RATIO, 1.0f}, {C_MOD, 15}, {C_MDEC, 80},
        {C_NOISE, 5}, {C_NDEC, 30}, {C_NFREQ, 3000}, {C_NMODE, 0}, {C_LOWCUT, 40},
        {C_VEL, 60}, {C_VMOD, 40}, {C_VSWEEP, 50}, {C_VPITCH, 2}, {C_VDECAY, 30}, {C_VNOISE, 30}, {C_VNFREQ, 30}, {-1, 0}}},
    /* FM Snare */
    {"fm/snare", "Snare", DR32_ENG_FM_SNARE, &ENG_SNARE, {
        {C_TONE, 70}, {C_PITCH, 185}, {C_DECAY, 180}, {C_SWEEP, 12}, {C_SWDEC, 20}, {C_DRIVE, 10}, {C_LOWCUT, 120},
        {C_RATIO, 1.47f}, {C_MOD, 40}, {C_MDEC, 60}, {C_FB, 10},
        {C_NOISE, 70}, {C_NDEC, 200}, {C_NFREQ, 1800}, {C_NRES, 10}, {C_NMODE, 2},
        {C_VEL, 60}, {C_VMOD, 50}, {C_VSWEEP, 40}, {C_VPITCH, 2}, {C_VDECAY, 30}, {C_VNOISE, 60}, {C_VNFREQ, 60}, {-1, 0}}},
    {"fm/clap", "Clap", DR32_ENG_FM_SNARE, &ENG_SNARE, {
        {C_TONE, 0}, {C_PITCH, 800}, {C_DECAY, 20}, {C_SWEEP, 0}, {C_SWDEC, 1}, {C_DRIVE, 15}, {C_LOWCUT, 300},
        {C_RATIO, 1.0f}, {C_MOD, 0}, {C_MDEC, 1}, {C_FB, 0},
        {C_NOISE, 100}, {C_NDEC, 250}, {C_NFREQ, 1200}, {C_NRES, 30}, {C_NMODE, 1}, {C_BURSTS, 4}, {C_GAP, 11},
        {C_VEL, 60}, {C_VMOD, 0}, {C_VDECAY, 20}, {C_VNFREQ, 60}, {-1, 0}}},
    {"fm/rim", "Rim", DR32_ENG_FM_SNARE, &ENG_SNARE, {
        {C_TONE, 100}, {C_PITCH, 480}, {C_DECAY, 40}, {C_SWEEP, 5}, {C_SWDEC, 5}, {C_LOWCUT, 200},
        {C_RATIO, 3.21f}, {C_MOD, 50}, {C_MDEC, 15}, {C_FB, 20},
        {C_NOISE, 20}, {C_NDEC, 8}, {C_NFREQ, 6000}, {C_NRES, 0}, {C_NMODE, 2},
        {C_VEL, 60}, {C_VMOD, 50}, {C_VPITCH, 1}, {C_VNOISE, 30}, {C_VNFREQ, 40}, {-1, 0}}},
    /* FM Metal. Mod Decay near Decay keeps the wash dense to the end (a
     * falling index is what makes a bell), High-passed so no pitch leads. */
    {"fm/chat", "Closed Hat", DR32_ENG_FM_METAL, &ENG_METAL, {
        {C_PITCH, 420}, {C_DECAY, 45}, {C_SPREAD, 67}, {C_MOD, 85}, {C_MDEC, 60}, {C_FB, 60},
        {C_NOISE, 45}, {C_NDEC, 35}, {C_NFREQ, 9000}, {C_NMODE, 2}, {C_LOWCUT, 7000},
        {C_VEL, 60}, {C_VMOD, 30}, {C_VDECAY, 30}, {C_VNOISE, 40}, {C_VNFREQ, 50}, {-1, 0}}},
    {"fm/ohat", "Open Hat", DR32_ENG_FM_METAL, &ENG_METAL, {
        {C_PITCH, 420}, {C_DECAY, 450}, {C_SPREAD, 67}, {C_MOD, 85}, {C_MDEC, 500}, {C_FB, 60},
        {C_NOISE, 45}, {C_NDEC, 350}, {C_NFREQ, 9000}, {C_NMODE, 2}, {C_LOWCUT, 6500},
        {C_VEL, 60}, {C_VMOD, 30}, {C_VDECAY, 30}, {C_VNOISE, 40}, {C_VNFREQ, 50}, {-1, 0}}},
    {"fm/cymbal", "Cymbal", DR32_ENG_FM_METAL, &ENG_METAL, {
        {C_PITCH, 330}, {C_DECAY, 1500}, {C_SPREAD, 75}, {C_MOD, 95}, {C_MDEC, 1500}, {C_FB, 70},
        {C_NOISE, 35}, {C_NDEC, 1200}, {C_NFREQ, 7000}, {C_NMODE, 2}, {C_LOWCUT, 4000},
        {C_VEL, 60}, {C_VMOD, 40}, {C_VDECAY, 40}, {C_VNOISE, 40}, {C_VNFREQ, 50}, {-1, 0}}},
    {"fm/cowbell", "Cowbell", DR32_ENG_FM_METAL, &ENG_METAL, {
        {C_PITCH, 540}, {C_DECAY, 350}, {C_SPREAD, 67}, {C_MOD, 12}, {C_MDEC, 300}, {C_FB, 0},
        {C_NOISE, 0}, {C_DRIVE, 30}, {C_LOWCUT, 250},
        {C_VEL, 60}, {C_VMOD, 30}, {C_VDECAY, 20}, {-1, 0}}},
    /* FM Perc */
    {"fm/perc", "Percussion", DR32_ENG_FM_PERC, &ENG_PERC, {
        {C_PITCH, 260}, {C_DECAY, 250}, {C_SWEEP, 5}, {C_SWDEC, 15},
        {C_RATIO, 1.5f}, {C_MOD, 45}, {C_MDEC, 70}, {C_FB, 15},
        {C_NOISE, 15}, {C_NDEC, 20}, {C_NFREQ, 3000}, {C_NMODE, 1},
        {C_VEL, 60}, {C_VMOD, 60}, {C_VSWEEP, 30}, {C_VPITCH, 1}, {C_VDECAY, 20}, {C_VNOISE, 40}, {-1, 0}}},
    {"fm/zap", "Zap", DR32_ENG_FM_PERC, &ENG_PERC, {
        {C_PITCH, 90}, {C_DECAY, 300}, {C_SWEEP, 48}, {C_SWDEC, 180},
        {C_RATIO, 0.5f}, {C_MOD, 55}, {C_MDEC, 250}, {C_FB, 30}, {C_DRIVE, 25},
        {C_VEL, 60}, {C_VMOD, 50}, {C_VSWEEP, 60}, {-1, 0}}},
    {"fm/drip", "Drip", DR32_ENG_FM_PERC, &ENG_PERC, {
        {C_PITCH, 900}, {C_DECAY, 120}, {C_SWEEP, -24}, {C_SWDEC, 40},
        {C_RATIO, 1.0f}, {C_MOD, 10}, {C_MDEC, 30},
        {C_VEL, 60}, {C_VMOD, 30}, {C_VPITCH, 3}, {-1, 0}}},
    {"fm/glitch", "Glitch", DR32_ENG_FM_PERC, &ENG_PERC, {
        {C_PITCH, 150}, {C_DECAY, 200}, {C_SWEEP, 0},
        {C_RATIO, 7.13f}, {C_MOD, 70}, {C_MDEC, 200}, {C_FB, 50}, {C_TRACK, 0},
        {C_NOISE, 30}, {C_NDEC, 120}, {C_NFREQ, 2500}, {C_NRES, 70}, {C_NMODE, 1}, {C_BURSTS, 3}, {C_GAP, 23},
        {C_NZFM, 60}, {C_RING, 40}, {C_CRUSH, 55}, {C_BITS, 6},
        {C_VEL, 60}, {C_VMOD, 50}, {-1, 0}}},
    {"fm/clank", "Clank", DR32_ENG_FM_PERC, &ENG_PERC, {
        {C_PITCH, 210}, {C_DECAY, 500}, {C_SWEEP, -5}, {C_SWDEC, 8},
        {C_RATIO, 3.53f}, {C_MOD, 80}, {C_MDEC, 120}, {C_FB, 35},
        {C_NOISE, 25}, {C_NDEC, 60}, {C_NFREQ, 4000}, {C_NRES, 50}, {C_NMODE, 1},
        {C_RING, 70}, {C_DRIVE, 35}, {C_LOWCUT, 150},
        {C_VEL, 60}, {C_VMOD, 60}, {C_VDECAY, 30}, {-1, 0}}},
};
constexpr int NM = COUNT(PRESETS);

struct Models {
    float vals[NM][DR32_ENG_MAX_PARAMS];
    dr32_model m[NM];
    Models() {
        for (int i = 0; i < NM; i++) {
            const Preset &p = PRESETS[i];
            const Engine *e = p.eng;
            for (int k = 0; k < e->n; k++) {
                vals[i][k] = e->params[k].def;
                for (const Set *s = p.s; s->slot >= 0; s++)
                    if (s->slot == e->slot[k]) vals[i][k] = s->v;
            }
            m[i] = dr32_model{p.slug, p.name, p.engine, vals[i], 0.0f, 0.0f};
        }
    }
};
const Models M;

}  // namespace

extern "C" {

const dr32_engine_ops dr32_engine_fm_kick = {
    DR32_ENG_FM_KICK, "fm_kick", "FM Kick", "fk_", COUNT(KICK_P), KICK_P,
    create<&ENG_KICK>, destroy, set, note_on, choke, render, DR32_FAM_FM,
};
const dr32_engine_ops dr32_engine_fm_snare = {
    DR32_ENG_FM_SNARE, "fm_snare", "FM Snare", "fs_", COUNT(SNARE_P), SNARE_P,
    create<&ENG_SNARE>, destroy, set, note_on, choke, render, DR32_FAM_FM,
};
const dr32_engine_ops dr32_engine_fm_metal = {
    DR32_ENG_FM_METAL, "fm_metal", "FM Metal", "fx_", COUNT(METAL_P), METAL_P,
    create<&ENG_METAL>, destroy, set, note_on, choke, render, DR32_FAM_FM,
};
const dr32_engine_ops dr32_engine_fm_perc = {
    DR32_ENG_FM_PERC, "fm_perc", "FM Perc", "fp_", COUNT(PERC_P), PERC_P,
    create<&ENG_PERC>, destroy, set, note_on, choke, render, DR32_FAM_FM,
};

const dr32_model *dr32_fm_models(int *count) {
    if (count) *count = NM;
    return M.m;
}

}
