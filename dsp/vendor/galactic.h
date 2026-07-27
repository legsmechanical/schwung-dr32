#pragma once
// ============================================================================
//  Galactic - faithful C++ port of the Airwindows "Galactic" reverb.
//  Original algorithm (c) Chris Johnson / Airwindows, MIT license.
//  A 4-wide Householder-matrix feedback-delay reverb with a quadrature
//  pitch-shift (detune) stereo-widening predelay.
//
//  Ported to a single-sample, RT-safe, self-contained struct for Schwung.
//  Source of truth: MacVST/Galactic processReplacing (float path).
// ============================================================================
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#ifndef EFX_GALACTIC_PI
#define EFX_GALACTIC_PI 3.141592653589793238
#endif


// PERFORMANCE NOTE: the dither literals here were `5.5e-36l`. The `l` suffix
// makes that arithmetic LONG DOUBLE, which on aarch64 is IEEE binary128
// emulated in software -- measured at ~1.2% of a Move core per stage elsewhere
// in this module. Dropped to double; the dither sits ~157 dB down and is
// unaffected in any audible sense. Same applies to pow(2,N) -> ldexp(1.0,N),
// which is bit-identical for integer N.

namespace efx {

struct Galactic {
    Galactic() { reset(); recalc(); }

    void setSampleRate(float fs) {
        fs_ = (fs > 0.0f) ? fs : 44100.0f;
        recalc();
    }

    // All inputs 0..1.
    //   size   -> Galactic D "Bigness"   (reverb buffer size / space)
    //   bright -> Galactic B "Brightness"(into+out-of the reverb lowpass)
    //   detune -> Galactic C "Detune"    (quadrature pitch-shift widening)
    //   mix    -> Galactic E "Dry/Wet"   (0 = dry passthrough, 1 = full wet)
    // Galactic A "Replace" (regen amount) is not exposed; held at its 0.5 default.
    void setParams(float size, float bright, float detune, float mix) {
        A_ = 0.5f;
        B_ = clamp01(bright);
        C_ = clamp01(detune);
        D_ = clamp01(size);
        E_ = clamp01(mix);
        recalc();
    }

    void reset() {
        iirAL = iirAR = iirBL = iirBR = 0.0;

        std::memset(aIL, 0, sizeof(aIL)); std::memset(aIR, 0, sizeof(aIR));
        std::memset(aJL, 0, sizeof(aJL)); std::memset(aJR, 0, sizeof(aJR));
        std::memset(aKL, 0, sizeof(aKL)); std::memset(aKR, 0, sizeof(aKR));
        std::memset(aLL, 0, sizeof(aLL)); std::memset(aLR, 0, sizeof(aLR));
        std::memset(aAL, 0, sizeof(aAL)); std::memset(aAR, 0, sizeof(aAR));
        std::memset(aBL, 0, sizeof(aBL)); std::memset(aBR, 0, sizeof(aBR));
        std::memset(aCL, 0, sizeof(aCL)); std::memset(aCR, 0, sizeof(aCR));
        std::memset(aDL, 0, sizeof(aDL)); std::memset(aDR, 0, sizeof(aDR));
        std::memset(aEL, 0, sizeof(aEL)); std::memset(aER, 0, sizeof(aER));
        std::memset(aFL, 0, sizeof(aFL)); std::memset(aFR, 0, sizeof(aFR));
        std::memset(aGL, 0, sizeof(aGL)); std::memset(aGR, 0, sizeof(aGR));
        std::memset(aHL, 0, sizeof(aHL)); std::memset(aHR, 0, sizeof(aHR));
        std::memset(aML, 0, sizeof(aML)); std::memset(aMR, 0, sizeof(aMR));

        feedbackAL = feedbackAR = 0.0;
        feedbackBL = feedbackBR = 0.0;
        feedbackCL = feedbackCR = 0.0;
        feedbackDL = feedbackDR = 0.0;

        for (int i = 0; i < 7; ++i) { lastRefL[i] = 0.0; lastRefR[i] = 0.0; }

        countI = countJ = countK = countL = 1;
        countA = countB = countC = countD = 1;
        countE = countF = countG = countH = 1;
        countM = 1;
        cycle = 0;

        vibM = 3.0;
        oldfpd = 429496.7295;

        fpdL = 1; while (fpdL < 16386) fpdL = (uint32_t)(rand() * (double)UINT32_MAX);
        fpdR = 1; while (fpdR < 16386) fpdR = (uint32_t)(rand() * (double)UINT32_MAX);
    }

    // Process one stereo sample in place.
    void tick(float &l, float &r) {
        double inputSampleL = l;
        double inputSampleR = r;
        if (std::fabs(inputSampleL) < 1.18e-23) inputSampleL = fpdL * 1.18e-17;
        if (std::fabs(inputSampleR) < 1.18e-23) inputSampleR = fpdR * 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        vibM += (oldfpd * drift_);
        if (vibM > (EFX_GALACTIC_PI * 2.0)) {
            vibM = 0.0;
            oldfpd = 0.4294967295 + (fpdL * 0.0000000000618);
        }

        aML[countM] = inputSampleL * attenuate_;
        aMR[countM] = inputSampleR * attenuate_;
        countM++; if (countM < 0 || countM > delayM) countM = 0;

        double offsetML = (std::sin(vibM) + 1.0) * 127;
        double offsetMR = (std::sin(vibM + (EFX_GALACTIC_PI / 2.0)) + 1.0) * 127;
        int workingML = countM + (int)offsetML;
        int workingMR = countM + (int)offsetMR;
        double interpolML = (aML[workingML - ((workingML > delayM) ? delayM + 1 : 0)] * (1 - (offsetML - std::floor(offsetML))));
        interpolML += (aML[workingML + 1 - ((workingML + 1 > delayM) ? delayM + 1 : 0)] * ((offsetML - std::floor(offsetML))));
        double interpolMR = (aMR[workingMR - ((workingMR > delayM) ? delayM + 1 : 0)] * (1 - (offsetMR - std::floor(offsetMR))));
        interpolMR += (aMR[workingMR + 1 - ((workingMR + 1 > delayM) ? delayM + 1 : 0)] * ((offsetMR - std::floor(offsetMR))));
        inputSampleL = interpolML;
        inputSampleR = interpolMR;
        // predelay that applies vibrato

        iirAL = (iirAL * (1.0 - lowpass_)) + (inputSampleL * lowpass_); inputSampleL = iirAL;
        iirAR = (iirAR * (1.0 - lowpass_)) + (inputSampleR * lowpass_); inputSampleR = iirAR;
        // initial filter

        cycle++;
        if (cycle == cycleEnd_) { // hit the end point and we do a reverb sample
            aIL[countI] = inputSampleL + (feedbackAR * regen_);
            aJL[countJ] = inputSampleL + (feedbackBR * regen_);
            aKL[countK] = inputSampleL + (feedbackCR * regen_);
            aLL[countL] = inputSampleL + (feedbackDR * regen_);
            aIR[countI] = inputSampleR + (feedbackAL * regen_);
            aJR[countJ] = inputSampleR + (feedbackBL * regen_);
            aKR[countK] = inputSampleR + (feedbackCL * regen_);
            aLR[countL] = inputSampleR + (feedbackDL * regen_);

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
            // first block: now we have four outputs

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
            // second block: four more outputs

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
            // third block: final outputs

            feedbackAL = (outEL - (outFL + outGL + outHL));
            feedbackBL = (outFL - (outEL + outGL + outHL));
            feedbackCL = (outGL - (outEL + outFL + outHL));
            feedbackDL = (outHL - (outEL + outFL + outGL));
            feedbackAR = (outER - (outFR + outGR + outHR));
            feedbackBR = (outFR - (outER + outGR + outHR));
            feedbackCR = (outGR - (outER + outFR + outHR));
            feedbackDR = (outHR - (outER + outFR + outGR));
            // which we need to feed back into the input again, a bit

            inputSampleL = (outEL + outFL + outGL + outHL) / 8.0;
            inputSampleR = (outER + outFR + outGR + outHR) / 8.0;
            // and take the final combined sum of outputs
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
            cycle = 0; // reset
            inputSampleL = lastRefL[cycle];
            inputSampleR = lastRefR[cycle];
        } else {
            inputSampleL = lastRefL[cycle];
            inputSampleR = lastRefR[cycle];
            // we are going through our references now
        }

        iirBL = (iirBL * (1.0 - lowpass_)) + (inputSampleL * lowpass_); inputSampleL = iirBL;
        iirBR = (iirBR * (1.0 - lowpass_)) + (inputSampleR * lowpass_); inputSampleR = iirBR;
        // end filter

        if (wet_ < 1.0) {
            inputSampleL = (inputSampleL * wet_) + (drySampleL * (1.0 - wet_));
            inputSampleR = (inputSampleR * wet_) + (drySampleR * (1.0 - wet_));
        }

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

    // Recompute everything the block-level code in the original derived from
    // params + sample rate. Cheap; called on any param/SR change.
    void recalc() {
        double overallscale = 1.0;
        overallscale /= 44100.0;
        overallscale *= (double)fs_;

        cycleEnd_ = (int)std::floor(overallscale);
        if (cycleEnd_ < 1) cycleEnd_ = 1;
        if (cycleEnd_ > 4) cycleEnd_ = 4;
        if (cycle > cycleEnd_ - 1) cycle = cycleEnd_ - 1; // sanity check

        regen_ = 0.0625 + ((1.0 - A_) * 0.0625);
        attenuate_ = (1.0 - (regen_ / 0.125)) * 1.333;
        lowpass_ = std::pow(1.00001 - (1.0 - (double)B_), 2.0) / std::sqrt(overallscale);
        drift_ = std::pow((double)C_, 3) * 0.001;
        double size = ((double)D_ * 1.77) + 0.1;
        wet_ = 1.0 - (std::pow(1.0 - (double)E_, 3));

        delayI = (int)(3407.0 * size);
        delayJ = (int)(1823.0 * size);
        delayK = (int)(859.0 * size);
        delayL = (int)(331.0 * size);
        delayA = (int)(4801.0 * size);
        delayB = (int)(2909.0 * size);
        delayC = (int)(1153.0 * size);
        delayD = (int)(461.0 * size);
        delayE = (int)(7607.0 * size);
        delayF = (int)(4217.0 * size);
        delayG = (int)(2269.0 * size);
        delayH = (int)(1597.0 * size);
        delayM = 256;
    }

    // --- delay buffers (sized for the original's max D*1.77+0.1 space) ---
    float aIL[6480], aJL[3660], aKL[1720], aLL[680];
    float aAL[9700], aBL[6000], aCL[2320], aDL[940];
    float aEL[15220], aFL[8460], aGL[4540], aHL[3200];
    float aIR[6480], aJR[3660], aKR[1720], aLR[680];
    float aAR[9700], aBR[6000], aCR[2320], aDR[940];
    float aER[15220], aFR[8460], aGR[4540], aHR[3200];
    float aML[3111], aMR[3111];

    double iirAL, iirBL, iirAR, iirBR;

    double vibM, oldfpd;

    double feedbackAL, feedbackBL, feedbackCL, feedbackDL;
    double feedbackAR, feedbackBR, feedbackCR, feedbackDR;

    double lastRefL[7], lastRefR[7];

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
    int countM, delayM;
    int cycle;

    uint32_t fpdL, fpdR;

    // params (Galactic A..E)
    float A_ = 0.5f, B_ = 0.5f, C_ = 0.5f, D_ = 1.0f, E_ = 1.0f;

    // cached derived values
    int cycleEnd_ = 1;
    double regen_ = 0.09375, attenuate_ = 0.0, lowpass_ = 1.0, drift_ = 0.0, wet_ = 1.0;

    float fs_ = 44100.0f;
};

} // namespace efx
