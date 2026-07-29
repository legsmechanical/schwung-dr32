// dr32_fxbus.cpp — implementation of DR32's send buses.
//
// C++ because the vendored reverbs are C++ structs (see dsp/vendor/SOURCES.md);
// the interface is extern "C" so the rest of the C11 engine is unaffected.

#include "dr32_fxbus.h"

#include "vendor/airwin_spaces.h"
#include "vendor/airwin_dyn.h"
#include "vendor/space_extra.h"
// Not vendored — ours, but a PORT rather than a design. Read its header before
// touching anything it does.
#include "dr32_supereco.h"

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <new>

#define DR32_SEND_SLOTS   2
#define DR32_MAX_BLOCK    1024

namespace {

/** Drum Bus — a drum-bus glue insert in the spirit of Ableton's Drum Buss.
 *
 *    Compress   — Airwindows *Pop3*'s compressor section, driven by ONE knob
 *                 that sweeps a coordinated threshold + ratio + release
 *                 program, followed by our own makeup gain.
 *    Crunch     — full-band saturation: tanh soft knee, a cubic term for grit,
 *                 and a deliberate asymmetry so it produces EVEN harmonics too.
 *    Attack     — fast/smoothed envelope pair, shaping the onset only.
 *    Sustain    — dual-release envelope detector shaping the decay only.
 *
 *  Attack and Sustain are orthogonal AND symmetric by measurement: sweeping
 *  Sustain end to end moves the tail -8.0 to +11.9 dB with the attack at
 *  0.00 dB, and sweeping Attack moves the hit -14.5 to +14.6 dB with the tail
 *  at 0.00 dB. Attack is also program-dependent — a 40 ms swell gets about half
 *  the treatment of a real hit, and steady material moves 0.17 dB.
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
 *  Both stages now use in-house detectors that are symmetric by construction
 *  and target disjoint parts of the hit.
 */
struct DrumBuss {
    float fs = 44100.0f;

    awk_pop3::Pop3   comp;      // compressor (its gate section is left off)
    /** Attack section.
     *
     *  Was Airwindows Point, which is excellent at SHARPENING and nearly inert
     *  at softening — the whole lower half of the knob bought under 2 dB
     *  (measured -1.92 dB at 0.00, -0.10 dB at 0.375) against +8.54 dB at the
     *  top. That is structural, not tuning: Point boosts by DIVIDING the slow
     *  follower's rate (nibDiv/(1.001-x), which reaches 3.1x) and softens by
     *  MULTIPLYING it (nibDiv*(1.001-(0.75x)^2), which cannot get past 0.44x).
     *  The softening direction simply cannot travel.
     *
     *  So the detector is ours, and symmetric by construction: a fast peak
     *  follower, and a slow SMOOTHING of that follower. Their difference is
     *  large only while the fast one is outrunning the smoothed one, i.e. at an
     *  onset.
     *
     *  The smoothed follower is what makes this attack-only. During a decay it
     *  lags ABOVE the fast one, so the difference goes negative and clamps to
     *  zero — the tail is untouched. An earlier version used two followers with
     *  different ATTACKS and a shared release; that never reconverged, because
     *  two envelopes falling at the same exponential rate keep whatever gap
     *  they had, so its "transient" reading stayed constant through the decay
     *  and the stage became a broadband gain (measured: attack AND tail both
     *  moved together). */
    struct Attack {
        float aFast = 0.0f, rFast = 0.0f, aSmooth = 0.0f;
        float envF[2] = { 0.0f, 0.0f }, envS[2] = { 0.0f, 0.0f };
        float depth = 0.0f;
        void setSampleRate(float fs) {
            aFast   = 1.0f - expf(-1.0f / (0.0005f * fs));  // 0.5 ms attack
            rFast   = 1.0f - expf(-1.0f / (0.050f * fs));   // 50 ms release
            aSmooth = 1.0f - expf(-1.0f / (0.030f * fs));   // 30 ms, both ways
        }
        void reset() { envF[0] = envF[1] = envS[0] = envS[1] = 0.0f; }
        inline void run(float *l, float *r, int n) {
            float *ch[2] = { l, r };
            for (int c = 0; c < 2; c++) {
                for (int i = 0; i < n; i++) {
                    float m = fabsf(ch[c][i]);
                    envF[c] += (m > envF[c] ? aFast : rFast) * (m - envF[c]);
                    envS[c] += aSmooth * (envF[c] - envS[c]);
                    // Normalised by envF, NOT by envS. Dividing by the
                    // smoothed follower looks equivalent and is not: coming out
                    // of silence envS is ~0, so the ratio explodes for ANY
                    // onset however gradual. Measured, that version applied an
                    // identical +-16.26 dB to a 3 ms click and to a 25 ms
                    // fade-in — a fixed gain on the first few ms, not transient
                    // shaping. Against envF the quantity is bounded in [0,1]
                    // and reads as "what fraction of this moment is faster than
                    // the local average", which is what transient-ness means.
                    float t = (envF[c] - envS[c]) / (envF[c] + 1e-6f);
                    if (t < 0.0f) t = 0.0f;      // decay: smoothed sits above
                    if (t > 1.0f) t = 1.0f;
                    float g = exp2f(depth * t);
                    if (g < 0.05f) g = 0.05f;
                    if (g > 12.0f) g = 12.0f;
                    ch[c][i] *= g;
                }
            }
        }
    } atk;

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
        sus.setSampleRate(fs);
    }

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    /** p1 compress, p2 crunch, p3 attack (0.5 neutral), p4 sustain (0.5 neutral). */
    void setParams(float p1, float p2, float p3, float p4) {
        const float c = clamp01(p1);
        crunch = clamp01(p2);
        compOn = c > 0.001f;

        // --- one knob, a coordinated program ---------------------------------
        // Threshold, ratio and release all move together, on a SQUARED taper.
        //
        // The first version drove them linearly and started the ratio at 0.30,
        // so the knob stepped straight into audible compression the moment it
        // left zero and then had little left to give: measured hat-duck went
        // -1.2 / -3.6 / -7.5 / -14.5 dB across the knob. Squaring spreads the
        // gentle half out and saves the violence for the top, and the top now
        // reaches further than it used to (threshold to -38 dBFS, ratio to a
        // full limit, release down to ~55 ms so it pumps rather than just
        // flattens).
        //
        // Attack stays pinned near Pop3's fastest (~11 ms) at every setting:
        // slow enough to let the beater click through, fast enough to catch the
        // body. That is what keeps click-to-body positive as this is pushed.
        // Exponent chosen off the measured duck curve, not by feel. Linear stepped
        // straight into audible compression (-1.2 dB of duck at knob 0.25) and
        // had little left at the top. 1.6 keeps the bottom genuinely subtle
        // (-0.22 dB at 0.30) while leaving real glue through the middle
        // (-1.4 at 0.50, -4.4 at 0.70) and saving the violence for the end
        // (-11.5 at 0.90, -21.5 at 1.00). A square law was tried and left the
        // whole bottom half inert.
        const float cc = powf(c, 1.6f);
        const float thrDb  = -4.0f - 34.0f * cc;
        const float thrLin = powf(10.0f, thrDb / 20.0f);
        comp.A = powf(thrLin, 0.25f);                 // compThresh = A^4
        const float ratio  = 0.06f + 0.94f * cc;
        comp.B = 1.0f - sqrtf(1.0f - ratio);          // compRatio = 1-(1-B)^2
        comp.C = 0.10f;                               // ~11.5 ms attack
        comp.D = 0.62f - 0.10f * cc;                  // ~115 ms down to ~55 ms
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
        // Signed depth, so up sharpens and down softens by comparable amounts.
        atk.depth = clampBi(p3) * 2.6f;   // +-15.6 dB at a fully transient onset
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

        if (atkOn) atk.run(sl, sr, n);
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

/** Tempo-synced stereo delay.
 *
 *  Modelled on the controls of Move's own `delay` device as it is actually used
 *  in the factory drum kits — all 12 of the 77 kits whose return chain is a
 *  delay were read off the device and every decision below comes from that
 *  corpus rather than from the device's full parameter list:
 *
 *    - Time is a COUNT OF SIXTEENTHS, 1..16. That is literally what
 *      DelayLine_SyncedSixteenth holds, and in synced mode the device offers no
 *      triplet or dotted values at all, so there is no division table to build.
 *    - L and R are timed INDEPENDENTLY and differ in every one of the twelve
 *      kits: L is 1-3 sixteenths, R is 4 in ten of them. That asymmetry is the
 *      sound of these presets, which is why this has two time controls and not
 *      one — a single shared time reproduces none of them.
 *    - The feedback filter is on in all twelve (530 Hz - 5.4 kHz centre, ~4
 *      octaves wide), so it is not optional here either. Filtering INSIDE the
 *      loop is what stops repeats accumulating into mush; on the output it
 *      would just darken the first tap.
 *    - SmoothingMode is "Repitch" in all twelve, so retiming repitches the tail
 *      tape-style. It is the only mode in the corpus, so it is the only one
 *      implemented — see the slew in run().
 *    - Modulation is off in 9 of 12 and incoherent in the rest (amounts 0.03 to
 *      0.58, rates 0.27 to 23 Hz). Deliberately not implemented.
 *
 *  Not a reconstruction: like Plate and Spaces, this is DR32's own effect
 *  wearing the native device's controls. Move's delay was never reverse
 *  engineered, and nothing here has been null-tested against it. */
struct Delay {
    // 16 sixteenths = 4 beats = 4 s at 60 BPM. Slower than that and the longest
    // divisions stop tracking tempo, which is the right trade against an
    // allocation that grows without bound as the tempo falls.
    static constexpr float kMaxSeconds = 4.0f;
    static constexpr float kMaxFeedback = 0.95f;
    // Free-time range. The bottom is short enough for a slapback/comb and the
    // top sits inside the 4 s line with room to spare.
    static constexpr float kMinMs = 10.0f;
    static constexpr float kMaxMs = 2000.0f;

    float  fs = 44100.0f;
    float *buf[2] = { nullptr, nullptr };
    int    cap = 0;
    int    w = 0;

    // Delay length in samples, fractional: `len` chases `target`, it does not
    // jump to it. See run().
    float  len[2] = { 0.0f, 0.0f }, target[2] = { 0.0f, 0.0f };
    // Synced and free times live side by side and BOTH survive a flip of
    // `synced`, the way the native device carries SyncedSixteenth and its free
    // time at once. Toggling sync therefore recalls what you last had in that
    // mode instead of reinterpreting one number in the wrong unit.
    float  sixteenths[2] = { 1.0f, 4.0f };
    float  freeMs[2] = { 125.0f, 500.0f };
    bool   synced = true;
    float  bpm = 120.0f;

    float  fb = 0.5f, pingpong = 0.0f;
    // Whether anything has been written into the lines since the last reset.
    // An empty line has nothing to repitch, so a time change before the first
    // hit must LAND rather than glide — otherwise the delay spends the first
    // seconds of its life sliding up from wherever it happened to start, and
    // the first repeat comes back at the wrong time.
    bool   primed = false;
    // Feedback bandpass, one pole each way. lpZ is the lowpass state; hpZ is the
    // lowpass whose output is SUBTRACTED to make the highpass.
    float  lpA = 1.0f, hpA = 0.0f;
    float  lpZ[2] = { 0.0f, 0.0f }, hpZ[2] = { 0.0f, 0.0f };

    ~Delay() { delete[] buf[0]; delete[] buf[1]; }

    void setSampleRate(float sr) {
        fs = (sr > 1.0f) ? sr : 44100.0f;
        int want = (int)(kMaxSeconds * fs) + 4;
        if (want != cap) {
            delete[] buf[0]; delete[] buf[1];
            // Host thread only (dr32_fxbus_create). Never from a type change.
            buf[0] = new (std::nothrow) float[want];
            buf[1] = new (std::nothrow) float[want];
            cap = (buf[0] && buf[1]) ? want : 0;
        }
        // recalc BEFORE reset: reset lands the lengths on their targets, and a
        // target that has not been computed yet is zero.
        recalc();
        reset();
    }

    void reset() {
        if (buf[0]) std::memset(buf[0], 0, sizeof(float) * (size_t)cap);
        if (buf[1]) std::memset(buf[1], 0, sizeof(float) * (size_t)cap);
        w = 0;
        lpZ[0] = lpZ[1] = hpZ[0] = hpZ[1] = 0.0f;
        primed = false;
        // Land ON the target rather than sliding up to it from zero — the slew
        // exists to repitch a change, not to swoop in every time a kit loads.
        len[0] = target[0];
        len[1] = target[1];
    }

    static float clampf(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    /** See dr32_fxbus.h for the slot table: sixteenths, sixteenths, feedback,
     *  tone, ping-pong, sync, free ms, free ms. */
    void setParams(const float *p) {
        sixteenths[0] = clampf(p[0], 1.0f, 16.0f);
        sixteenths[1] = clampf(p[1], 1.0f, 16.0f);
        fb = clampf(p[2], 0.0f, 1.0f) * kMaxFeedback;
        pingpong = clampf(p[4], 0.0f, 1.0f);
        synced = p[5] >= 0.5f;
        freeMs[0] = clampf(p[6], kMinMs, kMaxMs);
        freeMs[1] = clampf(p[7], kMinMs, kMaxMs);

        // Tone: one knob for the native pair (Filter_Frequency + Bandwidth).
        // Centre sweeps 200 Hz - 6 kHz logarithmically; bandwidth is pinned at
        // the corpus norm of 4 octaves, i.e. +-2 octaves around the centre.
        const float centre = 200.0f * powf(30.0f, clampf(p[3], 0.0f, 1.0f));
        setOnePole(lpA, clampf(centre * 4.0f, 20.0f, fs * 0.45f));
        setOnePole(hpA, clampf(centre * 0.25f, 20.0f, fs * 0.45f));
        recalc();
    }

    void setBpm(float b) {
        if (b < 20.0f || b > 400.0f) b = 120.0f;   // get_bpm's own fallback range
        if (b == bpm) return;
        bpm = b;
        recalc();
    }

    void setOnePole(float &a, float f) { a = 1.0f - expf(-2.0f * 3.14159265f * f / fs); }

    void recalc() {
        const float perSixteenth = (60.0f / bpm) * 0.25f * fs;   // samples
        for (int c = 0; c < 2; c++) {
            const float want = synced ? sixteenths[c] * perSixteenth
                                      : freeMs[c] * 0.001f * fs;
            target[c] = clampf(want, 1.0f, (float)(cap - 2));
            if (!primed) len[c] = target[c];    // nothing in the line to repitch
        }
    }

    /** Read `d` samples back, `d` fractional.
     *
     *  ⚠ The wrap is INTEGER and the write cursor never enters the float maths.
     *  Doing it the obvious way — `float rp = w - d`, wrap in float, split into
     *  index and fraction — is wrong twice over, and both bites are silent:
     *
     *    - PRECISION. A float has 24 bits of mantissa, so near a wrapped
     *      position of ~176400 (a 4 s line) one ulp is 1/64 of a SAMPLE. The
     *      sub-sample fraction of the delay is quantised by how far into the
     *      buffer the cursor happens to be, which is a moving target.
     *    - RANGE. `cap - tiny` ROUNDS UP to exactly `cap` in float, so `(int)rp`
     *      indexes one past the end of the buffer — an out-of-bounds read on the
     *      audio thread, dressed up as a slightly wrong sample.
     *
     *  Caught by a free-time test asking for 300 ms: 300 * 0.001f * 44100 lands
     *  on 13230 + 1/1024 rather than 13230, and a unit impulse came back as a
     *  single sample of 1/1024 — the energy did not smear, it VANISHED, because
     *  one of the two neighbours was read from past the end. The synced tests
     *  had all landed on exact integers and never touched it. */
    inline float readAt(int c, float d) const {
        if (d < 0.0f) d = 0.0f;
        const int   di = (int)d;
        const float fr = d - (float)di;      // small, so this stays exact
        int i0 = w - di;      while (i0 < 0) i0 += cap;   // delay di
        int i1 = i0 - 1;      if (i1 < 0)    i1 += cap;   // delay di + 1
        // Linear, like the sampler's reader — which is linear by measurement,
        // not by preference (CLAUDE.md).
        return buf[c][i0] + (buf[c][i1] - buf[c][i0]) * fr;
    }

    /** In place, interleaved. The return is 100% wet, so the input is written
     *  into the lines and only the taps come out. */
    void run(float *io, int n) {
        if (!cap) return;
        // Repitch: the length moves toward its target at a bounded rate, so a
        // tempo or knob change drags the read pointer and shifts the pitch of
        // whatever is still in the line. Move's device has no other smoothing
        // mode.
        //
        // Half a sample per sample: the read pointer runs at 0.5x or 1.5x while
        // it travels, which is an audible whoosh rather than a click, and the
        // largest jump on offer (1 to 16 sixteenths at 120 BPM, ~0.75 s of line)
        // settles in about 1.5 s. An earlier 0.125 took four seconds to cross
        // that, which reads as a broken control rather than as a glide.
        const float kSlew = 0.5f;

        for (int i = 0; i < n; i++) {
            for (int c = 0; c < 2; c++) {
                float d = target[c] - len[c];
                if (d >  kSlew) d =  kSlew;
                if (d < -kSlew) d = -kSlew;
                len[c] += d;
            }

            const float outL = readAt(0, len[0]);
            const float outR = readAt(1, len[1]);

            // Ping-pong as a CONTINUOUS crossfeed: 0 is two independent lines
            // (the device's PingPong: false), 1 is full alternation (true), and
            // the middle is a legitimate width control. A float also keeps this
            // out of the enum-string parsing that the generic slot path has no
            // room for.
            //
            // ⚠ Crossfeeding the FEEDBACK is not enough on its own. With the two
            // times equal and a centred hit, outL and outR are identical, so
            // swapping them changes nothing and the control does nothing at all
            // — which is exactly what it did on the device (Josh, 2026-07-28),
            // while the L-only test signal that was supposed to cover it happens
            // to be the one case that works without this. The INPUT has to be
            // steered too: at full ping-pong the hit is summed to mono and fed
            // into the LEFT line only, so the first repeat comes back left, the
            // crossed feedback puts the second one right, and it alternates.
            float fbL = outL + (outR - outL) * pingpong;
            float fbR = outR + (outL - outR) * pingpong;

            float f[2] = { fbL, fbR };
            for (int c = 0; c < 2; c++) {
                lpZ[c] += lpA * (f[c] - lpZ[c]);        // lowpass
                float x = lpZ[c];
                hpZ[c] += hpA * (x - hpZ[c]);           // its own lowpass...
                f[c] = x - hpZ[c];                      // ...subtracted = highpass
            }

            const float inL = io[2 * i], inR = io[2 * i + 1];
            if (inL != 0.0f || inR != 0.0f) primed = true;
            // See the note above: the input collapses toward "mono into L only"
            // as ping-pong opens, which is what makes the taps alternate for a
            // centred source.
            const float mono = 0.5f * (inL + inR);
            const float injL = inL + (mono - inL) * pingpong;
            const float injR = inR - inR * pingpong;
            buf[0][w] = injL + f[0] * fb;
            buf[1][w] = injR + f[1] * fb;
            w++; if (w >= cap) w = 0;

            io[2 * i]     = outL;
            io[2 * i + 1] = outR;
        }
    }
};

/** NonLin — the AMS RMX16's trick, and the reason it is called NONLINEAR: the
 *  tail does not decay at all. It holds a roughly constant level for a set time
 *  and then stops dead.
 *
 *  That is NOT the Gated type with a different knob. A gate follows the input
 *  and chops an exponential decay, so the level is always falling underneath —
 *  you hear a reverb being cut off. NonLin overrides the decay itself: the
 *  window is flat (or deliberately RISING, which no natural space does, and
 *  which is most of the character), and the end is a cliff rather than a fade.
 *
 *  Built from the plate tank rather than a fixed tap pattern. The period units
 *  used a dense FIR, which would be ~13k taps per channel for a 300 ms window
 *  here — hopeless per-sample on a Move core. Instead the tank is run with its
 *  feedback pinned near maximum, so its natural fall across a 300 ms window is
 *  a couple of dB rather than tens, and the synthetic envelope does the rest.
 *  Measured flatness is asserted in the tests; "near enough to flat" is a claim
 *  that has to be checked, not assumed.
 *
 *  Retriggers on transients, so a busy pattern re-arms the window per hit
 *  instead of the first hit owning it. */
struct NonLin {
    float fs = 44100.0f;
    // Transient detector: the same fast-follower-vs-smoothed-follower shape the
    // Drum Bus's Attack section uses, for the same reason — it reads "how much
    // of this moment is faster than the local average", which is bounded and
    // does not explode coming out of silence.
    float aFast = 0.0f, rFast = 0.0f, aSmooth = 0.0f;
    float envF = 0.0f, envS = 0.0f;

    int   pos = -1;          // samples since the window opened, -1 = closed
    int   len = 0;           // window length in samples
    int   rel = 66;          // release, in samples, AFTER the window
    int   refractory = 0;    // samples until a retrigger is allowed
    float tiltA = 1.0f, tiltB = 1.0f;   // gain at the start and end of the window

    void setSampleRate(float sr) {
        fs = (sr > 1.0f) ? sr : 44100.0f;
        aFast   = 1.0f - expf(-1.0f / (0.0005f * fs));
        rFast   = 1.0f - expf(-1.0f / (0.050f * fs));
        aSmooth = 1.0f - expf(-1.0f / (0.030f * fs));
    }
    void reset() { envF = envS = 0.0f; pos = -1; refractory = 0; }

    /** len01 -> 50..600 ms. shape 0.5 = flat, 0 = falling, 1 = rising (+-9 dB
     *  across the window, normalised so the peak stays at unity — a rising
     *  window must not also be a louder one). */
    void setParams(float len01, float shape, float rel01) {
        if (len01 < 0.0f) len01 = 0.0f; else if (len01 > 1.0f) len01 = 1.0f;
        if (shape < 0.0f) shape = 0.0f; else if (shape > 1.0f) shape = 1.0f;
        if (rel01 < 0.0f) rel01 = 0.0f; else if (rel01 > 1.0f) rel01 = 1.0f;
        len = (int)((0.050f + 0.550f * len01) * fs);
        // 1 ms .. 500 ms, logarithmic: the useful half of this control is the
        // short end, where it decides chop vs thump, and a linear knob would
        // spend nine tenths of its travel above 50 ms.
        rel = (int)(0.001f * powf(500.0f, rel01) * fs);
        if (rel < 2) rel = 2;
        const float db = (shape - 0.5f) * 18.0f;
        float a = powf(10.0f, -db / 40.0f), b = powf(10.0f, db / 40.0f);
        const float m = (a > b) ? a : b;
        tiltA = a / m; tiltB = b / m;
    }

    /** Call with the INPUT frame (for triggering) and the tank's WET frame. */
    inline void run(float inL, float inR, float &wl, float &wr) {
        const float m = fabsf(inL) + fabsf(inR);
        envF += (m > envF ? aFast : rFast) * (m - envF);
        envS += aSmooth * (envF - envS);
        const float t = (envF - envS) / (envF + 1e-6f);
        if (refractory > 0) refractory--;
        // 0.35 of "faster than local average", plus a real-signal floor so the
        // noise between hits cannot arm it. 30 ms refractory: a flam re-arming
        // the window twice in 5 ms would swallow its own front.
        if (t > 0.35f && envF > 1e-4f && refractory == 0) {
            pos = 0;
            refractory = (int)(0.030f * fs);
        }

        if (pos < 0) { wl = 0.0f; wr = 0.0f; return; }

        const int fade = (int)(0.0015f * fs);        // 1.5 ms, click-free in
        if (pos >= len + rel) { pos = -1; wl = 0.0f; wr = 0.0f; return; }

        float g;
        if (pos < len) {
            const float frac = (float)pos / (float)(len > 0 ? len : 1);
            g = tiltA + (tiltB - tiltA) * frac;
            if (pos < fade) g *= (float)pos / (float)fade;      // no click in
        } else {
            // The release runs AFTER the window rather than eating the end of
            // it, so lengthening it does not shorten what you set. Linear from
            // wherever the tilt left off — at the short end that is the cliff
            // this always had, at the long end it is a slope.
            const float k = (float)(pos - len) / (float)rel;
            g = tiltB * (1.0f - k);
        }
        pos++;
        wl *= g; wr *= g;
    }
};

/** One effect instance. Only the algorithm the current type needs is run; all
 *  of them are resident because allocating on a type change would have to
 *  happen on the audio thread. */
#define DR32_PREDELAY_MAX_MS 200
#define DR32_PREDELAY_MAX ((int)(DR32_PREDELAY_MAX_MS * 48))   // headroom to 48 kHz

/** Output trim on the Native reverb, so selecting it does not jump the send
 *  level against the other types. Measured, not guessed: it is the ratio that
 *  puts Native's tail RMS on the Plate's at each type's own default settings.
 *  ⚠ It is a LEVEL match only — it must not be used to compensate for anything
 *  the port gets wrong, and the null test bypasses it.
 *
 *  ⚠⚠ RE-MEASURE THIS AFTER ANY ENGINE CHANGE. It has silently gone stale twice:
 *  once when the real mixer replaced the placeholder, and once when the Band
 *  filter stopped collapsing to a razor-thin band-pass — the second left Native
 *  16 dB quiet, which reads as "this reverb is broken" rather than as a stale
 *  constant. It is the last thing to check before a deploy. */
static const float kNativeTrim = 1.339f;

struct Slot {
    dr32_efx_type type = DR32_EFX_NONE;
    // The generic control slots, see dr32_fxbus.h for what each means per type.
    // Named p[] rather than p1/p2/pd/... because two of them have now changed
    // meaning as types were added, and a positional name that lies is worse
    // than no name.
    float p[DR32_SEND_PARAMS] = { 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 125.0f, 500.0f };

    // Pre-delay line, applied before the reverb. The plate has its own internal
    // pre-delay (its third parameter!) but the k-reverbs have none, so one line
    // here keeps the control identical across all three — and the plate's
    // internal one is held at zero so they cannot stack.
    float pre[2 * DR32_PREDELAY_MAX];
    int   preW = 0, preLen = 0;

    SpaceExtra tank;                       // Dattorro figure-eight plate tank
    Diffuser   diff;                       // input diffusion ahead of the tank
    DampLP     plateLP;                    // transducer rolloff, swept by Damp
    DampLP     spacesLP;                   // extends Spaces' dark end only
    awk_verbity2::Verbity2 spaces;         // the flexible room-to-hall model
    Delay      delay;                      // tempo-synced, filtered feedback
    NonLin     nonlin;                     // flat-then-cliff envelope over the tank
    dr32_supereco::SuperEco native;        // the PORTED late network of Move's Reverb
    // ⚠ NULL-TEST MODE. Set only by dr32_fxbus_native_raw_commit(). It turns
    // off the two things that exist to make Native sit politely next to the
    // other send types and would otherwise corrupt a null measurement: the
    // output level trim, and the idle-skip that stops an unused bus costing
    // CPU. Never set from the UI or from saved state.
    bool nativeRawMode = false;

    /* Which SpaceExtra type a DR32 type arms, -1 for the ones that do not run
     * through the shared tank. Only ONE type is armed at a time — the struct is
     * built that way (its two delays share a buffer) — which is fine, since a
     * slot has exactly one type. */
    static int spaceType(dr32_efx_type t) {
        switch (t) {
            case DR32_EFX_PLATE:   return SpaceExtra::Plate;
            case DR32_EFX_GATED:   return SpaceExtra::GatedRev;
            // NonLin runs the PLAIN plate tank — its own envelope replaces the
            // decay, so arming the gated variant would gate it twice.
            case DR32_EFX_NONLIN:  return SpaceExtra::Plate;
            case DR32_EFX_DIGITAL: return SpaceExtra::Digital;
            case DR32_EFX_HALL:    return SpaceExtra::Hall;
            default:               return -1;
        }
    }
    bool isTank() const { return spaceType(type) >= 0; }

    void setSampleRate(float fs) {
        fs_ = fs;
        tank.setSampleRate(fs);
        tank.setType(spaceType(type) >= 0 ? spaceType(type) : (int)SpaceExtra::Plate);
        diff.setSampleRate(fs);
        spaces.setSampleRate(fs);
        delay.setSampleRate(fs);
        nonlin.setSampleRate(fs);
        native.setSampleRate(fs);
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
        // Arm the tank for whichever type this slot is. setType() early-outs on
        // an unchanged value, so this is free on a plain knob move.
        if (isTank()) tank.setType(spaceType(type));

        const float pd2 = p[1] < 0.0f ? 0.0f : (p[1] > 1.0f ? 1.0f : p[1]);
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
        // ...and it SWEEPS with Damp. Pinning it meant the knob only shortened
        // the HF decay (ratio 0.96 -> 0.52) while the tail's colour never
        // changed: measured density moved 0.16 dB across the whole control
        // (-5.66 to -5.82), which reads as "damping does nothing".
        //
        // Geometric sweep chosen so the plate's 0.35 default lands on the same
        // 5.5 kHz it has been voiced at, with 0 meaning genuinely undamped
        // (12.5 kHz) and 1.0 genuinely dark (1.2 kHz).
        plateLP.set(12486.0f * powf(0.0961f, pd2), fs_);
        // Damping is curved for the PLATE (and the gate, which is the plate
        // tank); the FDN and Chamber have their own damping laws inside
        // SpaceExtra and want the raw knob.
        //
        // The internal pre-delay is pinned at 0 for every type: DR32 runs its
        // own pre-delay line ahead of the tank, and the two would stack.
        //
        // ⚠ Decay is scaled per type rather than shared. 0.75 is the plate's
        // measured ceiling; Chamber reaches cathedral length at the top of its
        // range (it was pulled once for exactly that, commit 07ca02c), and the
        // GATE reads this slot as its HOLD time, where the full range is the
        // point.
        switch (type) {
            case DR32_EFX_GATED:
                tank.setParams(p[0], tankDamp, 0.0f, /*hold*/ p[2], 1.0f);
                // Slot 4 is the tank's OWN decay here (NonLin uses the same slot
                // for Shape). A gated reverb has two lengths — how long the
                // reverb rings and how long the gate stays open — and collapsing
                // them into one was what made this type indistinguishable from
                // NonLin.
                tank.setGateDecay(0.20f + 0.72f * p[4]);
                // Same law and same slot as NonLin's release: 1 ms .. 500 ms.
                tank.setGateRelease(0.001f * powf(500.0f, p[5] < 0.0f ? 0.0f :
                                                 (p[5] > 1.0f ? 1.0f : p[5])));
                break;
            case DR32_EFX_HALL:
                tank.setParams(p[0], pd2, 0.0f, /*feed*/ p[2] * 0.20f, 1.0f); break;
            case DR32_EFX_NONLIN:
                // Feedback pinned near maximum so the tank barely falls across
                // the window; slot 2 is the window LENGTH, not a decay, and
                // slot 4 tilts it.
                tank.setParams(p[0], tankDamp, 0.0f, /*feed*/ 0.95f, 1.0f);
                nonlin.setParams(p[2], p[4], p[5]);
                break;
            case DR32_EFX_DIGITAL:
                tank.setParams(p[0], pd2, 0.0f, /*feed*/ p[2] * 0.85f, 1.0f); break;
            default:
                tank.setParams(p[0], tankDamp, 0.0f, /*feed = decay*/ p[2] * 0.75f, 1.0f); break;
        }

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
        spaces.A = 0.20f + p[0] * 0.55f;
        spaces.B = p[2] * 0.45f;
        spaces.C = p[1];
        // Verbity2's Mulch alone spans -0.4 to -12.5 dB/Hz, which is a usable
        // range, so this only extends the DARK end: bypassed below 0.4 so the
        // part of the knob that already worked is untouched.
        {
            const float t = (pd2 - 0.4f) / 0.6f;
            spacesLP.set(t <= 0.0f ? 20000.0f : 20000.0f * powf(0.075f, t), fs_);
        }
        spaces.D = 1.0f;

        // --- Native (the ported SuperEco late network) ---------------------
        //
        // These knobs drive the DEVICE'S OWN parameters directly, in the
        // device's units, rather than through a DR32-flavoured abstraction —
        // there is no reason to invent a mapping for controls that already
        // exist. What is chosen here is only the RANGE each knob spans.
        if (type == DR32_EFX_NATIVE) {
            const float sz = p[0] < 0.0f ? 0.0f : (p[0] > 1.0f ? 1.0f : p[0]);
            const float dk = p[2] < 0.0f ? 0.0f : (p[2] > 1.0f ? 1.0f : p[2]);
            // RoomSize over the full span the 19 stock Reverb presets use,
            // 0.27..500. Log, because the device takes its CUBE ROOT — a linear
            // knob would spend its whole top half moving the tank by a few
            // percent.
            native.roomSize = 0.27f * powf(500.0f / 0.27f, sz);
            // DecayTime 0.3..8 s. The stock presets go to 19.5 s, but the
            // shelves impose a hard ceiling (see below) and everything past
            // about 8 s lands on it — a knob whose top third does nothing is
            // the defect this suite exists to catch. The null test drives the
            // full range directly, not through here.
            native.decayTime = 0.3f * powf(8.0f / 0.3f, dk);
            // ⚠ "Damping" is BOTH shelf gains, and they are per-round-trip
            // losses — so this knob shortens the tail as well as colouring it.
            // That is not a modelling shortcut, it is the mechanism: it is what
            // caps this reverb's RT60 near 5-7 s however long DecayTime asks
            // for. Measured in the port: with both shelves at unity RT60 tracks
            // 0.900 x DecayTime at every point out to 19.5 s; with the stock
            // shelves it saturates at 6.6 s, against the device's measured
            // 5.44 s.
            //
            // ⚠ The LOW shelf does most of that. Both shelves CUT — the loop is
            // a band-pass between ShelfLoFreq and ShelfHiFreq — and since most
            // of a drum's energy is below 670 Hz, pinning the low shelf open
            // (as this first did) leaves a knob that moves RT60 by 7% and looks
            // broken. It is the low shelf that makes the ceiling.
            //
            // ⭑ One knob reaches the factory setting exactly: the stock drum
            // kits' pair (ShelfLoGain 0.6167, ShelfHiGain 0.8833) lies on a
            // straight line through unity, so damp = 0.59 lands on BOTH.
            native.shelfLoGain = 1.0f - 0.65f * pd2;
            native.shelfHiGain = 1.0f - 0.20f * pd2;
            // Diffusion (AllPassGain). 0.6 is the value every stock DRUM KIT
            // uses; the audio-effect presets range wider.
            native.allPassGain = 0.30f + 0.65f * (p[4] < 0.0f ? 0.0f :
                                                  (p[4] > 1.0f ? 1.0f : p[4]));
            // ⚠ Pre-delay goes to the DEVICE'S OWN PreDelay, not to DR32's line
            // in front. It is not merely a delay here: it moves the early taps
            // AND compensates both their gains and the tap-modulation depth, so
            // routing it through the generic line would silently drop three
            // recovered laws. The generic line is disabled for this type below.
            native.preDelayMs = (p[3] < 0.0f ? 0.0f : (p[3] > 1.0f ? 1.0f : p[3]))
                                * (float)DR32_PREDELAY_MAX_MS;
            native.build();
        }

        // The Delay reads the whole array (its own units — see the header).
        delay.setParams(p);

        // Native owns its pre-delay (see above), so the shared line is off for
        // it — otherwise the two would stack.
        int n = (type == DR32_EFX_NATIVE) ? 0
                : (int)(p[3] * (DR32_PREDELAY_MAX_MS * 0.001f) * fs_);
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
        spacesLP.reset();
        spaces.reset();
        delay.reset();
        nonlin.reset();
        native.reset();
    }

    /** Run `n` interleaved stereo frames in place. `sl`/`sr` are scratch. */
    void processBlock(float *io, int n, float *sl, float *sr) {
        // The Delay does its own timing and has no use for the reverbs'
        // pre-delay line — a pre-delay in front of a delay is just a longer
        // delay, and it would desync the first tap from the grid.
        if (type == DR32_EFX_DELAY) { delay.run(io, n); return; }

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

        if (isTank()) {
            // The per-channel diffuser runs in front of EVERY tank type, not
            // just the plate. Its two sides use different prime lengths, so it
            // hands the reverb two decorrelated inputs — which is what stops a
            // symmetric algorithm collapsing to mono. Chamber has exactly that
            // defect and is why Hall was pulled from DR32 once (cef30f4: an
            // L-only impulse put -107 dB in the right channel and max|L-R| was
            // exactly 0). Allpasses are flat by construction, so this costs no
            // tone. The plate always ran through it, so its sound is unchanged.
            //
            // plateLP is the PLATE's transducer rolloff and stays on the plate
            // and its gated variant only — the FDN and Chamber carry their own
            // damping, and stacking ours on top would just make them dull.
            const bool lp = (type == DR32_EFX_PLATE || type == DR32_EFX_GATED
                             || type == DR32_EFX_NONLIN);
            // ⚠ Chamber is TWO INDEPENDENT MONO REVERBS — nothing crosses
            // between its channels. The diffuser alone does not save it: it is
            // per-channel, so an L-only hit still leaves the right side empty
            // (measured -112 dB into R, which is the very defect that got Hall
            // pulled from DR32 in cef30f4, at -107 dB). It needs the mono SUM
            // fed to both sides; the diffuser's two sides run different prime
            // lengths, so the two tails still come out decorrelated.
            //
            // Only Hall. The plate's figure-eight tank and the FDN both couple
            // their channels internally and measure fine from an L-only hit
            // (+0.1 dB and -0.4 dB), and forcing a mono sum on the plate would
            // change a voicing that has been measured and tuned.
            const bool monoIn = (type == DR32_EFX_HALL);
            for (int i = 0; i < n; i++) {
                const float inL = io[2 * i], inR = io[2 * i + 1];
                const float a = monoIn ? 0.5f * (inL + inR) : inL;
                const float b = monoIn ? 0.5f * (inL + inR) : inR;
                float l = diff.run(0, a), r = diff.run(1, b);
                // The tank now has its own early reflections (space_extra.h
                // taps the input diffusers, which fixed the plate's 44 ms
                // onset), so this is a TOP-UP rather than the whole early
                // field: without it NonLin arrives at 4.3 ms and its first 5 ms
                // sits at 19% of the window level, which for a burst effect
                // reads as a soft front. 0.35 puts the start ON the hit.
                // Measured both ways; the test asserts the result.
                const float earlyL = l, earlyR = r;
                tank.tick(l, r);
                if (type == DR32_EFX_NONLIN) { l += earlyL * 0.35f; r += earlyR * 0.35f; }
                if (lp) plateLP.run(l, r);
                // ⚠ Triggered from the INPUT, not from the tank's output: the
                // wet signal has already been smeared by the diffuser and the
                // tank, so its "transient" is whatever is left of one. The hit
                // that should open the window is the one going IN.
                if (type == DR32_EFX_NONLIN) nonlin.run(inL, inR, l, r);
                io[2 * i] = l; io[2 * i + 1] = r;
            }
            return;
        }

        switch (type) {
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
                for (int i = 0; i < n; i++) {
                    float l = sl[i], r = sr[i];
                    spacesLP.run(l, r);
                    io[2 * i] = l; io[2 * i + 1] = r;
                }
                break;
            }
            case DR32_EFX_NATIVE: {
                // ⭑ Nothing in front of it any more. The placeholder diffuser
                // that stood here is gone: the device's own early-reflection
                // and pre-diffusion stages are inside the port now, and this
                // reverb takes a MONO input by construction (the kernel injects
                // L+R) with all of its stereo coming from the parity folds.
                // Adding a per-channel diffuser would have been decorating a
                // path that the real algorithm does not have.
                const float trim = nativeRawMode ? 1.0f : kNativeTrim;
                for (int i = 0; i < n; i++) {
                    float ol, orr;
                    native.tick(io[2 * i], io[2 * i + 1], ol, orr);
                    io[2 * i] = ol * trim;
                    io[2 * i + 1] = orr * trim;
                }
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
    float send_return[DR32_SEND_SLOTS] = { 1.0f, 1.0f };
    // The always-on Drum Bus over the summed mix. Neutral by default, and
    // BYPASSED while neutral — see bus_neutral.
    DrumBuss bus;
    bool  bus_neutral = true;
    float bus_mix = 1.0f;                  // dry/wet blend = parallel compression
    float bus_dry[2 * DR32_MAX_BLOCK];     // pre-bus copy, only filled when mix < 1
    // Per-block accumulation of what the pads sent to each bus.
    float send_buf[DR32_SEND_SLOTS][2 * DR32_MAX_BLOCK];
    // Blocks since anything was fed to each bus. A loaded-but-unused reverb
    // should cost nothing, but its tail must still ring out first.
    int   idle_blocks[DR32_SEND_SLOTS] = { 0, 0 };
    // De-interleave scratch, shared by every slot: the vendored algorithms are
    // block-based with separate L/R pointers. One copy, not one per slot.
    float scratch_l[DR32_MAX_BLOCK];
    float scratch_r[DR32_MAX_BLOCK];
};

extern "C" {

int dr32_efx_is_tank(dr32_efx_type t) {
    return Slot::spaceType(t) >= 0 ? 1 : 0;
}

dr32_fxbus *dr32_fxbus_create(float sample_rate) {
    dr32_fxbus *fx = new (std::nothrow) dr32_fxbus();
    if (!fx) return nullptr;
    fx->sample_rate = (sample_rate > 1.0f) ? sample_rate : 44100.0f;
    for (int i = 0; i < DR32_SEND_SLOTS; i++) fx->sends[i].setSampleRate(fx->sample_rate);
    fx->bus.setSampleRate(fx->sample_rate);
    // Neutral: Attack and Sustain are bipolar about 0 on this side of the API,
    // 0.5 inside DrumBuss. Fully wet, so Mix only ever takes the stage AWAY.
    dr32_fxbus_set_bus_params(fx, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
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

void dr32_fxbus_set_send_params(dr32_fxbus *fx, int slot, const float *p, int n) {
    if (!fx || !p || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    Slot &s = fx->sends[slot];
    if (n > DR32_SEND_PARAMS) n = DR32_SEND_PARAMS;
    for (int i = 0; i < n; i++) s.p[i] = p[i];
    s.apply();                    // a send return is ALWAYS 100% wet
}

void dr32_fxbus_set_bpm(dr32_fxbus *fx, float bpm) {
    if (!fx) return;
    // setBpm early-outs on an unchanged value, so this is a float compare per
    // block in the common case — cheap enough to call from render_block.
    for (int i = 0; i < DR32_SEND_SLOTS; i++) fx->sends[i].delay.setBpm(bpm);
}

void dr32_fxbus_set_bus_params(dr32_fxbus *fx, float compress, float crunch,
                               float attack, float sustain, float mix) {
    if (!fx) return;
    // Bipolar -1..+1 on the way in, 0..1 about 0.5 inside DrumBuss. This is the
    // ONE place that conversion lives.
    fx->bus.setParams(compress, crunch, 0.5f + 0.5f * attack, 0.5f + 0.5f * sustain);
    fx->bus_mix = mix < 0.0f ? 0.0f : (mix > 1.0f ? 1.0f : mix);
    // The bypass test, and the whole reason an always-on stage is acceptable.
    // The tolerances match DrumBuss's own atkOn/susOn gates (which are ±0.005 in
    // the 0..1 domain, so ±0.01 here) — the two must not disagree about what
    // neutral means. Deliberately NOT keyed on mix: at neutral the stage passes
    // its input through, so blending it against the dry is still the dry.
    fx->bus_neutral = (compress <= 0.001f) && (crunch <= 0.001f) &&
                      (fabsf(attack) <= 0.01f) && (fabsf(sustain) <= 0.01f);
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

        // ⚠ The idle skip is disabled in null-test mode: its output gate cuts
        // at 1e-6 (-120 dB), which would otherwise put a floor under the null
        // depth that has nothing to do with the model.
        if (slot.active() &&
            (slot.nativeRawMode || fx->idle_blocks[s] <= idle_limit)) {
            const float g = fx->send_return[s];   // return is fully wet by design
            slot.processBlock(buf, n, fx->scratch_l, fx->scratch_r);
            // The idle counter is armed by the INPUT, but held open by the
            // OUTPUT. A reverb dies well inside four seconds, so input alone was
            // enough; a Delay at 16 sixteenths with high feedback rings far
            // longer than that and would have been cut off mid-repeat — silence
            // between hits is exactly the state a delay is FOR. Anything still
            // making sound keeps its slot alive, whatever the algorithm.
            float outPeak = 0.0f;
            for (int i = 0; i < n; i++) {
                float l = buf[2 * i], r = buf[2 * i + 1];
                float a = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
                if (a > outPeak) outPeak = a;
                out[2 * i]     += l * g;
                out[2 * i + 1] += r * g;
            }
            if (outPeak > 1e-6f) fx->idle_blocks[s] = 0;
        }
        std::memset(buf, 0, sizeof(float) * 2 * (size_t)n);
    }

    // --- the always-on Drum Bus, over the summed mix (dry + both returns).
    // Bypassed entirely while neutral: not "runs and does nothing", actually
    // skipped, so the stage is bit-transparent and costs one bool test per
    // block for anyone who never opens the page.
    if (!fx->bus_neutral) {
        const float mix = fx->bus_mix;
        // Parallel path: keep the unprocessed mix only when it is actually
        // going to be blended back in. At mix = 1 this is a plain in-place run
        // and the copy never happens.
        if (mix < 0.999f) std::memcpy(fx->bus_dry, out, sizeof(float) * 2 * (size_t)n);
        fx->bus.processBlock(out, n, fx->scratch_l, fx->scratch_r);
        if (mix < 0.999f) {
            for (int i = 0; i < 2 * n; i++)
                out[i] = fx->bus_dry[i] + (out[i] - fx->bus_dry[i]) * mix;
        }
    }
}

void dr32_fxbus_reset(dr32_fxbus *fx) {
    if (!fx) return;
    for (int i = 0; i < DR32_SEND_SLOTS; i++) {
        fx->sends[i].reset();
        std::memset(fx->send_buf[i], 0, sizeof(fx->send_buf[i]));
    }
    // The bus holds envelope followers, a compressor and a DC blocker; a kit
    // change must not leave any of that pointing at the previous kit's level.
    fx->bus.reset();
}

void dr32_efx_defaults(dr32_efx_type type, float *o) {
    if (!o) return;
    // Slot table is in dr32_fxbus.h. Reverbs use 0-3; the Delay uses all eight.
    for (int i = 0; i < DR32_SEND_PARAMS; i++) o[i] = 0.0f;
    switch (type) {
        case DR32_EFX_PLATE:
            // Snare/clap plate: medium tank, a little damping so it is not
            // brittle, short-ish tail, ~10 ms pre-delay to keep the hit clear.
            o[0] = 0.45f; o[1] = 0.35f; o[2] = 0.45f; o[3] = 0.05f; break;
        case DR32_EFX_SPACES:
            // Lands as a natural mid-size room rather than at either extreme:
            // Spaces is the one control set for everything from a tight room to
            // a hall, so it should open somewhere you would actually start.
            o[0] = 0.35f; o[1] = 0.40f; o[2] = 0.45f; o[3] = 0.02f; break;
        case DR32_EFX_GATED:
            // ⚠ A TIGHT tank, not a big one — measured, against my own first
            // guess of 0.70. A gate needs something to gate, and the tank's
            // density build dominates the first ~180 ms: at size 0.70 the
            // window still RISES through the whole hold (+6 dB) and the type is
            // indistinguishable from NonLin sitting next to it. At size 0.20 it
            // peaks around 65 ms and then falls cleanly (+6.9 -> -10.4 dB by
            // 240 ms) — quick dense build, audible decay, then the chop, which
            // is the sound this is for.
            //
            // Slot 2 is the gate HOLD (50..500 ms), slot 4 the tank's OWN decay,
            // slot 5 the release. No pre-delay: the gate is the shape.
            o[0] = 0.20f; o[1] = 0.20f; o[2] = 0.45f; o[3] = 0.0f;
            o[4] = 0.30f; o[5] = 0.28f; break;
        case DR32_EFX_DIGITAL:
            // 80s rack: mid-size, fairly bright, a medium tail. Its 12-bit loop
            // grain and chorused modulation are internal and always on — that is
            // the sound, not a fault.
            //
            // ⚠ SpaceExtra's sibling LoFi type (the same FDN at fs/3 and 7 bits)
            // is deliberately NOT offered. Measured, its decay knob is dead:
            // RT60 spans 0.19 s to 0.26 s across the whole control, and raising
            // the bit depth 7 -> 11 only reaches 0.41 s, so the short tail is
            // structural to that voicing rather than a quantisation floor. The
            // effect is usable but the control is not, and a dead knob is the
            // defect this suite exists to catch. Revisit with its own pass.
            o[0] = 0.50f; o[1] = 0.45f; o[2] = 0.50f; o[3] = 0.03f; break;
        case DR32_EFX_HALL:
            // ⚠ Chamber reaches cathedral length at the top of its range and was
            // pulled from DR32 once for exactly that (07ca02c). Decay is scaled
            // to 0.62 in apply() and the default sits mid-knob; the RT60 test
            // holds it under 3 s.
            o[0] = 0.55f; o[1] = 0.45f; o[2] = 0.45f; o[3] = 0.04f; break;
        case DR32_EFX_NONLIN:
            // A dense bright tank — the window is doing the shaping, so the
            // reverb under it wants density rather than character. Slot 2 is the
            // window LENGTH (0.45 -> ~300 ms, the classic non-lin), slot 4 its
            // TILT, defaulting dead flat. No pre-delay: the point is that the
            // hit and the window start together.
            o[0] = 0.35f; o[1] = 0.25f; o[2] = 0.45f; o[3] = 0.0f; o[4] = 0.5f;
            o[5] = 0.07f; break;   // ~1.5 ms, the cliff this always had
        case DR32_EFX_NATIVE:
            // A stock drum-kit room, in the device's own numbers: RoomSize 60
            // (the value the factory kits' return chains carry), a ~1.5 s decay,
            // a little HF damping, and Diffusion at 0.6 — again the factory
            // kits' own value. Pre-delay 0: the native device's own PreDelay is
            // 0 in every kit return chain, and DR32's line stands in for it.
            o[0] = 0.55f; o[1] = 0.59f; o[2] = 0.42f; o[3] = 0.0f;
            o[4] = 0.46f; break;
        case DR32_EFX_DELAY:
            // The factory corpus's dominant configuration, not a guess: L = 1
            // sixteenth and R = 4 is what ten of the twelve native delay returns
            // use, feedback is their median (0.50 against a 0.12-0.73 spread),
            // and tone 0.55 puts the feedback bandpass at ~1.3 kHz, their median
            // centre. Ping-pong starts off — the library is split 7/5 on it, and
            // the L/R asymmetry already gives a stereo pattern without it.
            //
            // Synced by default (11 of the 12 native delay returns are), and the
            // free times are seeded with what the synced pair produces at
            // 120 BPM — so flipping Sync off does not move the delay, it just
            // stops it following the tempo.
            o[0] = 1.0f; o[1] = 4.0f; o[2] = 0.50f; o[3] = 0.55f; o[4] = 0.0f;
            o[5] = 1.0f; o[6] = 125.0f; o[7] = 500.0f; break;
        default:
            o[0] = 0.5f; o[1] = 0.3f; o[2] = 0.5f; o[3] = 0.0f; break;
    }
}

int dr32_fxbus_native_set_raw(dr32_fxbus *fx, int slot, const char *key, float value) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return -1;
    return (int)fx->sends[slot].native.setRaw(key, value);
}

void dr32_fxbus_native_raw_commit(dr32_fxbus *fx, int slot) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    fx->sends[slot].native.build();
    fx->sends[slot].native.reset();
    fx->sends[slot].nativeRawMode = true;
    // The generic pre-delay line must be out of the way: the device's PreDelay
    // is one of the raw parameters and it is modelled inside the port.
    fx->sends[slot].preLen = 0;
}

const char *dr32_efx_name(dr32_efx_type type) {
    switch (type) {
        case DR32_EFX_PLATE:  return "Plate";
        case DR32_EFX_SPACES: return "Spaces";
        case DR32_EFX_DELAY:  return "Delay";
        case DR32_EFX_GATED:  return "Gated";
        case DR32_EFX_DIGITAL:return "Digital";
        case DR32_EFX_HALL:   return "Hall";
        case DR32_EFX_NONLIN: return "NonLin";
        case DR32_EFX_NATIVE: return "Native";
        default:              return "Off";
    }
}

dr32_efx_type dr32_efx_from_name(const char *name) {
    if (!name || !*name) return DR32_EFX_NONE;
    if (!std::strcmp(name, "Plate")) return DR32_EFX_PLATE;
    if (!std::strcmp(name, "Spaces")) return DR32_EFX_SPACES;
    if (!std::strcmp(name, "Delay")) return DR32_EFX_DELAY;
    if (!std::strcmp(name, "Gated")) return DR32_EFX_GATED;
    if (!std::strcmp(name, "Digital")) return DR32_EFX_DIGITAL;
    if (!std::strcmp(name, "Hall")) return DR32_EFX_HALL;
    if (!std::strcmp(name, "NonLin")) return DR32_EFX_NONLIN;
    if (!std::strcmp(name, "Native")) return DR32_EFX_NATIVE;
    // Anything else, including a saved state naming the old "Drum Bus" send
    // type, falls through to Off.
    return DR32_EFX_NONE;
}

}  // extern "C"
