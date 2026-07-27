// dr32_fxbus.cpp — implementation of DR32's send/insert buses.
//
// C++ because the vendored reverbs are C++ structs (see dsp/vendor/SOURCES.md);
// the interface is extern "C" so the rest of the C11 engine is unaffected.

#include "dr32_fxbus.h"

#include "vendor/airwin_spaces.h"
#include "vendor/airwin_dyn.h"
#include "vendor/space_extra.h"

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <new>

#define DR32_SEND_SLOTS   2
#define DR32_INSERT_SLOTS 2
#define DR32_MAX_BLOCK    1024

namespace {

/** Drum Buss — a drum-bus glue insert in the spirit of Ableton's Drum Buss.
 *
 *    Compress   — Airwindows *Pop3*'s compressor section, driven by ONE knob
 *                 that sweeps a coordinated threshold + ratio + release
 *                 program, followed by our own makeup gain.
 *    Crunch     — full-band saturation: tanh soft knee, a cubic term for grit,
 *                 and a deliberate asymmetry so it produces EVEN harmonics too.
 *    Attack     — Airwindows *Point*, fast reaction: bipolar transient shaping.
 *    Sustain    — dual-release envelope detector shaping the decay only.
 *
 *  Attack and Sustain are orthogonal by measurement, not by intent: sweeping
 *  Sustain end to end moves the tail -8.0 to +11.9 dB while the attack stays at
 *  0.00 dB, and sweeping Attack moves the hit -1.9 to +8.5 dB with the tail
 *  moving the other way.
 *
 *  ── why not what was here before ────────────────────────────────────────
 *  The previous Compress was Airwindows Pressure4, a vari-mu LEVELLER. It was
 *  measured against a drum groove and failed on every count that matters here:
 *
 *    - no threshold at all. Its curve was ~2:1 across the WHOLE range, so a
 *      -48 dBFS signal came out at -30 dBFS: +17.7 dB of lift applied to
 *      sample noise floor, room bleed and reverb tails.
 *    - half the knob was dead — comp=0.50 produced 0.3 dB of gain reduction,
 *      and even at 1.00 only 4.8 dB.
 *    - ~20 ms to reach full gain reduction, so it never caught a kick
 *      (measured kick GR at full knob: 2.0 dB).
 *    - backwards on a groove: a hat landing after a kick came out 17.8 dB
 *      quieter than an isolated hat. It buried the hats instead of holding
 *      the kick.
 *
 *  Pop3 fixes this structurally, not by tuning. Its gain is
 *  (1-ratio) + (popComp*ratio) with popComp clamped to [0,1], so it can only
 *  ever ATTENUATE — measured 0.00 dB of lift on a -48 dBFS sine at every
 *  setting. Makeup is therefore ours to apply deliberately.
 *
 *  The old Transients stage was a single broadband gain from the ratio of two
 *  envelope followers, so the tail tracked the attack instead of opposing it:
 *  at knob 0 it took the attack down 18.1 dB and the tail down 3.0 dB. There
 *  was no sustain control anywhere on it, despite the comment claiming one.
 *  Point returns to exactly unity in steady state and moves the tail OPPOSITE
 *  the attack, which is what a transient designer is supposed to do.
 */
struct DrumBuss {
    float fs = 44100.0f;

    awk_pop3::Pop3   comp;      // compressor (its gate section is left off)
    awk_point::Point atk;       // Point, fast reaction — attack shaping

    /** Sustain section.
     *
     *  Point is the right machine for ATTACK but the wrong one for sustain: it
     *  detects "how much louder is now than the local average", and slowing it
     *  down enough to straddle a decay just makes both followers average the
     *  whole hit, so it becomes a broadband level control. Measured at a 104 ms
     *  reaction it moved attack +15.8 dB and tail +13.4 dB TOGETHER — the same
     *  failure as the stage this replaces.
     *
     *  A sustain control needs a detector that is quiet at the onset and grows
     *  through the decay. Two followers sharing an attack but with different
     *  RELEASE times do exactly that: they track together while the signal
     *  rises, then the long-release one sits above the short-release one for
     *  the whole tail. Their ratio is 1 at the hit and climbs afterwards, so
     *  the gain lands on the decay and leaves the transient alone. */
    struct Sustain {
        float aAtt = 0.0f, rFast = 0.0f, rSlow = 0.0f;
        float envF[2] = { 0.0f, 0.0f }, envS[2] = { 0.0f, 0.0f };
        float depth = 0.0f;
        void setSampleRate(float fs) {
            aAtt  = 1.0f - expf(-1.0f / (0.001f * fs));   // 1 ms, shared
            rFast = 1.0f - expf(-1.0f / (0.050f * fs));   // 50 ms
            rSlow = 1.0f - expf(-1.0f / (0.400f * fs));   // 400 ms
        }
        void reset() { envF[0] = envF[1] = envS[0] = envS[1] = 0.0f; }
        inline void run(float *l, float *r, int n) {
            float *ch[2] = { l, r };
            for (int c = 0; c < 2; c++) {
                for (int i = 0; i < n; i++) {
                    float m = fabsf(ch[c][i]);
                    envF[c] += (m > envF[c] ? aAtt : rFast) * (m - envF[c]);
                    envS[c] += (m > envS[c] ? aAtt : rSlow) * (m - envS[c]);
                    // 0 at the onset and in steady state, positive through decay
                    float t = (envS[c] - envF[c]) / (envF[c] + 1e-5f);
                    if (t < 0.0f) t = 0.0f;
                    if (t > 3.0f) t = 3.0f;
                    // exp2f, not powf(2,x): this runs per sample.
                    float g = exp2f(depth * t);
                    if (g < 0.06f) g = 0.06f;
                    if (g > 8.0f)  g = 8.0f;
                    ch[c][i] *= g;
                }
            }
        }
    } sus;

    float crunch = 0.0f;
    float makeup = 1.0f;
    float crunchNorm = 1.0f;
    bool  compOn = false, atkOn = false, susOn = false;
    // DC blocker state — the asymmetric term in the saturator produces an
    // offset by construction, and an offset on a drum bus eats headroom and
    // makes the compressor below it behave differently on each polarity.
    float dcX[2] = { 0.0f, 0.0f }, dcY[2] = { 0.0f, 0.0f };

    void setSampleRate(float sr) {
        fs = (sr > 1.0f) ? sr : 44100.0f;
        comp.setSampleRate(fs);
        atk.setSampleRate(fs);
        sus.setSampleRate(fs);   // Sustain::setSampleRate, not Point's
    }

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    /** p1 compress, p2 crunch, p3 attack (0.5 neutral), p4 sustain (0.5 neutral). */
    void setParams(float p1, float p2, float p3, float p4) {
        const float c = clamp01(p1);
        crunch = clamp01(p2);
        compOn = c > 0.001f;

        // --- one knob, a coordinated program ---------------------------------
        // Threshold -6 dBFS down to -30 dBFS, ratio 0.30 -> 1.00 (Pop3's ratio
        // is a blend from 1:1 to limiting), release 100 -> 180 ms. Attack is
        // pinned near Pop3's fastest (~11 ms): slow enough to let the beater
        // click through, fast enough to catch the body. That combination is
        // what produced +14.5 dB of click-to-body on the bench.
        const float thrDb  = -6.0f - 24.0f * c;
        const float thrLin = powf(10.0f, thrDb / 20.0f);
        comp.A = powf(thrLin, 0.25f);                 // compThresh = A^4
        const float ratio  = 0.30f + 0.70f * c;
        comp.B = 1.0f - sqrtf(1.0f - ratio);          // compRatio = 1-(1-B)^2
        comp.C = 0.10f;                               // ~11.5 ms attack
        comp.D = 0.60f + 0.10f * c;                   // ~100..180 ms release
        comp.E = 0.0f;  comp.F = 0.0f;                // gate section off
        comp.G = 0.5f;  comp.H = 0.5f;

        // Makeup is measured, not predicted — see makeupTrack() below. A static
        // analytic makeup was tried first and was badly wrong: computing the
        // gain the compressor would apply to a full-scale steady peak gave
        // +21 dB at the top of the knob, because an 11 ms attack never reaches
        // that steady state on a drum hit. It re-created the exact +17.7 dB
        // low-level lift that Pop3 was brought in to eliminate.

        // --- transient shaping ------------------------------------------------
        // Point's B is bipolar around 0.5; C sets the detector timescale via
        // nibDiv = 1/(C+0.2)^7, so LOWER C is SLOWER (C=0.10 is a ~104 ms
        // follower, C=0.62 only ~0.1 ms). Getting that backwards once made the
        // "sustain" control shape the attack instead of the tail.
        //
        // Its taper also explodes at the very top — B=1 divides by 1.001-1.0
        // and measured +53 dB of attack — so the range is restricted to the
        // band where the response stays monotonic.
        atkOn = fabsf(p3 - 0.5f) > 0.005f;
        susOn = fabsf(p4 - 0.5f) > 0.005f;
        atk.A = 0.5f;                                  // unity input trim
        atk.B = 0.5f + clampBi(p3) * 0.34f;            // 0.16 .. 0.84
        atk.C = 0.30f;                                 // ~3 ms: shapes the hit
        // Sustain depth is signed: up lengthens the tail, down shortens it.
        sus.depth = clampBi(p4) * 1.15f;

        recalcCrunch();
    }

    static float clampBi(float v) {                    // 0..1 -> -1..+1
        float b = (v - 0.5f) * 2.0f;
        return b < -1.0f ? -1.0f : (b > 1.0f ? 1.0f : b);
    }

    /** The saturation curve, before makeup.
     *
     *  tanh alone is an ODD function, so the old Crunch measured H2 at -123 dB
     *  — no even harmonics at any drive, which is why it read as fuzz rather
     *  than warmth. The `asym` term is a squared component and is what puts
     *  energy into H2; it costs a DC offset, removed downstream. */
    inline float shape(float x) const {
        const float d  = x * (1.0f + crunch * 24.0f);
        const float sh = tanhf(d);
        const float asym = 0.14f * crunch;
        return sh - 0.15f * sh * sh * sh + asym * sh * sh;
    }

    void recalcCrunch() {
        // Output normalisation, MEASURED over a reference sine rather than
        // assumed: a fixed reference point got this wrong by 8 dB once already.
        const int   K = 64;
        const float amp = 0.3f;
        double in2 = 0.0, out2 = 0.0;
        for (int i = 0; i < K; i++) {
            float x = amp * sinf(2.0f * 3.14159265f * (float)i / (float)K);
            float y = shape(x);
            in2 += (double)x * x;
            out2 += (double)y * y;
        }
        crunchNorm = (out2 > 1e-12) ? (float)std::sqrt(in2 / out2) : 1.0f;
    }

    void reset() {
        comp.reset(); atk.reset(); sus.reset();
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
        makeup = 1.0f;
    }

    /** Auto makeup, MEASURED per block rather than predicted.
     *
     *  Pop3 only ever attenuates, so something has to put the level back or
     *  the Compress knob is just a volume control that goes down. Comparing
     *  the block's RMS either side of the compressor gives the gain reduction
     *  actually applied to actual program material — which for an 11 ms attack
     *  is far less than the steady-state maths predicts.
     *
     *  It is smoothed over ~300 ms so it behaves like makeup and not like a
     *  second, faster compressor, gated on real signal so silence cannot make
     *  it drift upward, and capped: without a cap, a bus fed something already
     *  limited would wind the makeup up indefinitely. */
    void makeupTrack(const float *a, const float *b, int n, double inSq) {
        double outSq = 0.0;
        for (int i = 0; i < n; i++) outSq += (double)a[i] * a[i] + (double)b[i] * b[i];
        // -60 dBFS worth of block energy: below this there is nothing to match.
        if (inSq < 1e-6 * (double)n || outSq <= 1e-20) return;
        float target = (float)std::sqrt(inSq / outSq);
        if (target < 1.0f) target = 1.0f;              // never attenuate further
        if (target > 5.6f) target = 5.6f;              // +15 dB ceiling
        const float alpha = 1.0f - expf(-(float)n / (0.30f * fs));
        makeup += (target - makeup) * alpha;
    }

    /** Block processing: Point and Pop3 both hoist a pile of pow() calls to the
     *  top of their loop, so running them one sample at a time would pay that
     *  cost 44100 times a second. */
    void processBlock(float *io, int n, float *sl, float *sr) {
        for (int i = 0; i < n; i++) { sl[i] = io[2 * i]; sr[i] = io[2 * i + 1]; }
        float *in[2] = { sl, sr }, *out[2] = { sl, sr };

        if (atkOn) atk.processReplacing(in, out, n);
        if (susOn) sus.run(sl, sr, n);

        if (crunch > 0.0f) {
            // Full-band. An earlier version split at ~1.2 kHz and folded only
            // the top, which audibly thinned the kick — that is EQ, not
            // saturation. Verified spectrum-neutral: 60 Hz and 6 kHz tones come
            // out with the same balance they went in with.
            const float R = 1.0f - 2.0f * 3.14159265f * 10.0f / fs;   // ~10 Hz DC block
            for (int c = 0; c < 2; c++) {
                float *ch = c ? sr : sl;
                for (int i = 0; i < n; i++) {
                    float x = ch[i];
                    float y = x + crunch * (shape(x) * crunchNorm - x);
                    float o = y - dcX[c] + R * dcY[c];
                    dcX[c] = y; dcY[c] = o;
                    ch[i] = o;
                }
            }
        }

        if (compOn) {
            double inSq = 0.0;
            for (int i = 0; i < n; i++) inSq += (double)sl[i] * sl[i] + (double)sr[i] * sr[i];
            comp.processReplacing(in, out, n);
            makeupTrack(sl, sr, n, inSq);
            for (int i = 0; i < n; i++) { sl[i] *= makeup; sr[i] *= makeup; }
        }

        for (int i = 0; i < n; i++) { io[2 * i] = sl[i]; io[2 * i + 1] = sr[i]; }
    }
};

using efx::SpaceExtra;

/** Input diffusion for the plate.
 *
 *  Measured against the Airwindows plates at a matched 1.20 s RT60, our tank's
 *  first 30 ms had a crest factor of 19.4 dB against their 12.0–14.5 — i.e. a
 *  handful of discrete echoes where they had a wash. Sparse-and-bright is
 *  exactly the recipe that reads as "metallic", and our tank also measured
 *  +7.3 dB of HF tilt.
 *
 *  Four cascaded allpasses per channel with mutually-prime delays smear the
 *  input into the tank without colouring it (an allpass is flat by
 *  construction). The two channels use different lengths so the plate keeps
 *  the decorrelation it already had. */
struct Diffuser {
    // Prime lengths, Dattorro-ish proportions, at 44.1 kHz. Sized for 48 kHz.
    // Filled by setSampleRate().
    static const int kMax = 512;
    int   len[2][4] = { { 142, 107, 379, 277 }, { 149, 113, 389, 281 } };
    float buf[2][4][kMax];
    int   w[2][4];
    float g = 0.68f;

    void reset() {
        std::memset(buf, 0, sizeof(buf));
        std::memset(w, 0, sizeof(w));
    }
    void setSampleRate(float fs) {
        const float s = fs / 44100.0f;
        static const int base[2][4] = { { 142, 107, 379, 277 }, { 149, 113, 389, 281 } };
        for (int c = 0; c < 2; c++)
            for (int k = 0; k < 4; k++) {
                int n = (int)(base[c][k] * s);
                if (n < 1) n = 1;
                if (n > kMax - 1) n = kMax - 1;
                len[c][k] = n;
            }
        reset();
    }
    inline float run(int c, float x) {
        for (int k = 0; k < 4; k++) {
            float *b = buf[c][k];
            int    L = len[c][k];
            int    r = w[c][k] - L; if (r < 0) r += kMax;
            float  d = b[r];
            float  v = x + g * d;
            b[w[c][k]] = v;
            w[c][k] = (w[c][k] + 1) % kMax;
            x = d - g * v;
        }
        return x;
    }
};

/** One-pole lowpass, used as the damping control for the k-series reverbs.
 *
 *  kWoodRoom and kGuitarHall2 have no true damping parameter — the control
 *  upstream calls "Filter" is a bezier UNDERSAMPLING rate, so turning it down
 *  makes the reverb lo-fi rather than dark. Mapping our Damp knob to it would
 *  trade an honest control for aliasing, so damping is done here instead. */
struct DampLP {
    float a = 0.0f, z[2] = { 0.0f, 0.0f };
    void set(float cutoff, float fs) {
        if (cutoff >= fs * 0.45f) { a = 0.0f; return; }   // 0 = bypass
        float x = expf(-2.0f * 3.14159265f * cutoff / fs);
        a = x;
    }
    void reset() { z[0] = z[1] = 0.0f; }
    inline void run(float &l, float &r) {
        if (a <= 0.0f) return;
        z[0] = l * (1.0f - a) + z[0] * a; l = z[0];
        z[1] = r * (1.0f - a) + z[1] * a; r = z[1];
    }
};

/** One effect instance. Only the algorithm the current type needs is run; all
 *  of them are resident because allocating on a type change would have to
 *  happen on the audio thread. */
#define DR32_PREDELAY_MAX_MS 200
#define DR32_PREDELAY_MAX ((int)(DR32_PREDELAY_MAX_MS * 48))   // headroom to 48 kHz

struct Slot {
    dr32_efx_type type = DR32_EFX_NONE;
    float p1 = 0.5f, p2 = 0.5f, p3 = 0.5f, mix = 1.0f;
    float pd = 0.0f;                       // pre-delay 0..1 -> 0..200 ms
                                           // (Drum Buss reuses this as Sustain)

    // Pre-delay line, applied before the reverb. The plate has its own internal
    // pre-delay (its third parameter!) but the k-reverbs have none, so one line
    // here keeps the control identical across all three — and the plate's
    // internal one is held at zero so they cannot stack.
    float pre[2 * DR32_PREDELAY_MAX];
    int   preW = 0, preLen = 0;

    SpaceExtra tank;                       // Dattorro figure-eight plate tank
    Diffuser   diff;                       // input diffusion ahead of the tank
    DampLP     plateLP;                    // fixed transducer rolloff, see apply()
    awk_verbity2::Verbity2 spaces;         // the flexible room-to-hall model
    DrumBuss   buss;

    void setSampleRate(float fs) {
        fs_ = fs;
        tank.setSampleRate(fs);
        tank.setType(SpaceExtra::Plate);
        diff.setSampleRate(fs);
        spaces.setSampleRate(fs);
        buss.setSampleRate(fs);
        apply();
    }

    void apply() {
        // p3 is DECAY for every reverb. The plate's own third parameter is its
        // internal pre-delay, so it is pinned to 0 and decay goes to `feed`
        // instead — previously p3 was passed as BOTH, so one knob was
        // simultaneously pre-delay and decay.
        // Damping is curved before it reaches the tank. Measured raw, the knob
        // does almost nothing over its first half — HF/LF decay ratio 0.95 at
        // 0, 0.91 at 0.25, 0.85 at 0.50 — and only bites above 0.75 (0.68) and
        // at 1.0 (0.51). So at the plate's 0.35 default the tank had virtually
        // no HF loss and the tail stayed glassy: per-Hz spectral density -2.0 dB
        // against -6.6 to -10.4 dB for the Airwindows plates.
        //
        // The exponent spreads that range over the knob instead of the top
        // corner. 0 still means "no damping" — a bright plate is a legitimate
        // thing to want, it just should not be the default.
        const float pd2 = p2 < 0.0f ? 0.0f : (p2 > 1.0f ? 1.0f : p2);
        const float tankDamp = powf(pd2, 0.40f);

        // A fixed HF rolloff on the plate's wet path. The damping curve above
        // fixes the DECAY (high frequencies now die away faster than low ones,
        // as they must), but not the tail's steady spectral balance: measured
        // per-Hz density stayed at about -2 dB across the whole damping range,
        // against -6.6 (kPlateC) to -10.4 dB (kPlate140).
        //
        // Those references are darker than us while decaying LESS
        // differentially, so their darkness is not a decay property at all —
        // it is the plate's transducer, which simply does not radiate the top
        // octaves efficiently. That is a fixed filter, and it belongs here
        // rather than on the damping knob.
        //
        // Deliberately gentler than an EMT model: this is the snare-and-clap
        // reverb on a drum rack, and it should keep more presence than a
        // 1957 plate. It lands near the brightest reference, not the darkest.
        plateLP.set(5500.0f, fs_);
        tank.setParams(p1, tankDamp, /*internal predelay*/ 0.0f, /*feed = decay*/ p3 * 0.75f, 1.0f);

        // --- Spaces (Verbity2) mapping ------------------------------------
        //   A RmSize  <- size   0.11-4.75 s at size .30, 0.37-16.1 s at .75,
        //                      with the onset growing 9.8 -> 32.8 ms, which is
        //                      what a bigger space physically does.
        //   B Sustain <- decay  SCALED. Raw, this runs away past 0.6: measured
        //                      32 s at 0.8 and 103 s at 1.0. Capped so the knob
        //                      spans a musical range instead of an infinite one.
        //   C Mulch   <- damp   a genuine tone control, and better than the
        //                      output lowpass this replaces: density sweeps
        //                      -0.26 to -14.69 dB/Hz while RT60 holds at
        //                      2.48-2.51 s, i.e. colour without length.
        //   D          = dry/wet, always 1; DR32 mixes outside.
        spaces.A = 0.20f + p1 * 0.55f;
        spaces.B = p3 * 0.45f;
        spaces.C = p2;
        spaces.D = 1.0f;

        // The Drum Buss has no use for pre-delay, so that knob is its Sustain.
        buss.setParams(p1, p2, p3, pd);

        int n = (int)(pd * (DR32_PREDELAY_MAX_MS * 0.001f) * fs_);
        if (n < 0) n = 0;
        if (n > DR32_PREDELAY_MAX - 1) n = DR32_PREDELAY_MAX - 1;
        preLen = n;
    }

    float fs_ = 44100.0f;

    void reset() {
        std::memset(pre, 0, sizeof(pre));
        preW = 0;
        tank.reset();
        diff.reset();
        plateLP.reset();
        spaces.reset();
        buss.reset();
    }

    /** Run `n` interleaved stereo frames in place. `sl`/`sr` are scratch. */
    void processBlock(float *io, int n, float *sl, float *sr) {
        if (type == DR32_EFX_DRUMBUSS) { buss.processBlock(io, n, sl, sr); return; }

        // Pre-delay ahead of the reverb.
        if (preLen > 0) {
            for (int i = 0; i < n; i++) {
                int w = preW % DR32_PREDELAY_MAX;
                int rd = (preW - preLen) % DR32_PREDELAY_MAX;
                if (rd < 0) rd += DR32_PREDELAY_MAX;
                pre[2 * w]     = io[2 * i];
                pre[2 * w + 1] = io[2 * i + 1];
                io[2 * i]     = pre[2 * rd];
                io[2 * i + 1] = pre[2 * rd + 1];
                preW = (preW + 1) % DR32_PREDELAY_MAX;
            }
        }

        switch (type) {
            case DR32_EFX_PLATE:
                for (int i = 0; i < n; i++) {
                    float l = diff.run(0, io[2 * i]), r = diff.run(1, io[2 * i + 1]);
                    tank.tick(l, r);
                    plateLP.run(l, r);
                    io[2 * i] = l; io[2 * i + 1] = r;
                }
                break;
            case DR32_EFX_SPACES: {
                // Verbity2's L and R paths are symmetric, so a centred hit came
                // out with the two channels bit-identical (correlation +1.00).
                // Rather than modify the transplant, the SAME per-channel
                // diffuser the plate uses is placed in front of it: its two
                // sides run different prime delay lengths, so the reverb is fed
                // two decorrelated signals and produces a decorrelated tail.
                // Allpasses are flat by construction, so this costs no tone.
                for (int i = 0; i < n; i++) {
                    sl[i] = diff.run(0, io[2 * i]);
                    sr[i] = diff.run(1, io[2 * i + 1]);
                }
                float *in[2] = { sl, sr }, *out[2] = { sl, sr };
                spaces.processReplacing(in, out, n);
                for (int i = 0; i < n; i++) { io[2 * i] = sl[i]; io[2 * i + 1] = sr[i]; }
                break;
            }
            default: break;
        }
    }

    bool active() const { return type != DR32_EFX_NONE; }
};

}  // namespace

struct dr32_fxbus {
    float sample_rate = 44100.0f;
    Slot  sends[DR32_SEND_SLOTS];
    Slot  inserts[DR32_INSERT_SLOTS];
    float send_return[DR32_SEND_SLOTS] = { 1.0f, 1.0f };
    // Per-block accumulation of what the pads sent to each bus.
    float send_buf[DR32_SEND_SLOTS][2 * DR32_MAX_BLOCK];
    // Blocks since anything was fed to each bus. A loaded-but-unused reverb
    // should cost nothing, but its tail must still ring out first.
    int   idle_blocks[DR32_SEND_SLOTS] = { 0, 0 };
    // De-interleave scratch, shared by every slot: the vendored algorithms are
    // block-based with separate L/R pointers. One copy, not one per slot.
    float scratch_l[DR32_MAX_BLOCK];
    float scratch_r[DR32_MAX_BLOCK];
    // Wet workspace for insert slots, which need the dry signal kept intact.
    float wet[2 * DR32_MAX_BLOCK];
};

extern "C" {

dr32_fxbus *dr32_fxbus_create(float sample_rate) {
    dr32_fxbus *fx = new (std::nothrow) dr32_fxbus();
    if (!fx) return nullptr;
    fx->sample_rate = (sample_rate > 1.0f) ? sample_rate : 44100.0f;
    for (int i = 0; i < DR32_SEND_SLOTS; i++) fx->sends[i].setSampleRate(fx->sample_rate);
    for (int i = 0; i < DR32_INSERT_SLOTS; i++) fx->inserts[i].setSampleRate(fx->sample_rate);
    std::memset(fx->send_buf, 0, sizeof(fx->send_buf));
    return fx;
}

void dr32_fxbus_destroy(dr32_fxbus *fx) { delete fx; }

void dr32_fxbus_set_send_type(dr32_fxbus *fx, int slot, dr32_efx_type type) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    if (fx->sends[slot].type == type) return;
    fx->sends[slot].type = type;
    fx->sends[slot].reset();
}

void dr32_fxbus_set_insert_type(dr32_fxbus *fx, int slot, dr32_efx_type type) {
    if (!fx || slot < 0 || slot >= DR32_INSERT_SLOTS) return;
    if (fx->inserts[slot].type == type) return;
    fx->inserts[slot].type = type;
    fx->inserts[slot].reset();
}

void dr32_fxbus_set_send_params(dr32_fxbus *fx, int slot,
                                float p1, float p2, float p3, float predelay) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    Slot &s = fx->sends[slot];
    s.p1 = p1; s.p2 = p2; s.p3 = p3; s.pd = predelay;
    s.mix = 1.0f;                 // a send return is ALWAYS 100% wet
    s.apply();
}

void dr32_fxbus_set_insert_params(dr32_fxbus *fx, int slot,
                                  float p1, float p2, float p3, float predelay, float mix) {
    if (!fx || slot < 0 || slot >= DR32_INSERT_SLOTS) return;
    Slot &s = fx->inserts[slot];
    s.p1 = p1; s.p2 = p2; s.p3 = p3; s.pd = predelay; s.mix = mix;
    s.apply();
}

void dr32_fxbus_set_send_return(dr32_fxbus *fx, int slot, float gain) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    fx->send_return[slot] = gain;
}

void dr32_fxbus_send(dr32_fxbus *fx, int slot, int frame, float l, float r) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    if (frame < 0 || frame >= DR32_MAX_BLOCK) return;
    fx->send_buf[slot][2 * frame]     += l;
    fx->send_buf[slot][2 * frame + 1] += r;
}

void dr32_fxbus_process(dr32_fxbus *fx, float *out, int n) {
    if (!fx || !out || n <= 0) return;
    if (n > DR32_MAX_BLOCK) n = DR32_MAX_BLOCK;

    // --- send buses: run the effect fully wet, add the return into the mix
    for (int s = 0; s < DR32_SEND_SLOTS; s++) {
        Slot &slot = fx->sends[s];
        float *buf = fx->send_buf[s];

        int fed = 0;
        for (int i = 0; i < 2 * n; i++) { if (buf[i] != 0.0f) { fed = 1; break; } }
        fx->idle_blocks[s] = fed ? 0 : (fx->idle_blocks[s] + 1);
        // ~4 s of silence is well past any tail these reverbs produce.
        const int idle_limit = (int)(4.0f * 44100.0f / (float)(n > 0 ? n : 128));

        if (slot.active() && fx->idle_blocks[s] <= idle_limit) {
            const float g = fx->send_return[s];   // return is fully wet by design
            slot.processBlock(buf, n, fx->scratch_l, fx->scratch_r);
            for (int i = 0; i < n; i++) {
                out[2 * i]     += buf[2 * i] * g;
                out[2 * i + 1] += buf[2 * i + 1] * g;
            }
        }
        std::memset(buf, 0, sizeof(float) * 2 * (size_t)n);
    }

    // --- inserts: serial, wet/dry per slot
    for (int s = 0; s < DR32_INSERT_SLOTS; s++) {
        Slot &slot = fx->inserts[s];
        if (!slot.active()) continue;
        const float wetg = slot.mix, dry = 1.0f - slot.mix;
        std::memcpy(fx->wet, out, sizeof(float) * 2 * (size_t)n);
        slot.processBlock(fx->wet, n, fx->scratch_l, fx->scratch_r);
        for (int i = 0; i < n; i++) {
            out[2 * i]     = out[2 * i]     * dry + fx->wet[2 * i]     * wetg;
            out[2 * i + 1] = out[2 * i + 1] * dry + fx->wet[2 * i + 1] * wetg;
        }
    }
}

void dr32_fxbus_reset(dr32_fxbus *fx) {
    if (!fx) return;
    for (int i = 0; i < DR32_SEND_SLOTS; i++) {
        fx->sends[i].reset();
        std::memset(fx->send_buf[i], 0, sizeof(fx->send_buf[i]));
    }
    for (int i = 0; i < DR32_INSERT_SLOTS; i++) fx->inserts[i].reset();
}

void dr32_efx_defaults(dr32_efx_type type, float *o) {
    if (!o) return;
    // [size, damp, decay, predelay, mix]. Pre-delay is in 0..200 ms.
    switch (type) {
        // NOTE on o[4] (mix): a REVERB as an insert must never default to fully
        // wet — that replaces the kit with its own ambience. Sends ignore this
        // value entirely (a send return is always 100% wet by design), so it
        // only affects insert slots.
        case DR32_EFX_PLATE:
            // Snare/clap plate: medium tank, a little damping so it is not
            // brittle, short-ish tail, ~10 ms pre-delay to keep the hit clear.
            o[0] = 0.45f; o[1] = 0.35f; o[2] = 0.45f; o[3] = 0.05f; o[4] = 0.28f; break;
        case DR32_EFX_SPACES:
            // Lands as a natural mid-size room rather than at either extreme:
            // Spaces is the one control set for everything from a tight room to
            // a hall, so it should open somewhere you would actually start.
            o[0] = 0.35f; o[1] = 0.40f; o[2] = 0.45f; o[3] = 0.02f; o[4] = 0.25f; break;
        case DR32_EFX_DRUMBUSS:
            // Gentle glue with a hint of grit and a little extra attack —
            // audible but not a statement. Fully wet: it is a processor, not an
            // ambience, so dry/dry blending would just weaken it.
            //
            // o[2] is Attack and o[3] is Sustain here, and BOTH are bipolar
            // about 0.5 — o[3] is the slot the reverbs use for pre-delay, so
            // leaving it at 0.0 would ship the Drum Buss with its tail pulled
            // down 8 dB.
            o[0] = 0.30f; o[1] = 0.15f; o[2] = 0.60f; o[3] = 0.5f; o[4] = 1.0f; break;
        default:
            o[0] = 0.5f; o[1] = 0.3f; o[2] = 0.5f; o[3] = 0.0f; o[4] = 1.0f; break;
    }
}

const char *dr32_efx_name(dr32_efx_type type) {
    switch (type) {
        case DR32_EFX_PLATE: return "Plate";
        case DR32_EFX_SPACES: return "Spaces";
        case DR32_EFX_DRUMBUSS: return "Drum Buss";
        default:             return "Off";
    }
}

dr32_efx_type dr32_efx_from_name(const char *name) {
    if (!name || !*name) return DR32_EFX_NONE;
    if (!std::strcmp(name, "Plate")) return DR32_EFX_PLATE;
    if (!std::strcmp(name, "Spaces")) return DR32_EFX_SPACES;
    // Kits store the effect type BY NAME, so anything saved before Room and
    // Hall were folded into one flexible model must still load. They map onto
    // Spaces rather than silently becoming "off".
    if (!std::strcmp(name, "Room"))  return DR32_EFX_SPACES;
    if (!std::strcmp(name, "Hall"))  return DR32_EFX_SPACES;
    if (!std::strcmp(name, "Drum Buss")) return DR32_EFX_DRUMBUSS;
    return DR32_EFX_NONE;
}

}  // extern "C"
