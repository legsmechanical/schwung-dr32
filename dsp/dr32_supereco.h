// dr32_supereco.h — a PORT of the late network of Move's own Reverb, SuperEco.
//
// ⚠ This is not a model, a fit, or an homage. Every constant and every law
// below is READ OFF the MoveOriginal binary (build move/release-v2.0.5b1) via
// the decompile in `../move original reconstruct/analysis/audio-effects/`.
// The acceptance test is a null test against the real engine, not an ear test.
// Anything that "sounds better" than what is written here is a fidelity bug.
//
// Why SuperEco and only SuperEco: `RoomType` is `ReverbQualityMode`, and all
// **466** Reverb instances in the entire CoreLibrary are SuperEco. Modelling
// SuperEco is modelling the whole device — there is no mode switching to do.
//
// Sources (read these before changing anything here):
//   REVERB_QUALITY_NETWORK_RECON.md   matrix, delay families, decay, shelves
//   REVERB_MAIN_KERNEL_TOPOLOGY.md    per-sample signal flow
//   extract_reverb_quality_tables.mjs re-emits every constant bit-exact
//   reverb-coalesced-rebuild.c        FUN_01b7cec0 / FUN_01b7d584 / FUN_01b7d9e4
//
// ── WHAT IS PORTED, AND WHAT IS NOT ──────────────────────────────────────────
//
// PORTED (all of it read off the binary):
//   LATE NETWORK
//   - the 4-lane orthonormal feedback matrix, exact float bits
//   - the three delay-length families A/B/C and the RoomSize cube-root law
//   - the decay law, including the stock clamped-exp2 approximation
//   - the per-lane TPT shelf pair, including the stock tan approximation
//   - the feedback all-pass recurrence and the stereo parity folds
//   FRONT END (REVERB_FRONT_END_RECON.md)
//   - the early-reflection tap-position law, its per-lane quadrature
//     oscillators, linear interpolation, and the decaying tap gains
//   - the four pre-diffusion delays and their orthonormal coefficient rows
//   - the two-state diffuser, whose five coefficients are DERIVED from the
//     last pre-diffusion gain rather than being constants
//
// NOT PORTED, and deliberately absent rather than guessed:
//   - the input Band filter's coefficient builder. Its recurrence is known but
//     the map from `BandFreq`/`BandWidth` to G/K/mixes is not, and it is an
//     INPUT tone control rather than part of the tail mechanism.
//   - the final mixer's exact form. `MixReflect`/`MixDiffuse` are applied here
//     as plain gains on the early and diffuse stereo pairs, which is the one
//     structural guess in this file — flagged, not hidden.
//   - room-delay modulation (`SizeModFreq`/`SizeModDepth` on the LATE lanes, as
//     distinct from the early-tap motion, which IS implemented).

#ifndef DR32_SUPERECO_H
#define DR32_SUPERECO_H

#include <cmath>
#include <cstring>
#include <cstdint>

namespace dr32_supereco {

// ── The stock transcendental approximations ─────────────────────────────────
//
// The device does NOT call expf/tanf. It uses its own approximations, and their
// quirks are part of the sound — most notably stockExp CANNOT RETURN MORE THAN
// 1.0, because the base-2 exponent is clamped to [0,127] and 127 is the unity
// exponent. A shelf gain above 1 is therefore silently flattened to unity.
//
// From FUN_01b7cec0 (reverb-coalesced-rebuild.c:718-795), which computes
//     127.0 - t*rate*8.833317*1.442695
// clamps it with fmax(.,0) then fmin(.,127.0), truncates to an int, and
// multiplies a degree-4 polynomial in the fraction by a BIT-CAST `i << 23`
// (i.e. 2^(i-127) — not an integer conversion, which is what it looks like in
// the decompiler output).

/** exp(x) as the device computes it. Exact to the binary, quirks included. */
inline float stockExp(float x) {
    float e = 127.0f + x * 1.442695f;              // 1/ln(2), as spelled there
    if (e < 0.0f) e = 0.0f;
    if (e > 127.0f) e = 127.0f;                    // ⚠ hard unity ceiling
    const int   i = (int)e;                        // truncation, not floor
    const float f = e - (float)i;
    const float poly =
        1.0f + (((f * 0.013487903f + 0.0521745f) * f + 0.24128748f) * f
                + 0.6930501f) * f;
    uint32_t bits = (uint32_t)i << 23;             // 2^(i-127)
    float scale;
    std::memcpy(&scale, &bits, sizeof(scale));
    return poly * scale;
}

/** The stock rational tangent, evaluated at an angle the caller has already
 *  halved and clamped. The Band builder needs this form; the shelf builder uses
 *  the wrapper below. */
inline float stockTanHalfRaw(float x) {
    const float x2 = x * x;
    return x * (0.999999463558197f - 0.09652461111545563f * x2) /
           (1.0f + (-0.4298672676086426f + 0.009981878101825714f * x2) * x2);
}

/** tan(w/2) as the device computes it, `w` being an angular frequency.
 *  The clamp is the device's, and it is what keeps the shelves stable as the
 *  corner frequency approaches Nyquist. */
inline float stockTanHalf(float w) {
    const float kClamp = 3.1337387561798096f;
    return stockTanHalfRaw(0.5f * (w < kClamp ? w : kClamp));
}

// ── The recovered constants ─────────────────────────────────────────────────
//
// 4 lanes: N = 2*RoomType + 4, and SuperEco is RoomType 0.
static const int kLanes = 4;

/** The orthonormal feedback diffusion matrix. Row norms are within 1.24e-6 of
 *  unity and off-diagonal row dots within 9.08e-7, which is what identifies it
 *  as energy preserving — the decay comes from the gains, never from here.
 *  Exact float bits in the comments; regenerate with
 *  extract_reverb_quality_tables.mjs. */
static const float kMatrix[kLanes][kLanes] = {
    { -0.077292003f /*0xbd9e4b45*/,  0.418047011f /*0x3ed60a42*/,
       0.871778011f /*0x3f5f2cd8*/,  0.243444994f /*0x3e7949a5*/ },
    { -0.633005977f /*0xbf220cae*/,  0.230260998f /*0x3e6bc98a*/,
      -0.348545998f /*0xbeb2749f*/,  0.651766002f /*0x3f26da23*/ },
    { -0.334378988f /*0xbeab33b9*/,  0.663406014f /*0x3f29d4fa*/,
      -0.166734993f /*0xbe2abc94*/, -0.648293018f /*0xbf25f688*/ },
    {  0.693916023f /*0x3f31a47b*/,  0.576290011f /*0x3f1387be*/,
      -0.301192999f /*0xbe9a35f8*/,  0.309278011f /*0x3e9e59b0*/ },
};

// The three delay-length families, in milliseconds before scaling. SuperEco
// takes slice [150..153] of the retained constant banks; Mid and High are
// supersets of the same region, which is why these four values reappear at the
// END of their tables.
static const float kFamA[kLanes] = { 29.929510f, 47.124821f, 99.554916f, 37.206055f };
static const float kFamB[kLanes] = { 33.346848f, 31.977379f, 46.707878f, 20.798279f };
static const float kFamC[kLanes] = { 14.690815f, 26.195496f,  7.875598f, 11.744622f };

/** The four-lane specialisation of the builder's scatter order. Six lanes and
 *  up use 0,5,1,2,3,4,6,7,8,9 instead. */
static const int kScatter[kLanes] = { 0, 3, 1, 2 };

// Decay: feedbackGain[i] = exp(-8.833317 * pathTime[i] / (1.15 * DecayTime)).
// ⭑ The 1.15 and the 8.833317 together mean RT60 = ln(1000)*1.15/8.833317 =
// 0.8993 * DecayTime, INDEPENDENT of path time — which is why applying one
// gain per delay element (below) and not one per loop is not just allowable but
// required: a traversal accumulates exp(-k*(tA+tB+tC)) either way, and the
// binary really does build several per-family gain vectors
// (reverb-coalesced-rebuild.c writes 0x5b0/0x5c0 and 0x7c0/0x7c8 from two
// different path-time arrays).
static const float kDecayNumer = 8.833317f;
static const float kDecayDenom = 1.15f;

/** RT60 as a multiple of DecayTime, implied by the two constants above.
 *  Measured on device at DecayTime 1500 ms: 1.33 s against 1.35 predicted. */
static const float kRt60PerDecayTime = 0.8993f;

// Buffer sizing. The stock presets reach RoomSize 500 and DR32 must be able to
// null-test against them, so the lines are sized for that at 48 kHz:
//   tankScale = 0.93 * cuberoot(500) = 7.381
//   lengthA = 48 * 7.381 * 0.500 * 99.5549 = 17634 samples
// AllPassSize maxes at 1.0 in the stock corpus, so B and C size off that.
static const int kMaxA = 18432;
static const int kMaxB = 13312;
static const int kMaxC = 4096;

/** Plain circular delay with an integer length. Integer, not fractional,
 *  because modulation is off (see the header note) and the builder rounds. */
template <int CAP>
struct Line {
    float buf[CAP];
    int   w = 0, len = 1;

    void reset() { std::memset(buf, 0, sizeof(buf)); w = 0; }
    void setLength(int n) {
        if (n < 1) n = 1;
        if (n > CAP - 1) n = CAP - 1;
        len = n;
    }
    /** Read the delayed sample without advancing. The all-passes need this
     *  separately from the write, because what they write depends on what they
     *  read. */
    inline float peek() const {
        int r = w - len; if (r < 0) r += CAP;
        return buf[r];
    }
    inline void push(float x) {
        buf[w] = x;
        if (++w >= CAP) w = 0;
    }
    /** Read `n` samples back from the write head. The early ring is ONE buffer
     *  with four independently-moving tap pairs, so it needs an arbitrary read
     *  rather than a single fixed length. */
    inline float at(int n) const {
        int r = w - 1 - n; while (r < 0) r += CAP;
        return buf[r];
    }
    /** Read at a fractional delay, then write. Linear interpolation, which is
     *  what the kernel does: one rounded subtraction and one fused
     *  multiply-add. */
    inline float stepFrac(float x, float delay) {
        if (delay < 1.0f) delay = 1.0f;
        if (delay > (float)(CAP - 2)) delay = (float)(CAP - 2);
        const int   i = (int)delay;
        const float f = delay - (float)i;
        int r0 = w - i;     if (r0 < 0) r0 += CAP;
        int r1 = w - i - 1; if (r1 < 0) r1 += CAP;
        const float a = buf[r0], b = buf[r1];
        const float y = a + f * (b - a);
        push(x);
        return y;
    }

    /** Read the delayed sample, then write the new one. */
    inline float step(float x) {
        const float y = peek();
        push(x);
        return y;
    }
};

// ── Front-end constants (REVERB_FRONT_END_RECON.md) ─────────────────────────

/** Early-reflection tap shape, and the fixed sign/trim on each lane. ⚠ Lane 2
 *  is NEGATIVE — that sign is the reverb's, and dropping it as "just a phase"
 *  collapses part of the early stereo image. */
static const float kEarlyShape[kLanes] =
    { 0.8481000066f, 1.0f, 0.6955000162f, 0.4178999960f };
static const float kEarlyTrim[kLanes] = { 1.0f, 1.0f, -1.0f, 1.0f };

/** Per-lane quadrature oscillator that moves the early taps: detune ratios and
 *  the exact initial (x,y). The seeds are cos/sin of 10/20/30/40 degrees, which
 *  is what decorrelates the four lanes' motion. */
static const float kEarlyDetune[kLanes] =
    { 1.0149999857f, 0.9916999936f, 1.0230000019f, 0.9897000194f };
static const float kEarlyOscX[kLanes] =
    { 0.9848077297f, 0.9396926165f, 0.8660253882f, 0.7660444379f };
static const float kEarlyOscY[kLanes] =
    { 0.1736481935f, 0.3420201540f, 0.5f, 0.6427876353f };

/** Pre-diffusion delay shape, and the orthonormal rows that feed the four
 *  delays from the four early lanes. Only the first four of each row's twelve
 *  floats are active in SuperEco; the rest are exact zero. */
static const float kPdShape[kLanes] =
    { 0.9010000229f, 0.6599000096f, 0.8421999812f, 0.4629999995f };
static const float kPdRow[kLanes][kLanes] = {
    { -0.048771999f /*0xbd47c526*/, -0.321126014f /*0xbea46aa1*/,
      -0.911998987f /*0xbf6978c4*/,  0.250515997f /*0x3e8043a2*/ },
    { -0.075773001f /*0xbd9b2ee0*/, -0.615700006f /*0xbf1d9e84*/,
       0.405299991f /*0x3ecf837b*/,  0.671494007f /*0x3f2be708*/ },
    {  0.477892011f /*0x3ef4ae43*/, -0.658955991f /*0xbf28b157*/,
       0.047448002f /*0x3d4258d6*/, -0.578917027f /*0xbf1433e8*/ },
    {  0.873784006f /*0x3f5fb04f*/,  0.289081007f /*0x3e94026d*/,
      -0.041708998f /*0xbd2ad70e*/,  0.388835996f /*0x3ec71583*/ },
};

// Ring capacities. The early ring's own allocation law is fs*0.001*984.1425,
// i.e. ~0.98 s; the largest tap actually reachable is roomSpan(RoomSize 500)
// plus PreDelay, about 14.2k samples at 48 kHz with a 200 ms pre-delay.
static const int kMaxEarly = 24576;
static const int kMaxPd    = 8192;

/** The TPT state-variable stage both shelves are built from. `G`, `K`, and the
 *  three output mixes come from the builder; the recurrence is transcribed
 *  from REVERB_MAIN_KERNEL_TOPOLOGY.md §1 (the Band filter and both shelves
 *  share it exactly). */
struct Svf {
    float g = 0.0f, k = 0.0f, a = 0.0f;
    float cLP = 0.0f, cBP = 0.0f, cHP = 1.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }
    void set(float G, float K, float lp, float bp, float hp) {
        g = 0.5f * G;
        k = K;
        a = g / (1.0f + g * (g + K));
        cLP = lp; cBP = bp; cHP = hp;
    }
    inline float run(float x) {
        const float d1 = a * ((x - z2) - (g + k) * z1);
        const float d2 = a * (z1 + g * (x - z2));
        const float v1 = z1 + d1;
        const float v2 = z2 + d2;
        const float hp = (x - k * v1) - v2;
        z1 += 2.0f * d1;
        z2 += 2.0f * d2;
        return cLP * v2 + cBP * v1 + cHP * hp;
    }
};

/** The shelf builder's shared K. One constant, one value, straight off the
 *  binary — do not "tidy" it to sqrt(2). */
static const float kShelfK = 1.4357497692108154f;

/** SuperEco's late network: four lanes, each a room delay, a shelf pair, and
 *  two feedback all-passes, tied together by the orthonormal matrix.
 *
 *  ⚠ Family C is placed INSIDE the feedback loop, as a second all-pass. That
 *  follows the shelf builder, which computes each lane's loop time as the sum
 *  of all THREE families (REVERB_QUALITY_NETWORK_RECON.md, "the lane loop time
 *  is also explicit"), read off the raw AArch64 listing. The topology note
 *  instead reads family C's delays as a pre-diffusion stage OUTSIDE the loop —
 *  the two notes use different offset bases and cannot both be right, and
 *  resolving that is Job 0/3 of the front-end handoff. `familyCInLoop` flips
 *  it so the answer can be tested rather than argued. */
struct SuperEco {
    float fs = 44100.0f;

    // Public parameters, in the device's own units and names.
    float roomSize    = 60.0f;      // 0.27 .. 500 across the stock presets
    float decayTime   = 1.5f;       // SECONDS (the device stores milliseconds)
    float allPassGain = 0.6f;       // "Diffusion"
    float allPassSize = 0.77f;      // "Scale"
    float shelfLoFreq = 670.77f, shelfLoGain = 0.6167f;
    float shelfHiFreq = 1469.95f, shelfHiGain = 0.8833f;
    bool  shelfLowOn = true, shelfHighOn = true;
    bool  freeze      = false;      // decayRate 0 -> every gain unity
    // ⭑ Settled by REVERB_FRONT_END_RECON.md §1: families A/B/C all come from
    // FUN_01b7cec0, which works on the LATE subobject (full_state + 0xa90), and
    // the pre-diffusion delays are a separate front-end stage with their own
    // length law. So family C really is a second in-loop all-pass. The flag is
    // kept only so the alternative stays one edit away.
    bool  familyCInLoop = true;
    /** WHERE the per-lane shelf pair sits. 0 = after the room delay (default),
     *  1 = on the residual, 2 = inside the all-pass chain.
     *
     *  ⚠ 0 and 1 measure IDENTICALLY — the outer loop is linear, so moving a
     *  filter around inside it changes nothing. 2 is much worse, because
     *  putting a filter inside an all-pass destroys the all-pass property.
     *
     *  Kept because it was the first thing suspected for the OPEN low-band
     *  discrepancy below, and ruling it out is worth keeping ruled out. */
    int   shelfPos = 0;

    // ⚠ ChorusOn gates LATE modulation; SpinOn gates the EARLY taps. They are
    // not interchangeable and the names invite exactly that mistake.
    bool  chorusOn     = true;
    bool  cutOn        = false;   // only meaningful while frozen
    float sizeModFreq  = 2.3728f;   // Hz
    float sizeModDepth = 0.1558f;   // percent; x0.01 becomes the depth
    // Input Band filter. Every stock preset has at least one side on.
    float bandFreq  = 402.48f;      // Hz
    float bandWidth = 4.5729f;      // OCTAVES: before clamping, fhi/flo = 2^W
    bool  bandLowOn = true, bandHighOn = true;
    // Final mixer.
    float mixDirect  = 1.0f;        // equal-power dry/wet, 0..1
    float stereoSeparation = 107.62f;   // 0..120, a WIDTH control
    // A send bus is 100% wet, so the direct path is off unless the null test
    // turns it on — the device's own render contains it.
    bool  includeDirect = false;
    // 0 = None (eight-sample quantised delays), 1 = Slow, 2 = Fast.
    int   sizeSmoothing = 2;

    // Front end. Defaults are the values the factory drum kits' return chains
    // actually carry, so an untouched instance is a stock drum room.
    float preDelayMs   = 0.0f;
    float diffuseDelay = 0.2143f;   // "Shape" — early-reflection spread
    float earlyModFreq = 0.1094f;   // Hz
    float earlyModDepth = 3.2429f;  // public units; 0.01x becomes the depth
    bool  spinOn       = true;
    float mixReflect   = 1.7925f;   // ⚠ see the header: plain gains here
    float mixDiffuse   = 1.9953f;

    // Front-end state. ONE early ring with four moving tap pairs (that is the
    // device's arrangement — not four rings), then four pre-diffusion delays.
    // Input Band filter: an upper-cutoff low-pass and a lower-cutoff high-pass
    // in series. ⚠ The ON switches do not bypass — a disabled BandHighOn leaves
    // the high-pass at 50 Hz and a disabled BandLowOn leaves the low-pass at
    // 18 kHz, which is a filter, not an absence of one.
    Svf bandLp, bandHp;

    // The final mixer's two stereo rings. Fixed capacities, not sized from
    // StereoSeparation.
    static const int kMaxMixLate  = 4096;   // floor(fs * 0.0735)
    static const int kMaxMixEarly = 1024;   // floor(fs * 0.01155)
    float mixLateBuf[2 * kMaxMixLate], mixEarlyBuf[2 * kMaxMixEarly];
    int   mixLateW = 0, mixEarlyW = 0, mixLateCap = 1, mixEarlyCap = 1;
    int   mixLatePos = 0, mixEarlyPos = 0;
    float cDry = 1.0f, cRefl = 0.0f, cLate = 0.0f, cSwap = 0.0f, cSame = 1.0f;

    // Late Size modulation. ⭑ SuperEco modulates ONE lane — lane 0. Lanes 1-3
    // are fixed integer reads. (Eco/Mid/High add a second oscillator on lane 5;
    // every other lane stays fixed at every quality.)
    float lateOscX = 1.0f, lateOscY = 0.0f, lateOscK = 0.0f, lateDepth = 0.0f;

    Line<kMaxEarly> early;
    Line<kMaxPd>    pd[kLanes];
    float oscX[kLanes], oscY[kLanes], oscK[kLanes];
    int   earlyBase[kLanes];        // unmodulated tap position, in samples
    float earlyGain[kLanes];
    int   tapOff[kLanes];           // live, modulated
    float tapW[kLanes];
    int   ctrlPhase = 0;            // the updater runs once every 8 samples
    float pdRow[kLanes][kLanes];    // base rows scaled by their delay's gain
    float dfA = 0.0f, dfB = 0.0f, dfC = 0.0f, dfD = 0.0f, dfE = 0.0f;
    float dfZ0[kLanes], dfZ1[kLanes];

    Line<kMaxA> lineA[kLanes];
    Line<kMaxB> lineB[kLanes];
    Line<kMaxC> lineC[kLanes];
    Svf   shelfLo[kLanes], shelfHi[kLanes];
    float gainA[kLanes], gainB[kLanes], gainC[kLanes];
    float resid[kLanes];            // R, the previous all-pass residual

    SuperEco() { reset(); build(); }

    void reset() {
        early.reset();
        for (int j = 0; j < kLanes; j++) {
            lineA[j].reset(); lineB[j].reset(); lineC[j].reset();
            pd[j].reset();
            shelfLo[j].reset(); shelfHi[j].reset();
            resid[j] = 0.0f;
            dfZ0[j] = dfZ1[j] = 0.0f;
            // The oscillators are seeded, not zeroed: their (x,y) pairs are
            // cos/sin of 10/20/30/40 degrees, which is what decorrelates the
            // four lanes' tap motion. Zeroing them would stop all four dead.
            oscX[j] = kEarlyOscX[j];
            oscY[j] = kEarlyOscY[j];
        }
        ctrlPhase = 0;
        bandLp.reset(); bandHp.reset();
        std::memset(mixLateBuf, 0, sizeof(mixLateBuf));
        std::memset(mixEarlyBuf, 0, sizeof(mixEarlyBuf));
        mixLateW = mixEarlyW = 0;
        // Late oscillator 0's seed is (1,0) — a different pair from the early
        // oscillators, and it advances BEFORE the first read, so the first
        // sample sees y = k rather than the seed's 0.
        lateOscX = 1.0f; lateOscY = 0.0f;
    }

    void setSampleRate(float sr) {
        fs = (sr > 1.0f) ? sr : 44100.0f;
        reset();
        build();
    }

    /** Recompute every table. Cheap enough to call on any knob move — this is
     *  what the device's own rebuild functions do. */
    void build() {
        // A shelf that is switched off is a shelf at unity gain: its Alo/Ahi
        // become 1, the fourth roots and square roots become 1, and the SVF
        // collapses to a flat mix. No special case in the per-sample path.
        const float loG = shelfLowOn  ? shelfLoGain : 1.0f;
        const float hiG = shelfHighOn ? shelfHiGain : 1.0f;

        // Delay lengths. RoomSize enters as a CUBE ROOT, which is the single
        // least guessable thing in the whole reverb and the reason a fitted
        // model never tracked it: doubling the room lengthens the tank by 26%.
        const float r0 = std::cbrt(roomSize > 0.0f ? roomSize : 0.0f);
        const float r  = (r0 > 0.0001f) ? r0 : 0.0001f;
        const float tankScale = 0.93f * r;
        const float msToSamples = fs * 0.001f;
        const float aps = allPassSize;

        // decayRate = 1/(1.15*DecayTime); Freeze sets it to zero, which makes
        // every gain exactly unity and turns the orthonormal matrix into a
        // sustaining, energy-preserving network. That is the whole mechanism.
        const float decayRate =
            freeze ? 0.0f
                   : 1.0f / (kDecayDenom * (decayTime > 1e-4f ? decayTime : 1e-4f));

        for (int j = 0; j < kLanes; j++) {
            // The builder scatters the computed lengths into lane order.
            const int s = kScatter[j];
            const float lenA = msToSamples * tankScale * 0.500f * kFamA[s];
            const float lenB = msToSamples * tankScale * aps * 0.750f * kFamB[s];
            const float lenC = msToSamples * tankScale * aps * 0.395f * kFamC[s];

            lineA[j].setLength((int)(lenA + 0.5f));
            lineB[j].setLength((int)(lenB + 0.5f));
            lineC[j].setLength((int)(lenC + 0.5f));

            // One decay gain per delay element, so a full traversal accumulates
            // exp(-k*(tA+tB+tC)) — see kDecayNumer above for why that is the
            // faithful arrangement and not a convenience.
            const float tA = (float)lineA[j].len / fs;
            const float tB = (float)lineB[j].len / fs;
            const float tC = (float)lineC[j].len / fs;
            gainA[j] = stockExp(-kDecayNumer * tA * decayRate);
            gainB[j] = stockExp(-kDecayNumer * tB * decayRate);
            gainC[j] = stockExp(-kDecayNumer * tC * decayRate);

            // Shelves. The builder feeds each lane its COMPLETE loop time, so a
            // long lane is shelved harder than a short one — the shelf is a
            // per-round-trip loss, not a fixed filter.
            const float t = tA + tB + (familyCInLoop ? tC : 0.0f);
            const float Alo = stockExp(6.0f * t * std::log(clampGain(loG)));
            const float Ahi = stockExp(6.0f * t * std::log(clampGain(hiG)));
            const float Tlo = 2.0f * stockTanHalf(2.0f * 3.14159265358979f * shelfLoFreq / fs);
            const float Thi = 2.0f * stockTanHalf(2.0f * 3.14159265358979f * shelfHiFreq / fs);
            const float qlo = std::sqrt(std::sqrt(Alo));      // fourth root
            const float qhi = std::sqrt(std::sqrt(Ahi));

            shelfLo[j].set(Tlo / qlo, kShelfK, Alo, kShelfK * std::sqrt(Alo), 1.0f);
            shelfHi[j].set(Thi * qhi, kShelfK, 1.0f, kShelfK * std::sqrt(Ahi), Ahi);
        }

        buildFrontEnd(r, decayRate);
        buildBand();
        buildLateMod();
        buildMixer();
    }

    /** The input Band filter (FUN_01b7a1a8). One BandFreq and a WIDTH IN
     *  OCTAVES: before clamping, fhi/flo is exactly 2^BandWidth. */
    void buildBand() {
        const float R = stockExp(bandWidth * 0.5f);
        const float flo = bandHighOn ? std::fmax(bandFreq / R, 50.0f) : 50.0f;
        const float tmp = bandFreq * R;
        const float fhi = (bandLowOn && tmp <= 18000.0f) ? tmp : 18000.0f;

        const float kClamp = 3.1337387561798096f;
        float phiLo = 2.0f * 3.14159265358979f * flo / fs;
        float phiHi = 2.0f * 3.14159265358979f * fhi / fs;
        if (phiLo > kClamp) phiLo = kClamp;
        if (phiHi > kClamp) phiHi = kClamp;
        // The stock code evaluates stockTan twice and adds, rather than
        // doubling. Transcribed as written — it is a port.
        const float Ghp = stockTanHalfRaw(phiLo * 0.5f) + stockTanHalfRaw(phiLo * 0.5f);
        const float Glp = stockTanHalfRaw(phiHi * 0.5f) + stockTanHalfRaw(phiHi * 0.5f);

        bandLp.set(Glp, 1.4144271612167358f, 1.0f, 0.0f, 0.0f);
        bandHp.set(Ghp, 2.857142925262451f,  0.0f, 0.0f, 1.0f);
    }

    /** Late Size modulation. Advances EVERY sample, unlike the early taps. */
    void buildLateMod() {
        const float q = (sizeModFreq * 1.0149999856948853f) / fs;
        lateOscK = 2.0f * std::sin(q * 3.14159265358979f);
        lateDepth = sizeModDepth * 0.01f;
    }

    /** The final mixer (FUN_01b7ebc0's builder).
     *
     *  ⚠ Two things here look like transcription errors and are not.
     *
     *  1. `MixReflect` scales the EARLY+diffuse composite ring, and
     *     `MixDiffuse` scales the LATE ring. That is literally which
     *     coefficient lands on which tap.
     *  2. The builder divides a value already in SECONDS by 60000 — the public
     *     milliseconds were divided by 1000 on the way in. Stock behaviour, not
     *     a unit slip on this side. */
    void buildMixer() {
        const float rr = roomSize / 500.0f;
        const float tt = decayTime / 60000.0f;

        const float dscale = ((rr + 1.2f) * 0.387f) / (tt + 2.25f);
        const float rscale = ((rr + 2.5f) * 0.192f) / (tt + 2.5f);

        // MixDirect is an EQUAL-POWER dry/wet, not a linear blend.
        const float angle = mixDirect * 1.5707963705062866f;
        cDry = std::cos(angle);
        const float wetSin = std::sin(angle);

        cRefl = wetSin * (((rscale * mixReflect) / 1.995300054550171f) * 0.44999998807907104f);
        cLate = wetSin * ((mixDiffuse * dscale) / 1.995300054550171f);

        // Width. 45 degrees at S=0 is a mono wet sum; 90 degrees at S=120
        // passes the pair with no cross-channel term.
        const float degrees = stereoSeparation * 0.375f + 45.0f;
        const float radians = (degrees * 3.14159265358979f) / 180.0f;
        cSwap = std::cos(radians);
        cSame = std::sin(radians);

        // Ring positions. DiffuseDelay drives the LATE ring; StereoSeparation
        // drives the early/diffuse one.
        float rawA = (diffuseDelay * 0.03500000014901161f) * fs;
        float rawB = ((((120.0f - stereoSeparation) / 120.0f) * 0.9900000095367432f)
                      * 0.0006300000241026282f) * fs;
        if (sizeSmoothing == 0) { rawA = quantise8(rawA); rawB = quantise8(rawB); }

        mixLateCap  = (int)(fs * 0.07349999994039536f);
        mixEarlyCap = (int)(fs * 0.011549999937415123f);
        if (mixLateCap  > kMaxMixLate)  mixLateCap  = kMaxMixLate;
        if (mixEarlyCap > kMaxMixEarly) mixEarlyCap = kMaxMixEarly;
        if (mixLateCap  < 2) mixLateCap  = 2;
        if (mixEarlyCap < 2) mixEarlyCap = 2;

        // The reader truncates and does ONE direct ring load. It does not
        // interpolate — do not "improve" this.
        mixLatePos  = clampPos((int)rawA, mixLateCap);
        mixEarlyPos = clampPos((int)rawB, mixEarlyCap);
    }

    static int clampPos(int p, int cap) {
        if (p < 0) p = 0;
        if (p > cap - 1) p = cap - 1;
        return p;
    }
    /** The shared eight-sample quantiser, used when SizeSmoothing is None. */
    static float quantise8(float raw) {
        return (float)std::floor((raw + 7.1f) * 0.125f) * 8.0f + 0.01f;
    }

    /** The early-reflection and pre-diffusion tables.
     *
     *  ⚠ The front end runs on its OWN decay rate, not the late network's:
     *  `DiffuseDelay` (the manual's "Shape") enters it as `(DiffuseDelay+0.5)`
     *  and nowhere else. It is not a second copy of the tail's decay. */
    void buildFrontEnd(float r, float decayRate) {
        const float preS = preDelayMs * 0.001f;
        const float frontDecayRate = (diffuseDelay + 0.5f) *
                                     (decayRate <= 0.0f ? 0.0f : decayRate);

        // Early taps. The order of operations here follows the AArch64
        // fmul/fadd sequence: the algebraically shorter roomSpan*(shape-0.4179)
        // rounds differently, and this is a port.
        const float roomScale = 0.33f * r;
        const float roomSpan  = (roomScale * 62.41f) * 0.001f;
        const float origin    = preS + roomSpan * -0.4179f;
        for (int j = 0; j < kLanes; j++) {
            const float raw = ((kEarlyShape[j] * roomSpan + origin) * fs) + 0.5f;
            int b = (int)raw;                       // truncation after the +0.5
            if (b < 1) b = 1;
            if (b > kMaxEarly - 2) b = kMaxEarly - 2;
            earlyBase[j] = b;
            tapOff[j] = b - 1;
            tapW[j] = 0.0f;
            // ⚠ The tap gains DECAY, and they are compensated for pre-delay:
            // a tap that arrives later has had longer to fall, except for the
            // part of its lateness that is just pre-delay.
            earlyGain[j] = kEarlyTrim[j] *
                stockExp(-kDecayNumer * frontDecayRate *
                         ((float)b / fs - 0.83f * preS));
            // Control-rate quadrature oscillator, one per lane.
            const float ctrlFs = fs / 8.0f;
            const float q = earlyModFreq * kEarlyDetune[j] / ctrlFs;
            oscK[j] = 2.0f * std::sin(3.14159265358979f * q);
        }

        // Pre-diffusion delays, and their rows scaled by each delay's own gain.
        const float pdScale = 0.54f * r;
        const float pdBase  = ((pdScale * 32.036003f) * fs) * 0.001f;
        for (int j = 0; j < kLanes; j++) {
            const float pdLen = pdBase * kPdShape[j];
            int n = (int)pdLen;
            if (n < 1) n = 1;
            if (n > kMaxPd - 1) n = kMaxPd - 1;
            pd[j].setLength(n);
            const float g = stockExp(-kDecayNumer * frontDecayRate * pdLen / fs);
            for (int k = 0; k < kLanes; k++) pdRow[j][k] = kPdRow[j][k] * g;
            if (j == kLanes - 1) {
                // ⭑ The two-state diffuser's five coefficients are DERIVED,
                // not constants: a fixed-Q lowpass whose cutoff is 12 kHz
                // scaled by the LAST pre-diffusion gain. So the diffuser opens
                // and closes with DecayTime and DiffuseDelay.
                const float cutoff = 12000.0f * g;
                const float G  = 2.0f * stockTanHalf(2.0f * 3.14159265358979f *
                                                     cutoff / fs);
                const float G2 = G * G;
                const float den = G2 + 4.0f * G + 4.0f;
                dfA = dfC = G2 / den;
                dfB = (2.0f * G2) / den;
                dfD = (2.0f * (G2 - 4.0f)) / den;
                dfE = (G2 - 4.0f * G + 4.0f) / den;
            }
        }
    }

    /** The eight-sample control update: move the taps. */
    inline void updateTaps() {
        const float preS = preDelayMs * 0.001f;
        float depth = 0.01f * earlyModDepth;
        if (preS > 0.005f) depth = depth / (1.0f + 500.0f * (preS - 0.005f));
        const float spin = spinOn ? 1.0f : 0.0f;
        for (int j = 0; j < kLanes; j++) {
            // ⚠ SIGN: x - k*y, not k*y - x. The first transcription had it
            // backwards; the AArch64 is an `fmsub`, which negates the PRODUCT.
            const float x1 = oscX[j] - oscK[j] * oscY[j];
            const float y1 = oscY[j] + oscK[j] * x1;
            oscX[j] = x1; oscY[j] = y1;
            const float p = (float)earlyBase[j] * (1.0f + depth * spin * y1);
            int t = (int)p;
            if (t < 1) t = 1;
            if (t > kMaxEarly - 2) t = kMaxEarly - 2;
            tapOff[j] = t - 1;
            tapW[j] = p - (float)((int)p);
        }
    }

    /** One stereo frame.
     *
     *  ⚠ The input is MONO — the kernel injects `L+R` and everything stereo
     *  about this reverb comes out of the parity folds, not out of two
     *  independent channels. Feeding it a stereo pair and expecting the sides
     *  to stay separate is a misreading of the topology. */
    inline void tick(float inL, float inR, float &outL, float &outR) {
        // ⚠ Freeze+Cut together zero the input. Freeze alone keeps accepting
        // it, and Cut outside Freeze does nothing at all.
        float x = (freeze && cutOn) ? 0.0f : (inL + inR);
        x = bandHp.run(bandLp.run(x));

        // ── Early reflections ──────────────────────────────────────────────
        // ⚠ The updater is called before every tap read but ADVANCES only when
        // the incremented counter reaches eight. So samples 1..7 after a reset
        // use the seed positions and sample 8 is the first to move — an
        // off-by-one that matters for a sample-exact null and for nothing else.
        if (++ctrlPhase >= 8) { ctrlPhase = 0; updateTaps(); }

        early.push(x);
        float E[kLanes];
        for (int j = 0; j < kLanes; j++) {
            const float a = early.at(tapOff[j]);
            const float b = early.at(tapOff[j] + 1);
            // Two-point LINEAR interpolation. Confirmed from the fmls/fmla
            // instruction pair — not cubic, whatever "nicer" would suggest.
            E[j] = 1.5f * earlyGain[j] * (a + (b - a) * tapW[j]);
        }

        // ── Pre-diffusion and the two-state diffuser ───────────────────────
        float X[kLanes];
        for (int j = 0; j < kLanes; j++) {
            float acc = 0.0f;
            for (int k = 0; k < kLanes; k++) acc += pdRow[j][k] * E[k];
            X[j] = pd[j].step(acc);
        }
        float D[kLanes];
        for (int j = 0; j < kLanes; j++) {
            const float d = dfZ0[j] + dfA * X[j];
            dfZ0[j] = dfZ1[j] + dfB * X[j] - dfD * d;
            dfZ1[j] =           dfC * X[j] - dfE * d;
            D[j] = d;
        }

        // ── The late network ───────────────────────────────────────────────
        float U[kLanes];
        for (int j = 0; j < kLanes; j++) U[j] = E[j] + D[j] + resid[j];

        // W = M * U. Orthonormal, so this redistributes energy without adding
        // or removing any.
        float W[kLanes];
        for (int j = 0; j < kLanes; j++) {
            float acc = 0.0f;
            for (int k = 0; k < kLanes; k++) acc += kMatrix[j][k] * U[k];
            W[j] = acc;
        }

        // ⭑ Lane 0's room delay is read at a MODULATED fractional position;
        // lanes 1-3 are plain integer reads. The oscillator advances before the
        // read, every sample — not at the early block's eight-sample rate.
        const float lx = lateOscX - lateOscK * lateOscY;
        const float ly = lateOscY + lateOscK * lx;
        lateOscX = lx; lateOscY = ly;
        const float mod = 1.0f + (chorusOn ? 1.0f : 0.0f) * (ly * lateDepth);

        const float g = allPassGain;
        for (int j = 0; j < kLanes; j++) {
            // Room delay, then the shelf pair: one loss per round trip.
            float y = gainA[j] * (j == 0 ? lineA[0].stepFrac(W[0], (float)lineA[0].len * mod)
                                         : lineA[j].step(W[j]));
            if (shelfPos == 0) { y = shelfLo[j].run(y); y = shelfHi[j].run(y); }

            // Feedback all-pass, exactly as the kernel spells it: the decay
            // gain multiplies the all-pass READ, and the residual is both the
            // audible output and the feedback term.
            float qb = gainB[j] * lineB[j].peek();
            if (shelfPos == 2) { qb = shelfLo[j].run(qb); qb = shelfHi[j].run(qb); }
            const float wb = y + g * qb;
            lineB[j].push(wb);
            float rj = qb - g * wb;

            if (familyCInLoop) {
                const float qc = gainC[j] * lineC[j].peek();
                const float wc = rj + g * qc;
                lineC[j].push(wc);
                rj = qc - g * wc;
            }
            if (shelfPos == 1) { rj = shelfLo[j].run(rj); rj = shelfHi[j].run(rj); }
            resid[j] = rj;
        }

        // The parity fold — early, diffuse and late all fold the same way. Two
        // terms a side at four lanes; six/eight/ten lanes give three/four/five,
        // which is what proves the lane count.
        const float earlyL = E[0] + E[2],   earlyR = E[1] + E[3];
        const float diffL  = D[0] + D[2],   diffR  = D[1] + D[3];
        const float lateL  = resid[0] + resid[2], lateR = resid[1] + resid[3];

        // ── The final mixer ────────────────────────────────────────────────
        // Two stereo rings, a fixed 1.39 crossfeed, and a rotation matrix. ⚠ It
        // really is MixReflect on the early+diffuse composite and MixDiffuse on
        // the LATE bus — see buildMixer().
        mixLateBuf[2 * mixLateW]     = lateL;
        mixLateBuf[2 * mixLateW + 1] = lateR;
        int ra = mixLateW - mixLatePos; if (ra < 0) ra += mixLateCap;
        const float tapAL = mixLateBuf[2 * ra], tapAR = mixLateBuf[2 * ra + 1];
        if (++mixLateW >= mixLateCap) mixLateW = 0;

        const float compL = earlyL + 1.3899999856948853f * diffL;
        const float compR = earlyR + 1.3899999856948853f * diffR;
        mixEarlyBuf[2 * mixEarlyW]     = compL;
        mixEarlyBuf[2 * mixEarlyW + 1] = compR;
        int rb = mixEarlyW - mixEarlyPos; if (rb < 0) rb += mixEarlyCap;
        const float tapBL = mixEarlyBuf[2 * rb], tapBR = mixEarlyBuf[2 * rb + 1];
        if (++mixEarlyW >= mixEarlyCap) mixEarlyW = 0;

        const float wetL = cRefl * tapBL + cLate * tapAL;
        const float wetR = cRefl * tapBR + cLate * tapAR;

        // swapLR is a TRUE channel swap (AArch64 `rev64`), and the third mixer
        // coefficient is a hard zero, so the direct term is unscaled here.
        const float dirL = includeDirect ? cDry * inL : 0.0f;
        const float dirR = includeDirect ? cDry * inR : 0.0f;
        outL = dirL + cSame * wetL + cSwap * wetR;
        outR = dirR + cSame * wetR + cSwap * wetL;
    }

    /** ShelfLoGain/ShelfHiGain reach 0 in principle and log(0) is not a number.
     *  The device's own range starts at 0.2. */
    static float clampGain(float x) { return x < 1e-4f ? 1e-4f : x; }

    // ── Raw device parameters, by their own names ──────────────────────────
    //
    // For the NULL TEST. A stock Reverb carries 33 parameters and DR32's send
    // has eight generic slots, so the musical knob mapping in dr32_fxbus.cpp
    // cannot express a stock preset. This takes the `.abl` JSON's own keys and
    // values directly, with no DR32-side interpretation in between.
    //
    // ⚠ It reports THREE outcomes, and the third is the point. A null test
    // whose renderer silently ignores a parameter it does not implement is a
    // null test that lies: the number comes out bad and nothing says why. So a
    // key that this port knowingly does not implement is distinguished from one
    // it has never heard of, and the caller is expected to print both.
    enum Applied {
        kUnknown     = -1,   // not a Reverb parameter at all — caller should fail
        kNotModelled =  0,   // real, understood, deliberately absent from the port
        kApplied     =  1,
    };

    /** Set one parameter by the device's own key name. `build()` is NOT called;
     *  call it once after the whole block, as the device's own callbacks
     *  effectively do. */
    Applied setRaw(const char *key, float v) {
        if (!key) return kUnknown;
        auto eq = [&](const char *k) { return std::strcmp(key, k) == 0; };

        if (eq("DecayTime"))     { decayTime = v * 0.001f; return kApplied; }  // ms
        if (eq("RoomSize"))      { roomSize = v; return kApplied; }
        if (eq("PreDelay"))      { preDelayMs = v; return kApplied; }          // ms
        if (eq("DiffuseDelay"))  { diffuseDelay = v; return kApplied; }
        if (eq("AllPassGain"))   { allPassGain = v; return kApplied; }
        if (eq("AllPassSize"))   { allPassSize = v; return kApplied; }
        if (eq("ShelfLoFreq"))   { shelfLoFreq = v; return kApplied; }
        if (eq("ShelfHiFreq"))   { shelfHiFreq = v; return kApplied; }
        if (eq("EarlyReflectModFreq"))  { earlyModFreq = v; return kApplied; }
        if (eq("EarlyReflectModDepth")) { earlyModDepth = v; return kApplied; }
        if (eq("SpinOn"))        { spinOn = (v >= 0.5f); return kApplied; }
        if (eq("FreezeOn"))      { freeze = (v >= 0.5f); return kApplied; }
        if (eq("MixReflect"))    { mixReflect = v; return kApplied; }
        if (eq("MixDiffuse"))    { mixDiffuse = v; return kApplied; }
        // The shelf ON switches are real, not ignored: unity gain IS a
        // bypassed shelf here, because the builder's Alo/Ahi become 1 and the
        // SVF collapses to a flat mix.
        if (eq("ShelfLowOn"))    { shelfLowOn = (v >= 0.5f); return kApplied; }
        if (eq("ShelfHighOn"))   { shelfHighOn = (v >= 0.5f); return kApplied; }
        if (eq("ShelfLoGain"))   { shelfLoGain = v; return kApplied; }
        if (eq("ShelfHiGain"))   { shelfHiGain = v; return kApplied; }

        if (eq("BandFreq"))      { bandFreq = v; return kApplied; }
        if (eq("BandWidth"))     { bandWidth = v; return kApplied; }   // OCTAVES
        if (eq("BandLowOn"))     { bandLowOn = (v >= 0.5f); return kApplied; }
        if (eq("BandHighOn"))    { bandHighOn = (v >= 0.5f); return kApplied; }
        if (eq("SizeModFreq"))   { sizeModFreq = v; return kApplied; }
        if (eq("SizeModDepth"))  { sizeModDepth = v; return kApplied; }
        if (eq("SizeSmoothing")) { sizeSmoothing = (int)(v + 0.5f); return kApplied; }
        if (eq("ChorusOn"))      { chorusOn = (v >= 0.5f); return kApplied; }
        if (eq("CutOn"))         { cutOn = (v >= 0.5f); return kApplied; }
        if (eq("StereoSeparation")) { stereoSeparation = v; return kApplied; }
        if (eq("MixDirect"))     { mixDirect = v; return kApplied; }

        // Real parameters this port still does not model. Each is a known hole.
        //   HighFilterType  the Lowpass shelf variant (recovered, not wired up)
        //   FlatOn          affects FROZEN mode only, forcing both shelves off
        //   Enabled         the device's own bypass; DR32 owns bypass itself
        if (eq("HighFilterType") || eq("FlatOn") || eq("Enabled"))
            return kNotModelled;

        // ⚠ RoomType is not a room shape, it is the quality tier, and this port
        // is SuperEco only. Anything else means the null test is comparing
        // against a different algorithm and the number would be meaningless.
        if (eq("RoomType")) return (v == 0.0f) ? kApplied : kUnknown;

        return kUnknown;
    }
};

}  // namespace dr32_supereco

#endif
