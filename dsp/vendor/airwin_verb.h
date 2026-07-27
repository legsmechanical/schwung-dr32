#pragma once
// ============================================================================
//  airwin_verb.h - faithful C++ ports of two Airwindows reverbs.
//  Original algorithms (c) Chris Johnson / Airwindows, MIT license.
//    * Chamber   - golden-ratio Householder feedforward chamber reverb.
//    * Infinity2 - allpass + Householder feedback reverb that can freeze
//                  (sustain infinitely) without blowing up.
//
//  Ported to single-sample, RT-safe, self-contained structs for Schwung,
//  mirroring the style of galactic.h (fixed member arrays, fpd dither,
//  overallscale caching, no allocation). Source of truth: the MacVST
//  processReplacing (float) paths of Chamber / Infinity2.
// ============================================================================
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#ifndef EFX_AIRWIN_PI
#define EFX_AIRWIN_PI 3.141592653589793238
#endif


// PERFORMANCE NOTE: the dither literals here were `5.5e-36l`. The `l` suffix
// makes that arithmetic LONG DOUBLE, which on aarch64 is IEEE binary128
// emulated in software -- measured at ~1.2% of a Move core per stage elsewhere
// in this module. Dropped to double; the dither sits ~157 dB down and is
// unaffected in any audible sense. Same applies to pow(2,N) -> ldexp(1.0,N),
// which is bit-identical for integer N.

namespace efx {

// ---------------------------------------------------------------------------
//  Chamber
// ---------------------------------------------------------------------------
//  Golden-ratio (0.618...) chained delay network with a 3-stage Householder
//  feedforward matrix and a per-cycle interpolation "regen" that lets the
//  tail sustain very long.
//
//  Control mapping (our 4 params -> Chamber A..E):
//    size     -> A "Bigness"  : reverb space, size = (A^2 * 0.9) + 0.1
//    damp     -> D "Darkness" : output lowpass, lowpass = (1 - D^2)/sqrt(os)
//    bigness  -> B "Longness" : regen/sustain, regen = (1 - (1-B)^6) * 0.123
//    mix      -> dry/wet blend (implemented HERE, see below)
//  Chamber C "Liteness" (input highpass) is not exposed; held at 0.35 default.
//
//  Mix: the original's E "Wetness" is a submix (both dry & wet at full volume
//  around 0.5). We DO NOT use it. Instead the internal reverb runs full-wet
//  and we blend ourselves:  out = dry*(1-mix) + wet*mix.  mix=0 => exact
//  passthrough, mix=1 => full wet.
// ---------------------------------------------------------------------------
struct Chamber {
    Chamber() { reset(); recalc(); }

    void setSampleRate(float fs) {
        fs_ = (fs > 0.0f) ? fs : 44100.0f;
        recalc();
    }

    // All inputs 0..1.
    void setParams(float size, float damp, float bigness, float mix) {
        A_ = clamp01(size);
        D_ = clamp01(damp);
        B_ = clamp01(bigness);
        mix_ = clamp01(mix);
        recalc();
    }

    void reset() {
        iirAL = iirAR = iirBL = iirBR = iirCL = iirCR = 0.0;

        std::memset(aEL, 0, sizeof(aEL)); std::memset(aER, 0, sizeof(aER));
        std::memset(aFL, 0, sizeof(aFL)); std::memset(aFR, 0, sizeof(aFR));
        std::memset(aGL, 0, sizeof(aGL)); std::memset(aGR, 0, sizeof(aGR));
        std::memset(aHL, 0, sizeof(aHL)); std::memset(aHR, 0, sizeof(aHR));
        std::memset(aAL, 0, sizeof(aAL)); std::memset(aAR, 0, sizeof(aAR));
        std::memset(aBL, 0, sizeof(aBL)); std::memset(aBR, 0, sizeof(aBR));
        std::memset(aCL, 0, sizeof(aCL)); std::memset(aCR, 0, sizeof(aCR));
        std::memset(aDL, 0, sizeof(aDL)); std::memset(aDR, 0, sizeof(aDR));
        std::memset(aIL, 0, sizeof(aIL)); std::memset(aIR, 0, sizeof(aIR));
        std::memset(aJL, 0, sizeof(aJL)); std::memset(aJR, 0, sizeof(aJR));
        std::memset(aKL, 0, sizeof(aKL)); std::memset(aKR, 0, sizeof(aKR));
        std::memset(aLL, 0, sizeof(aLL)); std::memset(aLR, 0, sizeof(aLR));

        feedbackAL = feedbackAR = feedbackBL = feedbackBR = 0.0;
        feedbackCL = feedbackCR = feedbackDL = feedbackDR = 0.0;
        previousAL = previousAR = previousBL = previousBR = 0.0;
        previousCL = previousCR = previousDL = previousDR = 0.0;

        for (int i = 0; i < 10; ++i) { lastRefL[i] = 0.0; lastRefR[i] = 0.0; }

        countI = countJ = countK = countL = 1;
        countA = countB = countC = countD = 1;
        countE = countF = countG = countH = 1;
        cycle = 0;

        fpdL = 1; while (fpdL < 16386) fpdL = (uint32_t)(rand() * (double)UINT32_MAX);
        fpdR = 1; while (fpdR < 16386) fpdR = (uint32_t)(rand() * (double)UINT32_MAX);
    }

    void tick(float &l, float &r) {
        double inputSampleL = l;
        double inputSampleR = r;
        if (std::fabs(inputSampleL) < 1.18e-23) inputSampleL = fpdL * 1.18e-17;
        if (std::fabs(inputSampleR) < 1.18e-23) inputSampleR = fpdR * 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        iirCL = (iirCL * (1.0 - highpass_)) + (inputSampleL * highpass_); inputSampleL -= iirCL;
        iirCR = (iirCR * (1.0 - highpass_)) + (inputSampleR * highpass_); inputSampleR -= iirCR;
        // initial highpass

        iirAL = (iirAL * (1.0 - lowpass_)) + (inputSampleL * lowpass_); inputSampleL = iirAL;
        iirAR = (iirAR * (1.0 - lowpass_)) + (inputSampleR * lowpass_); inputSampleR = iirAR;
        // initial filter

        cycle++;
        if (cycle == cycleEnd_) {
            feedbackAL = (feedbackAL * (1.0 - interpolate_)) + (previousAL * interpolate_); previousAL = feedbackAL;
            feedbackBL = (feedbackBL * (1.0 - interpolate_)) + (previousBL * interpolate_); previousBL = feedbackBL;
            feedbackCL = (feedbackCL * (1.0 - interpolate_)) + (previousCL * interpolate_); previousCL = feedbackCL;
            feedbackDL = (feedbackDL * (1.0 - interpolate_)) + (previousDL * interpolate_); previousDL = feedbackDL;
            feedbackAR = (feedbackAR * (1.0 - interpolate_)) + (previousAR * interpolate_); previousAR = feedbackAR;
            feedbackBR = (feedbackBR * (1.0 - interpolate_)) + (previousBR * interpolate_); previousBR = feedbackBR;
            feedbackCR = (feedbackCR * (1.0 - interpolate_)) + (previousCR * interpolate_); previousCR = feedbackCR;
            feedbackDR = (feedbackDR * (1.0 - interpolate_)) + (previousDR * interpolate_); previousDR = feedbackDR;

            aIL[countI] = inputSampleL + (feedbackAL * regen_);
            aJL[countJ] = inputSampleL + (feedbackBL * regen_);
            aKL[countK] = inputSampleL + (feedbackCL * regen_);
            aLL[countL] = inputSampleL + (feedbackDL * regen_);
            aIR[countI] = inputSampleR + (feedbackAR * regen_);
            aJR[countJ] = inputSampleR + (feedbackBR * regen_);
            aKR[countK] = inputSampleR + (feedbackCR * regen_);
            aLR[countL] = inputSampleR + (feedbackDR * regen_);

            countI++; if (countI < 0 || countI > delayI) countI = 0;
            countJ++; if (countJ < 0 || countJ > delayJ) countJ = 0;
            countK++; if (countK < 0 || countK > delayK) countK = 0;
            countL++; if (countL < 0 || countL > delayL) countL = 0;

            double outIL = aIL[countI - ((countI > delayI) ? delayI + 1 : 0)];
            double outJL = aJL[countJ - ((countJ > delayJ) ? delayJ + 1 : 0)];
            double outKL = aKL[countK - ((countK > delayK) ? delayK + 1 : 0)];
            double outLL = aLL[countL - ((countL > delayL) ? delayL + 1 : 0)];
            double outIR = aIR[countI - ((countI > delayI) ? delayI + 1 : 0)];
            double outJR = aJR[countJ - ((countJ > delayJ) ? delayJ + 1 : 0)];
            double outKR = aKR[countK - ((countK > delayK) ? delayK + 1 : 0)];
            double outLR = aLR[countL - ((countL > delayL) ? delayL + 1 : 0)];

            aAL[countA] = (outIL - (outJL + outKL + outLL));
            aBL[countB] = (outJL - (outIL + outKL + outLL));
            aCL[countC] = (outKL - (outIL + outJL + outLL));
            aDL[countD] = (outLL - (outIL + outJL + outKL));
            aAR[countA] = (outIR - (outJR + outKR + outLR));
            aBR[countB] = (outJR - (outIR + outKR + outLR));
            aCR[countC] = (outKR - (outIR + outJR + outLR));
            aDR[countD] = (outLR - (outIR + outJR + outKR));

            countA++; if (countA < 0 || countA > delayA) countA = 0;
            countB++; if (countB < 0 || countB > delayB) countB = 0;
            countC++; if (countC < 0 || countC > delayC) countC = 0;
            countD++; if (countD < 0 || countD > delayD) countD = 0;

            double outAL = aAL[countA - ((countA > delayA) ? delayA + 1 : 0)];
            double outBL = aBL[countB - ((countB > delayB) ? delayB + 1 : 0)];
            double outCL = aCL[countC - ((countC > delayC) ? delayC + 1 : 0)];
            double outDL = aDL[countD - ((countD > delayD) ? delayD + 1 : 0)];
            double outAR = aAR[countA - ((countA > delayA) ? delayA + 1 : 0)];
            double outBR = aBR[countB - ((countB > delayB) ? delayB + 1 : 0)];
            double outCR = aCR[countC - ((countC > delayC) ? delayC + 1 : 0)];
            double outDR = aDR[countD - ((countD > delayD) ? delayD + 1 : 0)];

            aEL[countE] = (outAL - (outBL + outCL + outDL));
            aFL[countF] = (outBL - (outAL + outCL + outDL));
            aGL[countG] = (outCL - (outAL + outBL + outDL));
            aHL[countH] = (outDL - (outAL + outBL + outCL));
            aER[countE] = (outAR - (outBR + outCR + outDR));
            aFR[countF] = (outBR - (outAR + outCR + outDR));
            aGR[countG] = (outCR - (outAR + outBR + outDR));
            aHR[countH] = (outDR - (outAR + outBR + outCR));

            countE++; if (countE < 0 || countE > delayE) countE = 0;
            countF++; if (countF < 0 || countF > delayF) countF = 0;
            countG++; if (countG < 0 || countG > delayG) countG = 0;
            countH++; if (countH < 0 || countH > delayH) countH = 0;

            double outEL = aEL[countE - ((countE > delayE) ? delayE + 1 : 0)];
            double outFL = aFL[countF - ((countF > delayF) ? delayF + 1 : 0)];
            double outGL = aGL[countG - ((countG > delayG) ? delayG + 1 : 0)];
            double outHL = aHL[countH - ((countH > delayH) ? delayH + 1 : 0)];
            double outER = aER[countE - ((countE > delayE) ? delayE + 1 : 0)];
            double outFR = aFR[countF - ((countF > delayF) ? delayF + 1 : 0)];
            double outGR = aGR[countG - ((countG > delayG) ? delayG + 1 : 0)];
            double outHR = aHR[countH - ((countH > delayH) ? delayH + 1 : 0)];

            feedbackAL = (outEL - (outFL + outGL + outHL));
            feedbackBL = (outFL - (outEL + outGL + outHL));
            feedbackCL = (outGL - (outEL + outFL + outHL));
            feedbackDL = (outHL - (outEL + outFL + outGL));
            feedbackAR = (outER - (outFR + outGR + outHR));
            feedbackBR = (outFR - (outER + outGR + outHR));
            feedbackCR = (outGR - (outER + outFR + outHR));
            feedbackDR = (outHR - (outER + outFR + outGR));

            inputSampleL = (outEL + outFL + outGL + outHL) / 8.0;
            inputSampleR = (outER + outFR + outGR + outHR) / 8.0;

            if (cycleEnd_ == 4) {
                lastRefL[0] = lastRefL[4];
                lastRefL[2] = (lastRefL[0] + inputSampleL) / 2;
                lastRefL[1] = (lastRefL[0] + lastRefL[2]) / 2;
                lastRefL[3] = (lastRefL[2] + inputSampleL) / 2;
                lastRefL[4] = inputSampleL;
                lastRefR[0] = lastRefR[4];
                lastRefR[2] = (lastRefR[0] + inputSampleR) / 2;
                lastRefR[1] = (lastRefR[0] + lastRefR[2]) / 2;
                lastRefR[3] = (lastRefR[2] + inputSampleR) / 2;
                lastRefR[4] = inputSampleR;
            }
            if (cycleEnd_ == 3) {
                lastRefL[0] = lastRefL[3];
                lastRefL[2] = (lastRefL[0] + lastRefL[0] + inputSampleL) / 3;
                lastRefL[1] = (lastRefL[0] + inputSampleL + inputSampleL) / 3;
                lastRefL[3] = inputSampleL;
                lastRefR[0] = lastRefR[3];
                lastRefR[2] = (lastRefR[0] + lastRefR[0] + inputSampleR) / 3;
                lastRefR[1] = (lastRefR[0] + inputSampleR + inputSampleR) / 3;
                lastRefR[3] = inputSampleR;
            }
            if (cycleEnd_ == 2) {
                lastRefL[0] = lastRefL[2];
                lastRefL[1] = (lastRefL[0] + inputSampleL) / 2;
                lastRefL[2] = inputSampleL;
                lastRefR[0] = lastRefR[2];
                lastRefR[1] = (lastRefR[0] + inputSampleR) / 2;
                lastRefR[2] = inputSampleR;
            }
            if (cycleEnd_ == 1) {
                lastRefL[0] = inputSampleL;
                lastRefR[0] = inputSampleR;
            }
            cycle = 0;
            inputSampleL = lastRefL[cycle];
            inputSampleR = lastRefR[cycle];
        } else {
            inputSampleL = lastRefL[cycle];
            inputSampleR = lastRefR[cycle];
        }

        switch (cycleEnd_) { // multi-pole average using lastRef[] variables
            case 4:
                lastRefL[8] = inputSampleL; inputSampleL = (inputSampleL + lastRefL[7]) * 0.5;
                lastRefL[7] = lastRefL[8];
                lastRefR[8] = inputSampleR; inputSampleR = (inputSampleR + lastRefR[7]) * 0.5;
                lastRefR[7] = lastRefR[8];
                [[fallthrough]];
            case 3:
                lastRefL[8] = inputSampleL; inputSampleL = (inputSampleL + lastRefL[6]) * 0.5;
                lastRefL[6] = lastRefL[8];
                lastRefR[8] = inputSampleR; inputSampleR = (inputSampleR + lastRefR[6]) * 0.5;
                lastRefR[6] = lastRefR[8];
                [[fallthrough]];
            case 2:
                lastRefL[8] = inputSampleL; inputSampleL = (inputSampleL + lastRefL[5]) * 0.5;
                lastRefL[5] = lastRefL[8];
                lastRefR[8] = inputSampleR; inputSampleR = (inputSampleR + lastRefR[5]) * 0.5;
                lastRefR[5] = lastRefR[8];
                [[fallthrough]];
            case 1:
                break;
        }

        iirBL = (iirBL * (1.0 - lowpass_)) + (inputSampleL * lowpass_); inputSampleL = iirBL;
        iirBR = (iirBR * (1.0 - lowpass_)) + (inputSampleR * lowpass_); inputSampleR = iirBR;
        // end filter

        // our own dry/wet: mix=0 dry passthrough, mix=1 full wet
        inputSampleL = (drySampleL * (1.0 - mix_)) + (inputSampleL * mix_);
        inputSampleR = (drySampleR * (1.0 - mix_)) + (inputSampleR * mix_);

        // begin 32 bit stereo floating point dither
        int expon; std::frexp((float)inputSampleL, &expon);
        fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
        inputSampleL += ((double(fpdL) - uint32_t(0x7fffffff)) * 5.5e-36 * std::pow(2, expon + 62));
        std::frexp((float)inputSampleR, &expon);
        fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
        inputSampleR += ((double(fpdR) - uint32_t(0x7fffffff)) * 5.5e-36 * std::pow(2, expon + 62));
        // end 32 bit stereo floating point dither

        l = (float)inputSampleL;
        r = (float)inputSampleR;
    }

private:
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    static constexpr double GR = 0.618033988749894848204586; // golden ratio conjugate

    void recalc() {
        double overallscale = 1.0;
        overallscale /= 44100.0;
        overallscale *= (double)fs_;

        cycleEnd_ = (int)std::floor(overallscale);
        if (cycleEnd_ < 1) cycleEnd_ = 1;
        if (cycleEnd_ > 4) cycleEnd_ = 4;
        if (cycle > cycleEnd_ - 1) cycle = cycleEnd_ - 1;

        double size = (std::pow((double)A_, 2) * 0.9) + 0.1;
        regen_ = (1.0 - std::pow(1.0 - (double)B_, 6)) * 0.123;
        highpass_ = std::pow((double)C_, 2.0) / std::sqrt(overallscale);
        lowpass_ = (1.0 - std::pow((double)D_, 2.0)) / std::sqrt(overallscale);
        interpolate_ = size * 0.381966011250105;

        delayE = (int)(19900.0 * size);
        delayF = (int)(delayE * GR);
        delayG = (int)(delayF * GR);
        delayH = (int)(delayG * GR);
        delayA = (int)(delayH * GR);
        delayB = (int)(delayA * GR);
        delayC = (int)(delayB * GR);
        delayD = (int)(delayC * GR);
        delayI = (int)(delayD * GR);
        delayJ = (int)(delayI * GR);
        delayK = (int)(delayJ * GR);
        delayL = (int)(delayK * GR);
    }

    // delay buffers (original max sizes; SR-independent, size scales by params)
    float aEL[20000], aFL[12361], aGL[7640], aHL[4722];
    float aAL[2916], aBL[1804], aCL[1115], aDL[689];
    float aIL[426], aJL[264], aKL[163], aLL[101];
    float aER[20000], aFR[12361], aGR[7640], aHR[4722];
    float aAR[2916], aBR[1804], aCR[1115], aDR[689];
    float aIR[426], aJR[264], aKR[163], aLR[101];

    double iirAL, iirBL, iirCL, iirAR, iirBR, iirCR;

    double feedbackAL, feedbackBL, feedbackCL, feedbackDL;
    double feedbackAR, feedbackBR, feedbackCR, feedbackDR;
    double previousAL, previousBL, previousCL, previousDL;
    double previousAR, previousBR, previousCR, previousDR;

    double lastRefL[10], lastRefR[10];

    int countA, delayA;
    int countB, delayB;
    int countC, delayC;
    int countD, delayD;
    int countE, delayE;
    int countF, delayF;
    int countG, delayG;
    int countH, delayH;
    int countI, delayI;
    int countJ, delayJ;
    int countK, delayK;
    int countL, delayL;
    int cycle;

    uint32_t fpdL, fpdR;

    // params: A Bigness(size), B Longness(bigness/regen), C Liteness(highpass,
    // fixed 0.35 default), D Darkness(damp). mix_ is our own dry/wet.
    float A_ = 0.35f, B_ = 0.35f, C_ = 0.35f, D_ = 0.35f, mix_ = 1.0f;

    // cached derived
    int cycleEnd_ = 1;
    double regen_ = 0.0, highpass_ = 0.0, lowpass_ = 0.0, interpolate_ = 0.0;

    float fs_ = 44100.0f;
};

// ---------------------------------------------------------------------------
//  InfinityVerb  (Airwindows Infinity2)
// ---------------------------------------------------------------------------
//  Input lowpass biquad -> 4 nested allpasses -> 2 four-tap Householder
//  feedback matrices with per-tap "dialBack" soft-compression, a post
//  lowpass biquad, hard clamp + asin() saturation and a final lowpass biquad.
//  The dialBack compression + clamp/asin are what make the freeze regime
//  (feedback -> 1) sustain forever yet stay bounded.
//
//  Control mapping (our 4 params -> Infinity2 A..F):
//    size     -> B "Size"     : delay lengths, size = (B^2 * 99) + 1
//    damp     -> C "Damping"  : fractional-tap darkening, damp = C^2 * 0.5
//    feedback -> E "Feedback" : regen, feedback = 1 - (1-E)^4  (E->1 = freeze)
//    mix      -> dry/wet blend (implemented HERE)
//  Infinity2 A "Filter" (input LP) held at default 1.0 (open), and
//  D "Allpass" (rawPass) held at default 1.0 (full reverb, no dry blend in).
//
//  Mix: internal wet path runs full; we blend out = dry*(1-mix) + wet*mix.
// ---------------------------------------------------------------------------
struct InfinityVerb {
    InfinityVerb() { reset(); recalc(); }

    void setSampleRate(float fs) {
        fs_ = (fs > 0.0f) ? fs : 44100.0f;
        recalc();
    }

    // All inputs 0..1.
    //
    // ⚠ FIXED IN DR32 (2026-07-26). The port assigned B_/C_/E_ while recalc()
    // reads A_/B_/C_/D_, so `size` never reached A_ and D_ stayed at its 1.0
    // default — making lowpass_ = (1 - D_^2) = 0, which zeroes the wet input
    // outright (`iirAL = iirAL*(1-0) + in*0; in = iirAL`). The result was a
    // completely silent reverb. Upstream echidna-fx routes its Hall to Chamber,
    // so this path was never exercised there.
    void setParams(float size, float damp, float decay, float mix) {
        A_ = clamp01(size);          // delay-line size
        B_ = clamp01(decay);         // regeneration / tail length
        C_ = clamp01(damp) * 0.5f;   // highpass amount, gentle
        D_ = clamp01(damp);          // lowpass: 0 damp -> wide open
        E_ = 1.0f;
        mix_ = clamp01(mix);
        recalc();
    }

    void reset() {
        for (int x = 0; x < 11; x++) { biquadA[x] = 0.0; biquadB[x] = 0.0; biquadC[x] = 0.0; }

        feedbackAL = feedbackAR = feedbackBL = feedbackBR = 0.0;
        feedbackCL = feedbackCR = feedbackDL = feedbackDR = 0.0;
        feedbackEL = feedbackER = feedbackFL = feedbackFR = 0.0;
        feedbackGL = feedbackGR = feedbackHL = feedbackHR = 0.0;

        std::memset(aAL, 0, sizeof(aAL)); std::memset(aAR, 0, sizeof(aAR));
        std::memset(aBL, 0, sizeof(aBL)); std::memset(aBR, 0, sizeof(aBR));
        std::memset(aCL, 0, sizeof(aCL)); std::memset(aCR, 0, sizeof(aCR));
        std::memset(aDL, 0, sizeof(aDL)); std::memset(aDR, 0, sizeof(aDR));
        std::memset(aEL, 0, sizeof(aEL)); std::memset(aER, 0, sizeof(aER));
        std::memset(aFL, 0, sizeof(aFL)); std::memset(aFR, 0, sizeof(aFR));
        std::memset(aGL, 0, sizeof(aGL)); std::memset(aGR, 0, sizeof(aGR));
        std::memset(aHL, 0, sizeof(aHL)); std::memset(aHR, 0, sizeof(aHR));
        std::memset(aIL, 0, sizeof(aIL)); std::memset(aIR, 0, sizeof(aIR));
        std::memset(aJL, 0, sizeof(aJL)); std::memset(aJR, 0, sizeof(aJR));
        std::memset(aKL, 0, sizeof(aKL)); std::memset(aKR, 0, sizeof(aKR));
        std::memset(aLL, 0, sizeof(aLL)); std::memset(aLR, 0, sizeof(aLR));

        countA = 1; countB = 1; countC = 1; countD = 1;
        countE = 1; countF = 1; countG = 1; countH = 1;
        countI = 1; countJ = 1; countK = 1; countL = 1;

        fpdL = 1; while (fpdL < 16386) fpdL = (uint32_t)(rand() * (double)UINT32_MAX);
        fpdR = 1; while (fpdR < 16386) fpdR = (uint32_t)(rand() * (double)UINT32_MAX);
    }

    void tick(float &l, float &r) {
        double inputSampleL = l;
        double inputSampleR = r;
        if (std::fabs(inputSampleL) < 1.18e-23) inputSampleL = fpdL * 1.18e-17;
        if (std::fabs(inputSampleR) < 1.18e-23) inputSampleR = fpdR * 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        double tempSampleL = (inputSampleL * biquadA[2]) + biquadA[7];
        biquadA[7] = (inputSampleL * biquadA[3]) - (tempSampleL * biquadA[5]) + biquadA[8];
        biquadA[8] = (inputSampleL * biquadA[4]) - (tempSampleL * biquadA[6]);
        inputSampleL = tempSampleL;

        double tempSampleR = (inputSampleR * biquadA[2]) + biquadA[9];
        biquadA[9] = (inputSampleR * biquadA[3]) - (tempSampleR * biquadA[5]) + biquadA[10];
        biquadA[10] = (inputSampleR * biquadA[4]) - (tempSampleR * biquadA[6]);
        inputSampleR = tempSampleR;

        double allpassIL = inputSampleL, allpassJL = inputSampleL, allpassKL = inputSampleL, allpassLL = inputSampleL;
        double allpassIR = inputSampleR, allpassJR = inputSampleR, allpassKR = inputSampleR, allpassLR = inputSampleR;

        int allpasstemp = countI + 1;
        if (allpasstemp < 0 || allpasstemp > delayI) allpasstemp = 0;
        allpassIL -= aIL[allpasstemp] * 0.5; aIL[countI] = allpassIL; allpassIL *= 0.5;
        allpassIR -= aIR[allpasstemp] * 0.5; aIR[countI] = allpassIR; allpassIR *= 0.5;
        countI++; if (countI < 0 || countI > delayI) countI = 0;
        allpassIL += (aIL[countI]); allpassIR += (aIR[countI]);

        allpasstemp = countJ + 1;
        if (allpasstemp < 0 || allpasstemp > delayJ) allpasstemp = 0;
        allpassJL -= aJL[allpasstemp] * 0.5; aJL[countJ] = allpassJL; allpassJL *= 0.5;
        allpassJR -= aJR[allpasstemp] * 0.5; aJR[countJ] = allpassJR; allpassJR *= 0.5;
        countJ++; if (countJ < 0 || countJ > delayJ) countJ = 0;
        allpassJL += (aJL[countJ]); allpassJR += (aJR[countJ]);

        allpasstemp = countK + 1;
        if (allpasstemp < 0 || allpasstemp > delayK) allpasstemp = 0;
        allpassKL -= aKL[allpasstemp] * 0.5; aKL[countK] = allpassKL; allpassKL *= 0.5;
        allpassKR -= aKR[allpasstemp] * 0.5; aKR[countK] = allpassKR; allpassKR *= 0.5;
        countK++; if (countK < 0 || countK > delayK) countK = 0;
        allpassKL += (aKL[countK]); allpassKR += (aKR[countK]);

        allpasstemp = countL + 1;
        if (allpasstemp < 0 || allpasstemp > delayL) allpasstemp = 0;
        allpassLL -= aLL[allpasstemp] * 0.5; aLL[countL] = allpassLL; allpassLL *= 0.5;
        allpassLR -= aLR[allpasstemp] * 0.5; aLR[countL] = allpassLR; allpassLR *= 0.5;
        countL++; if (countL < 0 || countL > delayL) countL = 0;
        allpassLL += (aLL[countL]); allpassLR += (aLR[countL]);
        // the big allpass in front of everything

        if (rawPass_ != 1.0) {
            allpassIL = (allpassIL * rawPass_) + (drySampleL * (1.0 - rawPass_));
            allpassJL = (allpassJL * rawPass_) + (drySampleL * (1.0 - rawPass_));
            allpassKL = (allpassKL * rawPass_) + (drySampleL * (1.0 - rawPass_));
            allpassLL = (allpassLL * rawPass_) + (drySampleL * (1.0 - rawPass_));
            allpassIR = (allpassIR * rawPass_) + (drySampleR * (1.0 - rawPass_));
            allpassJR = (allpassJR * rawPass_) + (drySampleR * (1.0 - rawPass_));
            allpassKR = (allpassKR * rawPass_) + (drySampleR * (1.0 - rawPass_));
            allpassLR = (allpassLR * rawPass_) + (drySampleR * (1.0 - rawPass_));
        }

        aAL[countA] = allpassIL + (feedbackAL * feedback_);
        aBL[countB] = allpassJL + (feedbackBL * feedback_);
        aCL[countC] = allpassKL + (feedbackCL * feedback_);
        aDL[countD] = allpassLL + (feedbackDL * feedback_);
        aEL[countE] = allpassIL + (feedbackEL * feedback_);
        aFL[countF] = allpassJL + (feedbackFL * feedback_);
        aGL[countG] = allpassKL + (feedbackGL * feedback_);
        aHL[countH] = allpassLL + (feedbackHL * feedback_);

        aAR[countA] = allpassIR + (feedbackAR * feedback_);
        aBR[countB] = allpassJR + (feedbackBR * feedback_);
        aCR[countC] = allpassKR + (feedbackCR * feedback_);
        aDR[countD] = allpassLR + (feedbackDR * feedback_);
        aER[countE] = allpassIR + (feedbackER * feedback_);
        aFR[countF] = allpassJR + (feedbackFR * feedback_);
        aGR[countG] = allpassKR + (feedbackGR * feedback_);
        aHR[countH] = allpassLR + (feedbackHR * feedback_);

        countA++; if (countA < 0 || countA > delayA) countA = 0;
        countB++; if (countB < 0 || countB > delayB) countB = 0;
        countC++; if (countC < 0 || countC > delayC) countC = 0;
        countD++; if (countD < 0 || countD > delayD) countD = 0;
        countE++; if (countE < 0 || countE > delayE) countE = 0;
        countF++; if (countF < 0 || countF > delayF) countF = 0;
        countG++; if (countG < 0 || countG > delayG) countG = 0;
        countH++; if (countH < 0 || countH > delayH) countH = 0;

        double frac = damping_ - std::floor(damping_);
        double infiniteAL = (aAL[countA - ((countA > delayA) ? delayA + 1 : 0)] * (1 - frac)) + (aAL[countA + 1 - ((countA + 1 > delayA) ? delayA + 1 : 0)] * frac);
        double infiniteBL = (aBL[countB - ((countB > delayB) ? delayB + 1 : 0)] * (1 - frac)) + (aBL[countB + 1 - ((countB + 1 > delayB) ? delayB + 1 : 0)] * frac);
        double infiniteCL = (aCL[countC - ((countC > delayC) ? delayC + 1 : 0)] * (1 - frac)) + (aCL[countC + 1 - ((countC + 1 > delayC) ? delayC + 1 : 0)] * frac);
        double infiniteDL = (aDL[countD - ((countD > delayD) ? delayD + 1 : 0)] * (1 - frac)) + (aDL[countD + 1 - ((countD + 1 > delayD) ? delayD + 1 : 0)] * frac);
        double infiniteAR = (aAR[countA - ((countA > delayA) ? delayA + 1 : 0)] * (1 - frac)) + (aAR[countA + 1 - ((countA + 1 > delayA) ? delayA + 1 : 0)] * frac);
        double infiniteBR = (aBR[countB - ((countB > delayB) ? delayB + 1 : 0)] * (1 - frac)) + (aBR[countB + 1 - ((countB + 1 > delayB) ? delayB + 1 : 0)] * frac);
        double infiniteCR = (aCR[countC - ((countC > delayC) ? delayC + 1 : 0)] * (1 - frac)) + (aCR[countC + 1 - ((countC + 1 > delayC) ? delayC + 1 : 0)] * frac);
        double infiniteDR = (aDR[countD - ((countD > delayD) ? delayD + 1 : 0)] * (1 - frac)) + (aDR[countD + 1 - ((countD + 1 > delayD) ? delayD + 1 : 0)] * frac);
        double infiniteEL = (aEL[countE - ((countE > delayE) ? delayE + 1 : 0)] * (1 - frac)) + (aEL[countE + 1 - ((countE + 1 > delayE) ? delayE + 1 : 0)] * frac);
        double infiniteFL = (aFL[countF - ((countF > delayF) ? delayF + 1 : 0)] * (1 - frac)) + (aFL[countF + 1 - ((countF + 1 > delayF) ? delayF + 1 : 0)] * frac);
        double infiniteGL = (aGL[countG - ((countG > delayG) ? delayG + 1 : 0)] * (1 - frac)) + (aGL[countG + 1 - ((countG + 1 > delayG) ? delayG + 1 : 0)] * frac);
        double infiniteHL = (aHL[countH - ((countH > delayH) ? delayH + 1 : 0)] * (1 - frac)) + (aHL[countH + 1 - ((countH + 1 > delayH) ? delayH + 1 : 0)] * frac);
        double infiniteER = (aER[countE - ((countE > delayE) ? delayE + 1 : 0)] * (1 - frac)) + (aER[countE + 1 - ((countE + 1 > delayE) ? delayE + 1 : 0)] * frac);
        double infiniteFR = (aFR[countF - ((countF > delayF) ? delayF + 1 : 0)] * (1 - frac)) + (aFR[countF + 1 - ((countF + 1 > delayF) ? delayF + 1 : 0)] * frac);
        double infiniteGR = (aGR[countG - ((countG > delayG) ? delayG + 1 : 0)] * (1 - frac)) + (aGR[countG + 1 - ((countG + 1 > delayG) ? delayG + 1 : 0)] * frac);
        double infiniteHR = (aHR[countH - ((countH > delayH) ? delayH + 1 : 0)] * (1 - frac)) + (aHR[countH + 1 - ((countH + 1 > delayH) ? delayH + 1 : 0)] * frac);

        double dialBackAL = 0.5, dialBackEL = 0.5, dialBackDryL = 0.5;
        if (std::fabs(infiniteAL) > 0.4) dialBackAL -= ((std::fabs(infiniteAL) - 0.4) * 0.2);
        if (std::fabs(infiniteEL) > 0.4) dialBackEL -= ((std::fabs(infiniteEL) - 0.4) * 0.2);
        if (std::fabs(drySampleL) > 0.4) dialBackDryL -= ((std::fabs(drySampleL) - 0.4) * 0.2);

        double dialBackAR = 0.5, dialBackER = 0.5, dialBackDryR = 0.5;
        if (std::fabs(infiniteAR) > 0.4) dialBackAR -= ((std::fabs(infiniteAR) - 0.4) * 0.2);
        if (std::fabs(infiniteER) > 0.4) dialBackER -= ((std::fabs(infiniteER) - 0.4) * 0.2);
        if (std::fabs(drySampleR) > 0.4) dialBackDryR -= ((std::fabs(drySampleR) - 0.4) * 0.2);
        // subtle compression so energy can be fed in without overloading

        feedbackAL = (infiniteAL - (infiniteBL + infiniteCL + infiniteDL)) * dialBackAL;
        feedbackBL = (infiniteBL - (infiniteAL + infiniteCL + infiniteDL)) * dialBackDryL;
        feedbackCL = (infiniteCL - (infiniteAL + infiniteBL + infiniteDL)) * dialBackDryL;
        feedbackDL = (infiniteDL - (infiniteAL + infiniteBL + infiniteCL)) * dialBackDryL;
        feedbackEL = (infiniteEL - (infiniteFL + infiniteGL + infiniteHL)) * dialBackEL;
        feedbackFL = (infiniteFL - (infiniteEL + infiniteGL + infiniteHL)) * dialBackDryL;
        feedbackGL = (infiniteGL - (infiniteEL + infiniteFL + infiniteHL)) * dialBackDryL;
        feedbackHL = (infiniteHL - (infiniteEL + infiniteFL + infiniteGL)) * dialBackDryL;

        feedbackAR = (infiniteAR - (infiniteBR + infiniteCR + infiniteDR)) * dialBackAR;
        feedbackBR = (infiniteBR - (infiniteAR + infiniteCR + infiniteDR)) * dialBackDryR;
        feedbackCR = (infiniteCR - (infiniteAR + infiniteBR + infiniteDR)) * dialBackDryR;
        feedbackDR = (infiniteDR - (infiniteAR + infiniteBR + infiniteCR)) * dialBackDryR;
        feedbackER = (infiniteER - (infiniteFR + infiniteGR + infiniteHR)) * dialBackER;
        feedbackFR = (infiniteFR - (infiniteER + infiniteGR + infiniteHR)) * dialBackDryR;
        feedbackGR = (infiniteGR - (infiniteER + infiniteFR + infiniteHR)) * dialBackDryR;
        feedbackHR = (infiniteHR - (infiniteER + infiniteFR + infiniteGR)) * dialBackDryR;

        inputSampleL = (infiniteAL + infiniteBL + infiniteCL + infiniteDL + infiniteEL + infiniteFL + infiniteGL + infiniteHL) / 8.0;
        inputSampleR = (infiniteAR + infiniteBR + infiniteCR + infiniteDR + infiniteER + infiniteFR + infiniteGR + infiniteHR) / 8.0;

        tempSampleL = (inputSampleL * biquadB[2]) + biquadB[7];
        biquadB[7] = (inputSampleL * biquadB[3]) - (tempSampleL * biquadB[5]) + biquadB[8];
        biquadB[8] = (inputSampleL * biquadB[4]) - (tempSampleL * biquadB[6]);
        inputSampleL = tempSampleL;

        tempSampleR = (inputSampleR * biquadB[2]) + biquadB[9];
        biquadB[9] = (inputSampleR * biquadB[3]) - (tempSampleR * biquadB[5]) + biquadB[10];
        biquadB[10] = (inputSampleR * biquadB[4]) - (tempSampleR * biquadB[6]);
        inputSampleR = tempSampleR;

        if (inputSampleL > 1.0) inputSampleL = 1.0;
        if (inputSampleL < -1.0) inputSampleL = -1.0;
        if (inputSampleR > 1.0) inputSampleR = 1.0;
        if (inputSampleR < -1.0) inputSampleR = -1.0;
        // hard clamp: prevents the NaN/DC-blast condition at full feedback

        inputSampleL = std::asin(inputSampleL);
        inputSampleR = std::asin(inputSampleR);

        tempSampleL = (inputSampleL * biquadC[2]) + biquadC[7];
        biquadC[7] = (inputSampleL * biquadC[3]) - (tempSampleL * biquadC[5]) + biquadC[8];
        biquadC[8] = (inputSampleL * biquadC[4]) - (tempSampleL * biquadC[6]);
        inputSampleL = tempSampleL;

        tempSampleR = (inputSampleR * biquadC[2]) + biquadC[9];
        biquadC[9] = (inputSampleR * biquadC[3]) - (tempSampleR * biquadC[5]) + biquadC[10];
        biquadC[10] = (inputSampleR * biquadC[4]) - (tempSampleR * biquadC[6]);
        inputSampleR = tempSampleR;

        // our own dry/wet: mix=0 dry passthrough, mix=1 full wet
        inputSampleL = (drySampleL * (1.0 - mix_)) + (inputSampleL * mix_);
        inputSampleR = (drySampleR * (1.0 - mix_)) + (inputSampleR * mix_);

        // begin 32 bit stereo floating point dither
        int expon; std::frexp((float)inputSampleL, &expon);
        fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
        inputSampleL += ((double(fpdL) - uint32_t(0x7fffffff)) * 5.5e-36 * std::pow(2, expon + 62));
        std::frexp((float)inputSampleR, &expon);
        fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
        inputSampleR += ((double(fpdR) - uint32_t(0x7fffffff)) * 5.5e-36 * std::pow(2, expon + 62));
        // end 32 bit stereo floating point dither

        l = (float)inputSampleL;
        r = (float)inputSampleR;
    }

private:
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    void recalc() {
        double sr = (double)fs_;
        biquadC[0] = biquadB[0] = biquadA[0] = ((std::pow((double)A_, 2) * 9900.0) + 100.0) / sr;
        biquadA[1] = 0.618033988749894848204586;
        biquadB[1] = ((double)A_ * 0.5) + 0.118033988749894848204586;
        biquadC[1] = 0.5;

        double K = std::tan(EFX_AIRWIN_PI * biquadA[0]);
        double norm = 1.0 / (1.0 + K / biquadA[1] + K * K);
        biquadA[2] = K * K * norm;
        biquadA[3] = 2.0 * biquadA[2];
        biquadA[4] = biquadA[2];
        biquadA[5] = 2.0 * (K * K - 1.0) * norm;
        biquadA[6] = (1.0 - K / biquadA[1] + K * K) * norm;

        K = std::tan(EFX_AIRWIN_PI * biquadA[0]);
        norm = 1.0 / (1.0 + K / biquadB[1] + K * K);
        biquadB[2] = K * K * norm;
        biquadB[3] = 2.0 * biquadB[2];
        biquadB[4] = biquadB[2];
        biquadB[5] = 2.0 * (K * K - 1.0) * norm;
        biquadB[6] = (1.0 - K / biquadB[1] + K * K) * norm;

        K = std::tan(EFX_AIRWIN_PI * biquadC[0]);
        norm = 1.0 / (1.0 + K / biquadC[1] + K * K);
        biquadC[2] = K * K * norm;
        biquadC[3] = 2.0 * biquadC[2];
        biquadC[4] = biquadC[2];
        biquadC[5] = 2.0 * (K * K - 1.0) * norm;
        biquadC[6] = (1.0 - K / biquadC[1] + K * K) * norm;

        double size = (std::pow((double)B_, 2) * 99.0) + 1.0;
        damping_ = std::pow((double)C_, 2) * 0.5;
        rawPass_ = (double)D_;
        feedback_ = 1.0 - std::pow(1.0 - (double)E_, 4);

        delayA = (int)(79 * size);
        delayB = (int)(73 * size);
        delayC = (int)(71 * size);
        delayD = (int)(67 * size);
        delayE = (int)(61 * size);
        delayF = (int)(59 * size);
        delayG = (int)(53 * size);
        delayH = (int)(47 * size);
        delayI = (int)(43 * size);
        delayJ = (int)(41 * size);
        delayK = (int)(37 * size);
        delayL = (int)(31 * size);
    }

    double biquadA[11], biquadB[11], biquadC[11];

    // delay buffers (original max sizes: delay*100 + headroom; SR-independent)
    float aAL[8111], aBL[7511], aCL[7311], aDL[6911];
    float aEL[6311], aFL[6111], aGL[5511], aHL[4911];
    float aIL[4511], aJL[4311], aKL[3911], aLL[3311];
    float aAR[8111], aBR[7511], aCR[7311], aDR[6911];
    float aER[6311], aFR[6111], aGR[5511], aHR[4911];
    float aIR[4511], aJR[4311], aKR[3911], aLR[3311];

    int countA, delayA;
    int countB, delayB;
    int countC, delayC;
    int countD, delayD;
    int countE, delayE;
    int countF, delayF;
    int countG, delayG;
    int countH, delayH;
    int countI, delayI;
    int countJ, delayJ;
    int countK, delayK;
    int countL, delayL;

    double feedbackAL, feedbackBL, feedbackCL, feedbackDL;
    double feedbackEL, feedbackFL, feedbackGL, feedbackHL;
    double feedbackAR, feedbackBR, feedbackCR, feedbackDR;
    double feedbackER, feedbackFR, feedbackGR, feedbackHR;

    uint32_t fpdL, fpdR;

    // params: A Filter(fixed 1.0 open), B Size(size), C Damping(damp),
    // D Allpass/rawPass(fixed 1.0), E Feedback(feedback). mix_ is our dry/wet.
    float A_ = 1.0f, B_ = 0.5f, C_ = 0.0f, D_ = 1.0f, E_ = 1.0f, mix_ = 1.0f;

    // cached derived
    double damping_ = 0.0, rawPass_ = 1.0, feedback_ = 1.0;

    float fs_ = 44100.0f;
};

} // namespace efx
