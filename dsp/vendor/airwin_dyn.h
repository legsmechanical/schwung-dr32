#pragma once
// ============================================================================
//  airwin_dyn.h - Airwindows dynamics, transplanted for DR32s Drum Buss.
//
//    Pop3   -> the Compress stage. A real threshold-and-ratio compressor with
//              independent attack and release. Its gain is
//              (1-ratio) + (popComp*ratio) with popComp clamped to [0,1], so it
//              can only ever ATTENUATE -- measured 0.00 dB of lift on a -48
//              dBFS sine at every setting, where the Pressure4 stage it
//              replaces lifted the same signal by +17.7 dB. That structural
//              property is the whole reason it is here: a drum bus must not
//              drag up the sample noise floor and room bleed.
//              It also carries a gate/sustain section (tail shortening).
//
//    Point  -> the Transients stage. Ratio of a fast and a slow leaky
//              integrator, with the SLOW ones speed moved to set direction.
//              Unlike a difference-of-envelopes it returns to exactly unity in
//              steady state, and the tail moves OPPOSITE the attack (measured
//              -5.9 dB attack / +0.7 dB tail one way, +5.5 / -1.1 the other),
//              which is real attack-vs-sustain separation.
//
//  Original algorithms (c) Chris Johnson / Airwindows, MIT licence.
//  Transplanted VERBATIM by tools/port_airwindows.py; both null against
//  upstream at about -142 dB (the dither floor). See vendor/SOURCES.md.
// ============================================================================
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>

// upstream bodies occasionally name this VST type internally
typedef int VstInt32;

// PERFORMANCE NOTES. Two changes to the transplanted DSP, both confined to the
// dither, which sits about 157 dB below signal:
//
//  1. `5.5e-36l` -> `5.5e-36`. The `l` suffix makes the dither arithmetic LONG
//     DOUBLE, which on aarch64 is IEEE binary128 emulated in SOFTWARE. That one
//     character cost ~1.2% of a Move core PER STAGE: measured per-stage, Pop3
//     alone was 1.25% and Point 1.27%, against 0.10% for the tanh saturator.
//     On x86 and Apple Silicon long double is cheap hardware, which is why this
//     was invisible off-device.
//  2. `pow(2, expon+62)` -> `ldexp(1.0, expon+62)`, bit-identical for integer
//     exponents, an exponent-field write instead of a transcendental.
//
// Both verified still null against upstream (see tools/port_airwindows.py).

// Point's upstream loop ends with `*in1++;` (a dereference whose result is
// discarded). It is harmless, and the transplant is deliberately verbatim, so
// the warning is silenced here rather than the vendored DSP edited.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"

// ---------------------------------------------------------------------------
//  Pop3 -- (c) Chris Johnson / Airwindows, MIT licence.
//  Transplanted from the upstream LinuxVST source by tools/port_airwindows.py:
//  the processReplacing body and all state are VERBATIM, only the VST base
//  class and parameter plumbing are replaced. Null-tested against upstream.
// ---------------------------------------------------------------------------
namespace awk_pop3 {

struct Pop3 {
    // upstream parameter defaults
    float A = 1.0; float B = 0.5; float C = 0.5; float D = 0.5; float E = 0.0; float F = 0.5; float G = 0.5; float H = 0.5;
    double sampleRate = 44100.0;
    double getSampleRate() const { return sampleRate; }
    void setSampleRate(double sr) { sampleRate = sr; }

	double popCompL;
	double popCompR;
	double popGate;
	uint32_t fpdL;
	uint32_t fpdR;

    Pop3() { reset(); }
    void reset() {
    	popCompL = 1.0;
    	popCompR = 1.0;
    	popGate = 1.0;
    	fpdL = 1.0; while (fpdL < 16386) fpdL = rand()*UINT32_MAX;
    	fpdR = 1.0; while (fpdR < 16386) fpdR = rand()*UINT32_MAX;
    	//this is reset: values being initialized only once. Startup values, whatever they are.
    }

    void processReplacing(float **inputs, float **outputs, int sampleFrames) {
    
        float* in1  =  inputs[0];
        float* in2  =  inputs[1];
        float* out1 = outputs[0];
        float* out2 = outputs[1];
    
    	double overallscale = 1.0;
    	overallscale /= 44100.0;
    	overallscale *= getSampleRate();
    	
    	double compThresh = pow(A,4);
    	double compRatio = 1.0-pow(1.0-B,2);
    	double compAttack = 1.0/(((pow(C,3)*5000.0)+500.0)*overallscale);
    	double compRelease = 1.0/(((pow(D,5)*50000.0)+500.0)*overallscale);
    	
    	double gateThresh = pow(E,4);
    	double gateRatio = 1.0-pow(1.0-F,2);
    	double gateSustain = M_PI_2 * pow(G+1.0,4.0);
    	double gateRelease = 1.0/(((pow(H,5)*500000.0)+500.0)*overallscale);
    	
        while (--sampleFrames >= 0)
        {
    		double inputSampleL = *in1;
    		double inputSampleR = *in2;
    		if (fabs(inputSampleL)<1.18e-23) inputSampleL = fpdL * 1.18e-17;
    		if (fabs(inputSampleR)<1.18e-23) inputSampleR = fpdR * 1.18e-17;
    		
    		if (fabs(inputSampleL) > compThresh) { //compression L
    			popCompL -= (popCompL * compAttack);
    			popCompL += ((compThresh / fabs(inputSampleL))*compAttack);
    		} else popCompL = (popCompL*(1.0-compRelease))+compRelease;
    		if (fabs(inputSampleR) > compThresh) { //compression R
    			popCompR -= (popCompR * compAttack);
    			popCompR += ((compThresh / fabs(inputSampleR))*compAttack);
    		} else popCompR = (popCompR*(1.0-compRelease))+compRelease;
    		if (popCompL > popCompR) popCompL -= (popCompL * compAttack);
    		if (popCompR > popCompL) popCompR -= (popCompR * compAttack);
    		if (fabs(inputSampleL) > gateThresh) popGate = gateSustain;
    		else if (fabs(inputSampleR) > gateThresh) popGate = gateSustain;
    		else popGate *= (1.0-gateRelease);
    		if (popGate < 0.0) popGate = 0.0;
    		popCompL = fmax(fmin(popCompL,1.0),0.0);
    		popCompR = fmax(fmin(popCompR,1.0),0.0);
    		inputSampleL *= ((1.0-compRatio)+(popCompL*compRatio));
    		inputSampleR *= ((1.0-compRatio)+(popCompR*compRatio));
    		if (popGate < M_PI_2) {
    			inputSampleL *= ((1.0-gateRatio)+(sin(popGate)*gateRatio));
    			inputSampleR *= ((1.0-gateRatio)+(sin(popGate)*gateRatio));
    		}
    		
    		//begin 32 bit stereo floating point dither
    		int expon; frexpf((float)inputSampleL, &expon);
    		fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
    		inputSampleL += ((double(fpdL)-uint32_t(0x7fffffff)) * 5.5e-36 * ldexp(1.0,expon+62));
    		frexpf((float)inputSampleR, &expon);
    		fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
    		inputSampleR += ((double(fpdR)-uint32_t(0x7fffffff)) * 5.5e-36 * ldexp(1.0,expon+62));
    		//end 32 bit stereo floating point dither
    		
    		*out1 = inputSampleL;
    		*out2 = inputSampleR;
    
    		in1++;
    		in2++;
    		out1++;
    		out2++;
        }
    }
};
} // namespace awk_pop3


// ---------------------------------------------------------------------------
//  Point -- (c) Chris Johnson / Airwindows, MIT licence.
//  Transplanted from the upstream LinuxVST source by tools/port_airwindows.py:
//  the processReplacing body and all state are VERBATIM, only the VST base
//  class and parameter plumbing are replaced. Null-tested against upstream.
// ---------------------------------------------------------------------------
namespace awk_point {

struct Point {
    // upstream parameter defaults
    float A = 0.5; float B = 0.5; float C = 0.5;
    double sampleRate = 44100.0;
    double getSampleRate() const { return sampleRate; }
    void setSampleRate(double sr) { sampleRate = sr; }

	uint32_t fpdL;
	uint32_t fpdR;
	bool fpFlip;
	double nibAL;
	double nobAL;
	double nibBL;
	double nobBL;
	double nibAR;
	double nobAR;
	double nibBR;
	double nobBR;

    Point() { reset(); }
    void reset() {
    	nibAL = 0.0;
    	nobAL = 0.0;
    	nibBL = 0.0;
    	nobBL = 0.0;
    	nibAR = 0.0;
    	nobAR = 0.0;
    	nibBR = 0.0;
    	nobBR = 0.0;
    	fpdL = 1.0; while (fpdL < 16386) fpdL = rand()*UINT32_MAX;
    	fpdR = 1.0; while (fpdR < 16386) fpdR = rand()*UINT32_MAX;
    	fpFlip = true;
    	//this is reset: values being initialized only once. Startup values, whatever they are.
    }

    void processReplacing(float **inputs, float **outputs, int sampleFrames) {
    
        float* in1  =  inputs[0];
        float* in2  =  inputs[1];
        float* out1 = outputs[0];
        float* out2 = outputs[1];
    
    	double overallscale = 1.0;
    	overallscale /= 44100.0;
    	overallscale *= getSampleRate();
    	
    	double gaintrim = pow(10.0,((A*24.0)-12.0)/20);
    	double nibDiv = 1 / pow(C+0.2,7);
    	nibDiv /= overallscale;
    	double nobDiv;
    	if (((B*2.0)-1.0) > 0) nobDiv = nibDiv / (1.001-((B*2.0)-1.0));
    	else nobDiv = nibDiv * (1.001-pow(((B*2.0)-1.0)*0.75,2));
    	double nibnobFactor = 0.0; //start with the fallthrough value, why not
    	double absolute;
    	
    	double inputSampleL;
    	double inputSampleR;
    	    
        while (--sampleFrames >= 0)
        {
    		inputSampleL = *in1;
    		inputSampleR = *in2;
    		if (fabs(inputSampleL)<1.18e-23) inputSampleL = fpdL * 1.18e-17;
    		if (fabs(inputSampleR)<1.18e-23) inputSampleR = fpdR * 1.18e-17;
    
    		inputSampleL *= gaintrim;
    		absolute = fabs(inputSampleL);
    		if (fpFlip)
    		{
    			nibAL = nibAL + (absolute / nibDiv);
    			nibAL = nibAL / (1 + (1/nibDiv));
    			nobAL = nobAL + (absolute / nobDiv);
    			nobAL = nobAL / (1 + (1/nobDiv));
    			if (nobAL > 0)
    			{
    				nibnobFactor = nibAL / nobAL;
    			}
    		}
    		else
    		{
    			nibBL = nibBL + (absolute / nibDiv);
    			nibBL = nibBL / (1 + (1/nibDiv));
    			nobBL = nobBL + (absolute / nobDiv);
    			nobBL = nobBL / (1 + (1/nobDiv));
    			if (nobBL > 0)
    			{
    				nibnobFactor = nibBL / nobBL;
    			}		
    		}
    		inputSampleL *= nibnobFactor;
    		
    		
    		inputSampleR *= gaintrim;
    		absolute = fabs(inputSampleR);
    		if (fpFlip)
    		{
    			nibAR = nibAR + (absolute / nibDiv);
    			nibAR = nibAR / (1 + (1/nibDiv));
    			nobAR = nobAR + (absolute / nobDiv);
    			nobAR = nobAR / (1 + (1/nobDiv));
    			if (nobAR > 0)
    			{
    				nibnobFactor = nibAR / nobAR;
    			}
    		}
    		else
    		{
    			nibBR = nibBR + (absolute / nibDiv);
    			nibBR = nibBR / (1 + (1/nibDiv));
    			nobBR = nobBR + (absolute / nobDiv);
    			nobBR = nobBR / (1 + (1/nobDiv));
    			if (nobBR > 0)
    			{
    				nibnobFactor = nibBR / nobBR;
    			}		
    		}
    		inputSampleR *= nibnobFactor;
    		fpFlip = !fpFlip;
    		
    		//begin 32 bit stereo floating point dither
    		int expon; frexpf((float)inputSampleL, &expon);
    		fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
    		inputSampleL += ((double(fpdL)-uint32_t(0x7fffffff)) * 5.5e-36 * ldexp(1.0,expon+62));
    		frexpf((float)inputSampleR, &expon);
    		fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
    		inputSampleR += ((double(fpdR)-uint32_t(0x7fffffff)) * 5.5e-36 * ldexp(1.0,expon+62));
    		//end 32 bit stereo floating point dither
    
    		*out1 = inputSampleL;
    		*out2 = inputSampleR;
    
    		*in1++;
    		*in2++;
    		*out1++;
    		*out2++;
        }
    }
};
} // namespace awk_point


#pragma GCC diagnostic pop
