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
//   - the 4-lane orthonormal feedback matrix, exact float bits
//   - the three delay-length families A/B/C and the RoomSize cube-root law
//   - the decay law, including the stock clamped-exp2 approximation
//   - the per-lane TPT shelf pair, including the stock tan approximation
//   - the feedback all-pass recurrence and the stereo parity folds
//
// NOT PORTED — the front end is still unrecovered, and is deliberately absent
// rather than guessed (see `_worklogs/NEXT-PROMPT-reverb-frontend.md`):
//   - the early-reflection tap table (offsets, weights, gains, tap motion)
//   - the two-state diffuser's five coefficient vectors
//   - the input Band filter's coefficient builder
//   - room-delay modulation: `SizeModFreq`/`SizeModDepth`/`SpinOn` are real and
//     nonzero in every stock preset, but the law that turns them into a read
//     offset is not recovered. **It is therefore OFF**, and a null test against
//     a stock preset will not close until it lands.
//
// So this is the TAIL, fed by DR32's own diffuser. It is the part the RT60
// measurements bear on, and it is complete on its own terms.

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

/** tan(w/2) as the device computes it, `w` being an angular frequency.
 *  The clamp is the device's, and it is what keeps the shelves stable as the
 *  corner frequency approaches Nyquist. */
inline float stockTanHalf(float w) {
    const float kClamp = 3.1337387561798096f;
    const float x  = 0.5f * (w < kClamp ? w : kClamp);
    const float x2 = x * x;
    return x * (0.999999463558197f - 0.09652461111545563f * x2) /
           (1.0f + (-0.4298672676086426f + 0.009981878101825714f * x2) * x2);
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
    /** Read the delayed sample, then write the new one. */
    inline float step(float x) {
        const float y = peek();
        push(x);
        return y;
    }
};

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
    bool  freeze      = false;      // decayRate 0 -> every gain unity
    bool  familyCInLoop = true;

    Line<kMaxA> lineA[kLanes];
    Line<kMaxB> lineB[kLanes];
    Line<kMaxC> lineC[kLanes];
    Svf   shelfLo[kLanes], shelfHi[kLanes];
    float gainA[kLanes], gainB[kLanes], gainC[kLanes];
    float resid[kLanes];            // R, the previous all-pass residual

    SuperEco() { reset(); build(); }

    void reset() {
        for (int j = 0; j < kLanes; j++) {
            lineA[j].reset(); lineB[j].reset(); lineC[j].reset();
            shelfLo[j].reset(); shelfHi[j].reset();
            resid[j] = 0.0f;
        }
    }

    void setSampleRate(float sr) {
        fs = (sr > 1.0f) ? sr : 44100.0f;
        reset();
        build();
    }

    /** Recompute every table. Cheap enough to call on any knob move — this is
     *  what the device's own rebuild functions do. */
    void build() {
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
            const float Alo = stockExp(6.0f * t * std::log(clampGain(shelfLoGain)));
            const float Ahi = stockExp(6.0f * t * std::log(clampGain(shelfHiGain)));
            const float Tlo = 2.0f * stockTanHalf(2.0f * 3.14159265358979f * shelfLoFreq / fs);
            const float Thi = 2.0f * stockTanHalf(2.0f * 3.14159265358979f * shelfHiFreq / fs);
            const float qlo = std::sqrt(std::sqrt(Alo));      // fourth root
            const float qhi = std::sqrt(std::sqrt(Ahi));

            shelfLo[j].set(Tlo / qlo, kShelfK, Alo, kShelfK * std::sqrt(Alo), 1.0f);
            shelfHi[j].set(Thi * qhi, kShelfK, 1.0f, kShelfK * std::sqrt(Ahi), Ahi);
        }
    }

    /** One stereo frame.
     *
     *  ⚠ The injection is a PLACEHOLDER. In the device, `U = E + D + R` — the
     *  early field and the diffuser output, neither of which is recovered yet.
     *  Here the (already diffused, by DR32's own diffuser) input is split by
     *  PARITY, matching the output fold, so the two sides stay decorrelated
     *  instead of collapsing to mono. Replace this with the real E and D once
     *  the front-end constants land. */
    inline void tick(float inL, float inR, float &outL, float &outR) {
        float U[kLanes];
        for (int j = 0; j < kLanes; j++)
            U[j] = ((j & 1) ? inR : inL) + resid[j];

        // W = M * U. Orthonormal, so this redistributes energy without adding
        // or removing any.
        float W[kLanes];
        for (int j = 0; j < kLanes; j++) {
            float acc = 0.0f;
            for (int k = 0; k < kLanes; k++) acc += kMatrix[j][k] * U[k];
            W[j] = acc;
        }

        const float g = allPassGain;
        for (int j = 0; j < kLanes; j++) {
            // Room delay, then the shelf pair: one loss per round trip.
            float y = gainA[j] * lineA[j].step(W[j]);
            y = shelfLo[j].run(y);
            y = shelfHi[j].run(y);

            // Feedback all-pass, exactly as the kernel spells it: the decay
            // gain multiplies the all-pass READ, and the residual is both the
            // audible output and the feedback term.
            const float qb = gainB[j] * lineB[j].peek();
            const float wb = y + g * qb;
            lineB[j].push(wb);
            float rj = qb - g * wb;

            if (familyCInLoop) {
                const float qc = gainC[j] * lineC[j].peek();
                const float wc = rj + g * qc;
                lineC[j].push(wc);
                rj = qc - g * wc;
            }
            resid[j] = rj;
        }

        // The parity fold. Two terms a side at four lanes; six/eight/ten lanes
        // give three/four/five, which is what proves the lane count.
        outL = resid[0] + resid[2];
        outR = resid[1] + resid[3];
    }

    /** ShelfLoGain/ShelfHiGain reach 0 in principle and log(0) is not a number.
     *  The device's own range starts at 0.2. */
    static float clampGain(float x) { return x < 1e-4f ? 1e-4f : x; }
};

}  // namespace dr32_supereco

#endif
