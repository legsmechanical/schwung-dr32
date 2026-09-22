/*
 * chowkick_engine.cpp — ChowKick as a DR32 engine.
 *
 * ChowKick is Chowdhury DSP's kick synth (BSD-3-Clause,
 * github.com/Chowdhury-DSP/ChowKick @ 4a11869): a trigger pulse, shaped by a
 * wave-digital model of a diode/RC pulse circuit, rung through a nonlinear
 * resonant filter, with envelope-following noise and a tone filter. The
 * plugin is JUCE; this is a line-for-line SCALAR port of its DSP
 * (its src/dsp/ directory), one voice per pad:
 *
 *   Trigger.cpp          the pulse: Width ms at Amp x velocity
 *   PulseShaper.cpp      the WDF circuit — chowdsp_wdf, vendored UNMODIFIED
 *                        under chowkick/chowdsp_wdf/ (BSD-3-Clause)
 *   ResonantFilter.cpp   the nonlinear biquad and its three modes
 *   Noise.cpp            noise gated by the voice's own envelope
 *   OutputFilter.cpp     one-pole Tone, plus the plugin's level makeup
 *   ChowKick.cpp         the 10 Hz DC blocker at the end
 *
 * The helpers those files lean on are REIMPLEMENTED here from their behaviour,
 * not copied: JUCE's SmoothedValue (linear and multiplicative),
 * FastMathApproximations::tan, Random, dsp::Gain and NormalisableRange's
 * centre skew. The SVF (Andy Simper's published equations) and the noise
 * generators (uniform, Box-Muller, Voss pink) follow chowdsp_utils'
 * chowdsp_filters / chowdsp_sources, which are GPLv3 — compatible with DR32's
 * GPL-3.0-or-later, the combination being GPLv3 (NOTICES.md). Each helper
 * says what it reproduces.
 *
 * The plugin runs four voices in SIMD lanes; a pad is one voice, so this is
 * lane 0 in scalar float. The transcendental functions are std:: rather than
 * xsimd's, so the port is not bit-identical to the plugin — tests/
 * test_chowkick.c holds it to the plugin's own render (attack within -40 dB,
 * envelope within 0.5 dB, pitch exact; tools/chowkick_ref/ builds that
 * reference from ChowKick's real sources). Josh chose this over a
 * bit-identical 4-lane xsimd port, which would cost ~4x the maths:
 * "we definitely need it optimized as possible".
 *
 * WHAT DR32 TAKES OVER: the plugin's Level (DR32 Volume does it; the
 * plugin's own makeup gain inside the tone filter stays), its 1-4 voice
 * polyphony (one voice per pad; a retrigger restarts the pulse on the same
 * ringing filter, as the plugin does with Voices = 1), MIDI tuning and MTS
 * (the filter's Link-to-note mode: a pad's TRSP/DETN moves the filter
 * instead), presets and GUI.
 */
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <tuple>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wshadow"
#include "chowkick/chowdsp_wdf/chowdsp_wdf.h"
#pragma GCC diagnostic pop

#include "kit_port.h"
#include "../dr32_engine.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

/* Test hook: nonzero computes the pulse circuit every sample, so
 * tests/test_chowkick.c can prove the hold exact against it. Nothing sets it
 * in the module. */
extern "C" int dr32_chowkick_no_hold;
int dr32_chowkick_no_hold = 0;

namespace {
constexpr float kTwoPi = 2.0f * kPi;

/* ---- helpers reimplemented from their behaviour -------------------------- */

/* JUCE SmoothedValue<float, Linear | Multiplicative>: reset(fs, seconds) sets
 * floor(seconds * fs) steps and snaps to the target; setTarget ramps over
 * that many steps (a zero-length ramp snaps); multiplicative steps by
 * exp((ln|target| - ln|current|) / countdown). Default value 0 (linear) or 1
 * (multiplicative). */
template <bool Mult>
struct Smoothed {
    float cur = Mult ? 1.0f : 0.0f, target = Mult ? 1.0f : 0.0f, step = 0.0f;
    int countdown = 0, steps = 0;
    void reset(float fs, float seconds) { steps = (int) std::floor(seconds * fs); snap(target); }
    void snap(float v) { cur = target = v; countdown = 0; }
    bool smoothing() const { return countdown > 0; }
    void set_target(float v) {
        if (v == target) return;
        if (steps <= 0) { snap(v); return; }
        target = v;
        countdown = steps;
        step = Mult ? std::exp((std::log(std::abs(target)) - std::log(std::abs(cur))) / (float) countdown)
                    : (target - cur) / (float) countdown;
    }
    float next() {
        if (!smoothing()) return target;
        --countdown;
        if (smoothing()) cur = Mult ? cur * step : cur + step;
        else cur = target;
        return cur;
    }
};
using SmoothLin = Smoothed<false>;
using SmoothMul = Smoothed<true>;

/* JUCE FastMathApproximations::tan: a [7/6] Pade approximant. */
inline float fast_tan(float x) {
    const float x2 = x * x;
    const float num = x * (-135135.0f + x2 * (17325.0f + x2 * (-378.0f + x2)));
    const float den = -135135.0f + x2 * (62370.0f + x2 * (-3150.0f + 28.0f * x2));
    return num / den;
}

/* JUCE Random: the 48-bit LCG (x * 0x5DEECE66D + 11), nextInt = seed >> 16,
 * nextFloat = uint32(nextInt) / 2^32, never 1.0. The plugin seeds from the
 * clock; a pad seeds from a constant, so a pad is deterministic. */
struct Rng {
    int64_t seed = 1;
    int next_int() {
        seed = (int64_t) ((((uint64_t) seed) * 0x5deece66dULL + 11) & 0xffffffffffffULL);
        return (int) (seed >> 16);
    }
    float next_float() {
        const float r = (float) (uint32_t) next_int() / ((float) std::numeric_limits<uint32_t>::max() + 1.0f);
        return r == 1.0f ? 1.0f - std::numeric_limits<float>::epsilon() : r;
    }
    /* The plugin's noise is a 4-lane SIMD batch: every draw takes one float
     * per lane, and a pad is lane 0. So the sequence a pad hears is every
     * fourth draw — kept, so the noise's character is the plugin's. */
    float lane0() {
        const float v = next_float();
        next_float(); next_float(); next_float();
        return v;
    }
};

/* chowdsp::StateVariableFilter (Andy Simper's linear trapezoidal SVF):
 * g = tan(pi fc / fs), k = 1/Q, a1 = 1/(1 + g(g + k)), a2 = g a1, a3 = g a2,
 * ak = (g + k) a1. Default fc 1000 Hz, Q 1/sqrt 2. */
struct Svf {
    float fs = 44100.0f, g0 = 0, k0 = 0, a1 = 0, a2 = 0, a3 = 0, ak = 0, s1 = 0, s2 = 0;
    void prepare(float sr) { fs = sr; s1 = s2 = 0.0f; set_q((float) (1.0 / 1.4142135623730951)); set_cutoff(1000.0f); }
    void set_cutoff(float f) { g0 = std::tan(kPi * f / fs); update(); }
    void set_q(float q) { k0 = 1.0f / q; update(); }
    void update() {
        const float gk = g0 + k0;
        a1 = 1.0f / (1.0f + g0 * gk);
        a2 = g0 * a1;
        a3 = g0 * a2;
        ak = gk * a1;
    }
    /* returns {high, band, low} */
    inline void core(float x, float &v0, float &v1, float &v2) {
        const float v3 = x - s2;
        v0 = a1 * v3 - ak * s1;
        v1 = a2 * v3 + a1 * s1;
        v2 = a3 * v3 + a2 * s1 + s2;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
    }
    inline float lowpass(float x) { float h, b, l; core(x, h, b, l); return l; }
    inline float highpass(float x) { float h, b, l; core(x, h, b, l); return h; }
};

/* JUCE NormalisableRange with setSkewForCentre: skew = ln 0.5 / ln((c - s)/(e - s)),
 * convertTo0to1(v) = clamp((v - s)/(e - s))^skew. */
inline float to01_centred(float v, float s, float e, float c) {
    const float skew = std::log(0.5f) / std::log((c - s) / (e - s));
    float p = (v - s) / (e - s);
    p = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    return std::pow(p, skew);
}

/* ---- ResonantFilterProcs.h, verbatim in scalar -------------------------- */

inline float drive(float x, float d) { return std::tanh(x * d) / d; }

struct Coefs { float b[3], a[3]; };

enum { MODE_LINEAR = 0, MODE_BASIC = 1, MODE_BOUNCY = 2 };

inline void drive_values(int mode, float tight, float bounce, float &d1, float &d2, float &d3) {
    switch (mode) {
        case MODE_BASIC:
            d1 = 4.9f * std::pow(tight, 4.0f) + 0.1f;
            d2 = 4.9f * std::pow(tight, 6.0f) + 0.1f;
            d3 = 4.75f * std::pow(bounce, 3.0f) + 0.25f;
            return;
        case MODE_BOUNCY: {
            d1 = 4.9f * std::pow(tight, 4.0f) + 0.1f;
            const float bounceScale = 0.7f * tight + 0.3f;
            d3 = std::pow(bounceScale * bounce, 2.0f) + 0.1f;
            d2 = 0.4f * std::pow(tight, 0.8f) + 0.4f * std::pow(1.0f - bounce, 0.8f) + 0.1f;
            return;
        }
        default:
            d1 = d2 = d3 = 1.0f;
            return;
    }
}

inline float res_proc(int mode, float x, const Coefs &c, float (&z)[3], float d1, float d2, float d3) {
    switch (mode) {
        case MODE_LINEAR: {
            const float y = (z[1] + x * c.b[0]) * 0.999999f;
            z[1] = z[2] + x * c.b[1] - y * c.a[1];
            z[2] = x * c.b[2] - y * c.a[2];
            return y * 0.15f;
        }
        case MODE_BASIC: {
            const float y = z[1] + x * c.b[0];
            const float yDrive = drive(y, d3);
            z[1] = drive(z[2] + x * c.b[1] - yDrive * c.a[1], d1);
            z[2] = drive(x * c.b[2] - yDrive * c.a[2], d2);
            return y;
        }
        default: {                                  /* Bouncy */
            const float y = z[1] + x * c.b[0];
            const float yDrive = drive(y, d3);
            z[1] = drive(z[2] + x * c.b[1] - yDrive * c.a[1], d1);
            z[2] = drive(x * c.b[2] - y * c.a[2], d1);
            return y * d2;
        }
    }
}

/* ---- the parameter table ------------------------------------------------ */

/* ⚠ ORDER IS THE PAGE ORDER. The state is saved by KEY, so reordering is
 * safe for saved pads. Width, Amp, Decay, Sustain is ChowKick's own panel
 * order (res/gui.xml; Josh: "sustain and decay knobs should be reversed").
 * Display units; percents are 0..100 here and 0..1 in the plugin. The
 * defaults are the plugin's own (its addParameters). */
enum P {
    P_WIDTH, P_AMP, P_DECAY, P_SUSTAIN, P_VELO, P_TONE,
    P_FREQ, P_Q, P_DAMP, P_TIGHT, P_BOUNCE, P_MODE, P_PORTA,
    P_NOISE, P_NDECAY, P_NCUT, P_NTYPE,
    NP
};
const dr32_eparam PARAMS[NP] = {
    {"ck_width",   "Width",       "WIDTH", 0.025f, 2.5f,   1.0f,  0.025f, "ms", "Pulse", nullptr},
    {"ck_amp",     "Amp",         "AMP",   0.0f,   100.0f, 100.0f, 1.0f,  "%",  "Pulse", nullptr},
    {"ck_decay",   "Decay",       "DECAY", 0.0f,   100.0f, 50.0f, 1.0f,   "%",  "Pulse", nullptr},
    {"ck_sustain", "Sustain",     "SUST",  0.0f,   100.0f, 50.0f, 1.0f,   "%",  "Pulse", nullptr},
    {"ck_velo",    "Vel Sense",   "VSENS", 0.0f,   1.0f,   1.0f,  1.0f,   nullptr, "Pulse", "Off|On"},
    {"ck_tone",    "Tone",        "TONE",  300.0f, 7000.0f, 800.0f, 10.0f, "hz", "Pulse", nullptr},
    {"ck_freq",    "Frequency",   "FREQ",  30.0f,  500.0f, 100.0f, 1.0f,  "hz", "Body",  nullptr},
    {"ck_q",       "Q",           "Q",     0.1f,   2.0f,   0.5f,  0.01f,  nullptr, "Body", nullptr},
    {"ck_damp",    "Damping",     "DAMP",  0.0f,   100.0f, 50.0f, 1.0f,   "%",  "Body",  nullptr},
    {"ck_tight",   "Tight",       "TIGHT", 0.0f,   100.0f, 50.0f, 1.0f,   "%",  "Body",  nullptr},
    {"ck_bounce",  "Bounce",      "BOUNC", 0.0f,   100.0f, 0.0f,  1.0f,   "%",  "Body",  nullptr},
    {"ck_mode",    "Mode",        "MODE",  0.0f,   2.0f,   1.0f,  1.0f,   nullptr, "Body", "Linear|Basic|Bouncy"},
    {"ck_porta",   "Portamento",  "PORTA", 0.1f,   200.0f, 50.0f, 0.1f,   "ms", "Body",  nullptr},
    {"ck_noise",   "Noise",       "NOISE", 0.0f,   100.0f, 0.0f,  1.0f,   "%",  "Noise", nullptr},
    {"ck_ndecay",  "Noise Decay", "NDCY",  0.0f,   100.0f, 50.0f, 1.0f,   "%",  "Noise", nullptr},
    {"ck_ncut",    "Noise Cutoff","NCUT",  20.0f,  20000.0f, 2000.0f, 10.0f, "hz", "Noise", nullptr},
    {"ck_ntype",   "Noise Type",  "NTYPE", 0.0f,   2.0f,   0.0f,  1.0f,   nullptr, "Noise", "Uniform|Normal|Pink"},
};

/* ---- the voice ---------------------------------------------------------- */

using v_type = float;
namespace wdft = chowdsp::wdft;

/* PulseShaper.h: a resistive voltage source into C40 || R163, in parallel
 * with an inverted R162, terminated by a 1N4148 (Is 2.52 nA). R162 and R163
 * are the Decay and Sustain knobs. */
struct Shaper {
    wdft::ResistiveVoltageSourceT<v_type> Vs;
    wdft::ResistorT<v_type> r162 { 4700.0f };
    wdft::ResistorT<v_type> r163 { 100000.0f };
    wdft::CapacitorAlphaT<v_type> c40;
    wdft::WDFParallelT<v_type, decltype(c40), decltype(r163)> P1 { c40, r163 };
    wdft::WDFSeriesT<v_type, decltype(Vs), decltype(P1)> S1 { Vs, P1 };
    wdft::PolarityInverterT<v_type, decltype(r162)> I1 { r162 };
    wdft::WDFParallelT<v_type, decltype(I1), decltype(S1)> P2 { I1, S1 };
    wdft::DiodeT<v_type, decltype(P2)> d53 { P2, 2.52e-9f };

    explicit Shaper(float fs) : c40(0.015e-6f, fs, 0.029f) {}

    inline float process(float x) {
        Vs.setVoltage(x);
        d53.incident(P2.reflected());
        const float y = wdft::voltage<v_type>(r162);
        P2.incident(d53.reflected());
        return y;
    }
};

/* Pink noise, chowdsp's Voss generator (QUALITY 8), on lane 0. */
struct Pink {
    int frame = -1;
    float values[8] = {};
    float next(Rng &r) {
        const int last = frame;
        frame++;
        if (frame >= (1 << 8)) frame = 0;
        const int diff = last ^ frame;
        float sum = 0.0f;
        for (int i = 0; i < 8; i++) {
            if (diff & (1 << i)) values[i] = r.lane0() - 0.5f;
            sum += values[i];
        }
        return sum * (1.0f / 8.0f);
    }
};

struct Voice {
    float fs;
    float p[NP];
    float freq_mult = 1.0f;
    int   fresh = 1;          /* not yet rendered: knob writes SNAP, as a preset
                               * loaded before prepareToPlay does in the plugin */
    kp_gate gate;

    /* Trigger */
    int   pending = 0;
    float pending_vel = 0.0f;
    int   leftover = 0;
    float vel_amp = 1.0f;

    Shaper *shaper;
    int     shaper_idle = 0;  /* at its fixed point with no input: skipped */
    float   shaper_hold = 0.0f; /* the value it sits at there                */

    /* ResonantFilter */
    SmoothMul freq_s, q_s, g_s, d1_s, d2_s, d3_s;
    Coefs c { {1, 0, 0}, {1, 0, 0} };
    float z[3] = {0, 0, 0};
    float prev_porta = 0.0f;

    /* Noise */
    Rng rng;
    Pink pink;
    SmoothLin noise_gain;     /* juce::dsp::Gain: linear, 50 ms, from 0 */
    SmoothMul ndecay_s;       /* from 1, 50 ms */
    Svf noise_lp;

    /* OutputFilter */
    SmoothMul tone_s, gain_s;
    float oa1 = 0.0f, ob0 = 1.0f, ob1 = 0.0f, oz1 = 0.0f;

    Svf dc;                    /* ChowKick.cpp: highpass at 10 Hz */

    float buf[256];
    float nbuf[256];

    float pct(int i) const { return p[i] * 0.01f; }

    float res_freq_hz() const { return freq_mult * p[P_FREQ]; }
    float res_g() const {
        const float lowDamp = 0.0001f, highDamp = 0.5f;
        return lowDamp * std::pow(highDamp / lowDamp, pct(P_DAMP));
    }
    void calc_res(float freq, float Q, float G) {
        const float wc = freq * kTwoPi / fs;
        const float wS = std::sin(wc), wC = std::cos(wc);
        const float alpha = wS / (2.0f * Q);
        const float a0 = (G + 1.0f) + alpha * G;
        c.b[0] = (alpha + 1.0f) / a0;
        c.b[1] = wC * -2.0f / a0;
        c.b[2] = (1.0f - alpha) / a0;
        c.a[0] = 1.0f;
        c.a[1] = wC * -2.0f * (G + 1.0f) / a0;
        c.a[2] = ((G + 1.0f) - alpha * G) / a0;
    }
    float out_gain() const {
        const float toneMakeupDB = (to01_centred(p[P_TONE], 300.0f, 7000.0f, 800.0f) - 0.5f) * -6.0f;
        const float bounceMakeupDB = 14.0f * std::pow(pct(P_BOUNCE), 2.5f);
        const float db = 0.0f /* Level: DR32's Volume */ + bounceMakeupDB + toneMakeupDB + 3.5f;
        return db > -100.0f ? std::pow(10.0f, db * 0.05f) : 0.0f;
    }
    void calc_out(float freq, float gain) {
        const float wc = kTwoPi * freq / fs;
        const float cc = 1.0f / fast_tan(wc / 2.0f);
        const float a0 = cc + 1.0f;
        ob0 = gain / a0;
        ob1 = ob0;
        oa1 = (1.0f - cc) / a0;
    }

    /* ResonantFilter::reset + OutputFilter::reset: everything smoothed jumps
     * to the knobs, and the coefficients follow. */
    void snap() {
        prev_porta = p[P_PORTA];
        freq_s.reset(fs, prev_porta * 0.001f);
        freq_s.snap(res_freq_hz());
        q_s.snap(p[P_Q]);
        g_s.snap(res_g());
        float d1, d2, d3;
        drive_values((int) p[P_MODE], pct(P_TIGHT), pct(P_BOUNCE), d1, d2, d3);
        d1_s.snap(d1); d2_s.snap(d2); d3_s.snap(d3);
        calc_res(freq_s.target, q_s.target, g_s.target);
        tone_s.snap(p[P_TONE]);
        gain_s.snap(out_gain());
        calc_out(tone_s.target, gain_s.target);
    }

    Voice(float sr) : fs(sr) {
        for (int i = 0; i < NP; i++) p[i] = PARAMS[i].def;
        shaper = new (std::nothrow) Shaper(fs);
        q_s.reset(fs, 0.05f); g_s.reset(fs, 0.05f);
        d1_s.reset(fs, 0.05f); d2_s.reset(fs, 0.05f); d3_s.reset(fs, 0.05f);
        tone_s.reset(fs, 0.05f); gain_s.reset(fs, 0.05f);
        ndecay_s.reset(fs, 0.05f);
        noise_gain.reset(fs, 0.05f);
        noise_lp.prepare(fs);
        dc.prepare(fs);
        dc.set_cutoff(10.0f);
        rng.seed = 0x6368'6f77LL;   /* "chow": any constant, so pads are repeatable */
        kp_gate_init(&gate, (int) sr);
        snap();
    }
    ~Voice() { delete shaper; }

    void block(float *out, int n) {
        /* ---- Trigger::processBlock ---- */
        const int pulse = (int) (fs * (p[P_WIDTH] / 1000.0f));
        const float amp = pct(P_AMP);
        int fill = leftover < n ? leftover : n;
        for (int i = 0; i < n; i++) buf[i] = i < fill ? amp * vel_amp : 0.0f;
        leftover -= fill;
        if (pending) {
            pending = 0;
            vel_amp = p[P_VELO] >= 0.5f ? 0.1f + 1.8f * pending_vel : 1.0f;
            fill = pulse < n ? pulse : n;
            for (int i = 0; i < fill; i++) buf[i] = amp * vel_amp;
            leftover = pulse - fill;
        }

        /* ---- PulseShaper::processBlock ----
         * ⭑ THE ONE OPTIMISATION, and it is where the time goes: the diode
         * circuit is 60-70% of a sounding pad (7.4 of ~11 us per block on an
         * M-series Mac), but it only SHAPES THE PULSE. Between hits its input
         * is exactly 0, and within milliseconds it settles — not to 0 but to a
         * FIXED POINT of float arithmetic (2e-8 on Tonal, 2e-10 on Default),
         * where every sample it computes is the same value. The resonator after
         * it can ring for 30 s on that. So once a whole block of zero input
         * comes out as one repeated value, the circuit is skipped and that value
         * HELD until the next pulse: what it would have computed anyway, so the
         * output is unchanged bit for bit (tests/test_chowkick.c renders with
         * the hold and without it and requires them identical). */
        {
            int any_in = 0;
            for (int i = 0; i < n && !any_in; i++) any_in = buf[i] != 0.0f;
            if (any_in) shaper_idle = 0;
            if (shaper_idle && !dr32_chowkick_no_hold) {
                for (int i = 0; i < n; i++) buf[i] = shaper_hold;
            } else {
                const float r1Off = 5000.0f, r1Scale = 500000.0f;
                const float sustainVal = 1.0f - std::pow(pct(P_SUSTAIN), 0.05f);
                shaper->r163.setResistanceValue(r1Off + sustainVal * (r1Scale - r1Off));
                const float r2Off = 500.0f, r2Scale = 100000.0f;
                const float decayVal = std::pow(pct(P_DECAY), 2.0f);
                shaper->r162.setResistanceValue(r2Off + decayVal * (r2Scale - r2Off));
                for (int i = 0; i < n; i++) buf[i] = shaper->process(buf[i]);
                if (!any_in) {
                    int same = 1;
                    for (int i = 1; i < n && same; i++) same = buf[i] == buf[0];
                    if (same) { shaper_idle = 1; shaper_hold = buf[0]; }
                }
            }
        }

        /* ---- ResonantFilter::processBlock ---- */
        {
            if (p[P_PORTA] != prev_porta) {
                prev_porta = p[P_PORTA];
                freq_s.reset(fs, prev_porta * 0.001f);
            }
            freq_s.set_target(res_freq_hz());
            q_s.set_target(p[P_Q]);
            g_s.set_target(res_g());
            const int mode = (int) p[P_MODE];
            float d1, d2, d3;
            drive_values(mode, pct(P_TIGHT), pct(P_BOUNCE), d1, d2, d3);
            d1_s.set_target(d1); d2_s.set_target(d2); d3_s.set_target(d3);
            if (freq_s.smoothing() || q_s.smoothing() || g_s.smoothing()) {
                for (int i = 0; i < n; i++) {
                    /* argument order as the plugin evaluates it */
                    const float f = freq_s.next(), q = q_s.next(), g = g_s.next();
                    calc_res(f, q, g);
                    const float e1 = d1_s.next(), e2 = d2_s.next(), e3 = d3_s.next();
                    buf[i] = res_proc(mode, buf[i], c, z, e1, e2, e3);
                }
            } else if (d1_s.smoothing() || d2_s.smoothing() || d3_s.smoothing()) {
                for (int i = 0; i < n; i++) {
                    const float e1 = d1_s.next(), e2 = d2_s.next(), e3 = d3_s.next();
                    buf[i] = res_proc(mode, buf[i], c, z, e1, e2, e3);
                }
            } else {
                for (int i = 0; i < n; i++) buf[i] = res_proc(mode, buf[i], c, z, d1, d2, d3);
            }
        }

        /* ---- Noise::processBlock ---- */
        {
            const float amt = pct(P_NOISE);
            noise_gain.set_target(std::pow(amt, 2.0f));
            ndecay_s.set_target(std::pow(1.0f - pct(P_NDECAY), 2.5f) * 2.0f + 1.0f);
            /* Nothing to add while the gain is, and stays, exactly 0: the
             * plugin would add pow(|x|, d) * 0 = 0 here. The RNG then does
             * not advance, which only moves where the (unseeded, in the
             * plugin) noise sequence resumes. */
            if (noise_gain.smoothing() || noise_gain.target != 0.0f) {
                const int type = (int) p[P_NTYPE];
                for (int i = 0; i < n; i++) {
                    float r;
                    if (type == 0) {
                        r = 2.0f * rng.lane0() - 1.0f;
                    } else if (type == 1) {
                        const float u1 = rng.lane0(), u2 = rng.lane0();
                        const float radius = std::sqrt(-2.0f * std::log(1.0f - u1));
                        const float theta = kTwoPi * u2;
                        r = radius * std::sin(theta) / std::sqrt(2.0f);
                    } else {
                        r = pink.next(rng);
                    }
                    nbuf[i] = r * noise_gain.next();
                }
                noise_lp.set_cutoff(p[P_NCUT]);
                for (int i = 0; i < n; i++) nbuf[i] = noise_lp.lowpass(nbuf[i]);
                for (int i = 0; i < n; i++) {
                    const float gainN = std::pow(std::abs(buf[i]), ndecay_s.next());
                    buf[i] += gainN * nbuf[i];
                }
            } else {
                for (int i = 0; i < n; i++) ndecay_s.next();
            }
        }

        /* ---- OutputFilter::processBlock ---- */
        {
            tone_s.set_target(p[P_TONE]);
            gain_s.set_target(out_gain());
            if (tone_s.smoothing() || gain_s.smoothing()) {
                for (int i = 0; i < n; i++) {
                    const float f = tone_s.next(), g = gain_s.next();
                    calc_out(f, g);
                    const float y = oz1 + buf[i] * ob0;
                    oz1 = buf[i] * ob1 - y * oa1;
                    buf[i] = y;
                }
            } else {
                for (int i = 0; i < n; i++) {
                    const float y = oz1 + buf[i] * ob0;
                    oz1 = buf[i] * ob1 - y * oa1;
                    buf[i] = y;
                }
            }
        }

        /* ---- the DC blocker ---- */
        for (int i = 0; i < n; i++) {
            float s = dc.highpass(buf[i]);
            if (!(s > -8.0f && s < 8.0f)) s = 0.0f;
            out[i] = s;
            kp_gate_frame(&gate, s);
        }
    }
};

void *create(int sr) {
    Voice *v = new (std::nothrow) Voice((float) sr);
    if (v && !v->shaper) { delete v; return nullptr; }
    return v;
}

void destroy(void *e) { delete static_cast<Voice *>(e); }

void set(void *e, int idx, float display) {
    Voice *v = static_cast<Voice *>(e);
    if (!v || idx < 0 || idx >= NP) return;
    const dr32_eparam &ep = PARAMS[idx];
    v->p[idx] = display < ep.min ? ep.min : (display > ep.max ? ep.max : display);
    if (v->fresh) v->snap();
}

void note_on(void *e, float vel01, float tune_st) {
    Voice *v = static_cast<Voice *>(e);
    if (!v) return;
    /* The pad's tune moves the filter: ResonantFilter::setFreqMult, which is
     * there for exactly this, and which glides over Portamento like any
     * other move of the filter's frequency. */
    v->freq_mult = std::pow(2.0f, tune_st / 12.0f);
    if (v->fresh) v->snap();
    v->pending = 1;
    v->pending_vel = vel01 < 0.0f ? 0.0f : (vel01 > 1.0f ? 1.0f : vel01);
    kp_gate_hit(&v->gate);
}

/* ChowKick has no choke; DR32's own ramp is the choke, and the gate stops the
 * voice once it is quiet. */
void choke(void *e) { (void) e; }

int render(void *e, float *out, int n) {
    Voice *v = static_cast<Voice *>(e);
    if (!v || !v->gate.active) {
        std::memset(out, 0, sizeof(float) * (size_t) n);
        return 0;
    }
    v->fresh = 0;
    for (int at = 0; at < n; at += 256) {
        const int m = n - at < 256 ? n - at : 256;
        v->block(out + at, m);
    }
    return kp_gate_end(&v->gate);
}

/* ---- models: the plugin's five factory presets --------------------------- */

struct Preset {
    const char *slug, *name;
    float width, amp, sustain, decay, tone, freq, q, damp, tight, bounce, mode, porta;
};
/* The plugin's res/presets/ files, their values as saved (percents x100 here). Each
 * preset leaves the rest at the plugin's defaults: no noise, Vel Sense on.
 * Wonky Synth's filter is LINKED to the played note in the plugin; here it
 * sits at its saved 80 Hz and TRSP plays it. Its Voices = 4 is polyphony,
 * which a pad does not have. */
const Preset PRESETS[] = {
    {"chowkick/default", "Default",     1.0f,        100, 50, 100,         800, 80.0f,        0.5f,  50,    50, 0,    1, 0.5f},
    {"chowkick/tight",   "Tight",       2.5f,        100, 5.0000001f, 25,  330, 60.000004f,   1.4f,  75,    50, 0,    2, 0.5f},
    {"chowkick/tonal",   "Tonal",       0.99999994f, 100, 0,  80.000001f,  800, 80.0f,        1.0f,  30.000001f, 50, 0, 0, 0.5f},
    {"chowkick/bouncy",  "Bouncy",      1.4999999f,  100, 50, 100,         450, 80.0f,        1.0f,  55.000001f, 20.000001f, 60.000002f, 2, 0.5f},
    {"chowkick/wonky",   "Wonky Synth", 0.25f,       100, 60, 80,          800, 80.0f,        1.41f, 14.5f, 0,  36,   1, 50.0f},
};
const int NM = (int) (sizeof(PRESETS) / sizeof(PRESETS[0]));

struct Models {
    float values[NM][DR32_ENG_MAX_PARAMS];
    dr32_model m[NM];
    Models() {
        for (int i = 0; i < NM; i++) {
            const Preset &s = PRESETS[i];
            float *v = values[i];
            for (int k = 0; k < NP; k++) v[k] = PARAMS[k].def;
            v[P_WIDTH] = s.width;   v[P_AMP] = s.amp;       v[P_SUSTAIN] = s.sustain;
            v[P_DECAY] = s.decay;   v[P_TONE] = s.tone;     v[P_FREQ] = s.freq;
            v[P_Q] = s.q;           v[P_DAMP] = s.damp;     v[P_TIGHT] = s.tight;
            v[P_BOUNCE] = s.bounce; v[P_MODE] = s.mode;     v[P_PORTA] = s.porta;
            m[i] = dr32_model{s.slug, s.name, DR32_ENG_CHOWKICK, v, 0.0f, 0.0f};
        }
    }
};
const Models M;

}  // namespace

extern "C" {

const dr32_engine_ops dr32_engine_chowkick = {
    DR32_ENG_CHOWKICK, "chowkick", "ChowKick", "ck_", NP, PARAMS,
    create, destroy, set, note_on, choke, render, DR32_FAM_CHOWKICK,
};

const dr32_model *dr32_chowkick_models(int *count) {
    if (count) *count = NM;
    return M.m;
}

}
