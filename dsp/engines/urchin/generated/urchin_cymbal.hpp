/* ------------------------------------------------------------
copyright: "Copyright (c) 2023 Punk Labs LLC"
license: "GPLv3 (or later)"
name: "OneTrick URCHIN DSP"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 0 -cn UrchinCymbal -dtl 65536 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __UrchinCymbal_H__
#define  __UrchinCymbal_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

/* link with : "" */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#ifndef FAUST_INT_WRAP
#define FAUST_INT_WRAP
inline int faust_wrap_add(int a, int b) { return int((unsigned int)a + (unsigned int)b); }
inline int faust_wrap_sub(int a, int b) { return int((unsigned int)a - (unsigned int)b); }
inline int faust_wrap_mul(int a, int b) { return int((unsigned int)a * (unsigned int)b); }
#endif


#ifndef FAUSTCLASS 
#define FAUSTCLASS UrchinCymbal
#endif

#ifdef __APPLE__ 
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

class UrchinCymbalSIG0 {
	
  private:
	
	int iVec6[2];
	int iRec24[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsUrchinCymbalSIG0() {
		return 0;
	}
	int getNumOutputsUrchinCymbalSIG0() {
		return 1;
	}
	
	void instanceInitUrchinCymbalSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			iVec6[l15] = 0;
		}
		for (int l16 = 0; l16 < 2; l16 = faust_wrap_add(l16, 1)) {
			iRec24[l16] = 0;
		}
	}
	
	void fillUrchinCymbalSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec6[0] = 1;
			iRec24[0] = (faust_wrap_add(iVec6[1], iRec24[1])) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec24[0]));
			iVec6[1] = iVec6[0];
			iRec24[1] = iRec24[0];
		}
	}

};

static UrchinCymbalSIG0* newUrchinCymbalSIG0() { return (UrchinCymbalSIG0*)new UrchinCymbalSIG0(); }
static void deleteUrchinCymbalSIG0(UrchinCymbalSIG0* dsp) { delete dsp; }

static float UrchinCymbal_faustpower2_f(float value) {
	return value * value;
}
static float UrchinCymbal_faustpower3_f(float value) {
	return value * value * value;
}
static float ftbl0UrchinCymbalSIG0[65536];

class UrchinCymbal : public dsp {
	
 private:
	
	FAUSTFLOAT fHslider0;
	FAUSTFLOAT fEntry0;
	FAUSTFLOAT fHslider1;
	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider2;
	FAUSTFLOAT fHslider3;
	float fConst2;
	float fConst3;
	FAUSTFLOAT fHslider4;
	float fConst4;
	FAUSTFLOAT fEntry1;
	FAUSTFLOAT fHslider5;
	FAUSTFLOAT fEntry2;
	float fConst5;
	FAUSTFLOAT fButton0;
	float fVec0[2];
	int iVec1[2];
	float fRec3[2];
	float fConst6;
	FAUSTFLOAT fHslider6;
	int iRec18[2];
	float fConst7;
	int iConst8;
	int iRec19[2];
	float fRec17[2];
	float fRec20[2];
	float fRec16[2];
	float fConst9;
	float fConst10;
	FAUSTFLOAT fEntry3;
	int IOTA0;
	float fVec2[4096];
	float fConst11;
	float fConst12;
	FAUSTFLOAT fHslider7;
	float fVec3[2];
	int iVec4[2];
	int iVec5[2];
	float fConst13;
	float fRec23[2];
	float fRec22[2];
	float fRec21[2];
	FAUSTFLOAT fHslider8;
	FAUSTFLOAT fHslider9;
	float fConst14;
	float fConst15;
	float fRec25[2];
	float fConst16;
	float fRec26[2];
	float fConst17;
	float fRec15[2048];
	float fRec29[2];
	float fRec30[2];
	float fRec28[2];
	float fConst18;
	float fRec27[2048];
	float fRec33[2];
	float fRec34[2];
	float fRec32[2];
	float fConst19;
	float fRec31[2048];
	float fRec37[2];
	float fRec38[2];
	float fRec36[2];
	float fConst20;
	float fRec35[4096];
	float fConst21;
	float fConst22;
	float fRec40[2];
	float fRec39[2];
	float fConst23;
	float fRec14[3];
	float fConst24;
	FAUSTFLOAT fHslider10;
	float fConst25;
	FAUSTFLOAT fHslider11;
	int iRec45[2];
	float fRec44[2];
	float fRec46[2];
	float fRec43[2];
	float fRec42[3];
	float fVec7[2];
	float fRec41[2];
	float fRec48[2];
	float fRec47[2];
	float fRec13[3];
	float fConst26;
	float fRec12[3];
	float fRec11[3];
	float fRec10[3];
	float fRec9[3];
	float fConst27;
	float fRec8[3];
	float fConst28;
	float fRec7[3];
	float fConst29;
	float fConst30;
	float fConst31;
	float fRec6[3];
	float fConst32;
	float fRec5[3];
	float fConst33;
	float fRec4[3];
	float fRec50[3];
	float fRec49[3];
	int iRec51[2];
	float fConst34;
	float fConst35;
	FAUSTFLOAT fHslider12;
	float fRec52[2];
	float fConst36;
	float fRec53[2];
	float fConst37;
	float fConst38;
	int iRec54[2];
	float fVec8[2];
	float fRec55[2];
	float fRec2[3];
	FAUSTFLOAT fHslider13;
	float fRec1[3];
	float fRec0[3];
	FAUSTFLOAT fHslider14;
	float fRec56[2];
	float fRec58[2];
	int iVec9[2];
	int iRec57[2];
	float fConst39;
	int iRec59[2];
	FAUSTFLOAT fHslider15;
	
 public:
	UrchinCymbal() {
	}
	
	UrchinCymbal(const UrchinCymbal&) = default;
	
	virtual ~UrchinCymbal() = default;
	
	UrchinCymbal& operator=(const UrchinCymbal&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/downSample:author", "Romain Michon");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/sAndH:author", "Romain Michon");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 0 -cn UrchinCymbal -dtl 65536 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("envelopes.lib/adsr:author", "Yann Orlarey and Andrey Bundin");
		m->declare("envelopes.lib/ar:author", "Yann Orlarey, Stéphane Letz");
		m->declare("envelopes.lib/author", "GRAME");
		m->declare("envelopes.lib/copyright", "GRAME");
		m->declare("envelopes.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("envelopes.lib/name", "Faust Envelope Library");
		m->declare("envelopes.lib/version", "1.3.0");
		m->declare("filename", "cymbal.dsp");
		m->declare("filters.lib/fb_comb_common:author", "Oleg Nesterov");
		m->declare("filters.lib/fb_fcomb:author", "Julius O. Smith III");
		m->declare("filters.lib/fb_fcomb:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>, revised by Oleg Nesterov");
		m->declare("filters.lib/fb_fcomb:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/highpass:author", "Julius O. Smith III");
		m->declare("filters.lib/highpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/highpass:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass0_highpass1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass0_highpass1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowpass:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/notchw:author", "Julius O. Smith III");
		m->declare("filters.lib/notchw:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/notchw:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/peak_eq:author", "Julius O. Smith III");
		m->declare("filters.lib/peak_eq:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/peak_eq:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf1:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf1s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1s:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf2s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2s:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/version", "1.9.0");
		m->declare("interpolators.lib/interpolate_linear:author", "Stéphane Letz");
		m->declare("interpolators.lib/interpolate_linear:licence", "MIT");
		m->declare("interpolators.lib/name", "Faust Interpolator Library");
		m->declare("interpolators.lib/remap:author", "David Braun");
		m->declare("interpolators.lib/version", "1.6.0");
		m->declare("license", "GPLv3 (or later)");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("misceffects.lib/dryWetMixer:author", "David Braun, revised by Stéphane Letz");
		m->declare("misceffects.lib/name", "Misc Effects Library");
		m->declare("misceffects.lib/version", "2.6.0");
		m->declare("name", "OneTrick URCHIN DSP");
		m->declare("noises.lib/name", "Faust Noise Generator Library");
		m->declare("noises.lib/version", "1.6.0");
		m->declare("onetrick.lib/copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("onetrick.lib/license", "GPLv3 (or later)");
		m->declare("onetrick.lib/name", "OneTrick DSP Library");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.8.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("shared.lib/copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("shared.lib/license", "GPLv3 (or later)");
		m->declare("shared.lib/name", "OneTrick URCHIN DSP");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.7.0");
	}

	virtual int getNumInputs() {
		return 0;
	}
	virtual int getNumOutputs() {
		return 3;
	}
	
	static void classInit(int sample_rate) {
		UrchinCymbalSIG0* sig0 = newUrchinCymbalSIG0();
		sig0->instanceInitUrchinCymbalSIG0(sample_rate);
		sig0->fillUrchinCymbalSIG0(65536, ftbl0UrchinCymbalSIG0);
		deleteUrchinCymbalSIG0(sig0);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 3141.5928f / fConst0;
		fConst2 = 1570.7964f / fConst0;
		fConst3 = 6283.1855f / fConst0;
		fConst4 = 628.31854f / fConst0;
		fConst5 = std::exp(-(1e+02f / fConst0));
		fConst6 = 78.53982f / fConst0;
		fConst7 = fConst0 / std::min<float>(std::min<float>(4.8e+04f, fConst0), fConst0);
		iConst8 = static_cast<int>(fConst7);
		fConst9 = std::exp(-(1e+03f / fConst0));
		fConst10 = 1.0f - fConst9;
		fConst11 = 0.02f * fConst0;
		fConst12 = 0.001f * fConst0;
		fConst13 = 1.0f / fConst0;
		fConst14 = 0.008296002f * fConst0;
		fConst15 = 15.31f / fConst0;
		fConst16 = 9.31f / fConst0;
		fConst17 = fConst0 + 1.0f;
		fConst18 = 0.0061207004f * fConst0;
		fConst19 = 0.0052731493f * fConst0;
		fConst20 = 0.011764706f * fConst0;
		fConst21 = 3.1415927f / fConst0;
		fConst22 = 0.475f * fConst0;
		fConst23 = 6.2831855f / fConst0;
		fConst24 = 5.5866294f / fConst0;
		fConst25 = std::log(0.0010795455f * fConst0);
		fConst26 = 942.4778f / fConst0;
		fConst27 = 2607.522f / fConst0;
		fConst28 = 7728.318f / fConst0;
		fConst29 = 4084.0706f / fConst0;
		fConst30 = 3134.1506f / fConst0;
		fConst31 = 8168.141f / fConst0;
		fConst32 = 12566.371f / fConst0;
		fConst33 = 28902.652f / fConst0;
		fConst34 = 0.003f * fConst0;
		fConst35 = 0.005f * fConst0;
		fConst36 = 0.00015f * fConst0;
		fConst37 = std::max<float>(1.0f, fConst36);
		fConst38 = 1.0f / fConst37;
		fConst39 = 1.0f / std::max<float>(1.0f, fConst12);
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.0f);
		fEntry0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(1.5f);
		fHslider3 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider4 = static_cast<FAUSTFLOAT>(14.0f);
		fEntry1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider5 = static_cast<FAUSTFLOAT>(0.0f);
		fEntry2 = static_cast<FAUSTFLOAT>(0.0f);
		fButton0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider6 = static_cast<FAUSTFLOAT>(5e+01f);
		fEntry3 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider7 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider8 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider9 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider10 = static_cast<FAUSTFLOAT>(75.0f);
		fHslider11 = static_cast<FAUSTFLOAT>(1e+02f);
		fHslider12 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider13 = static_cast<FAUSTFLOAT>(2e+01f);
		fHslider14 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider15 = static_cast<FAUSTFLOAT>(5e+01f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			fVec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			iVec1[l1] = 0;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec3[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			iRec18[l3] = 0;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			iRec19[l4] = 0;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec17[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = faust_wrap_add(l6, 1)) {
			fRec20[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec16[l7] = 0.0f;
		}
		IOTA0 = 0;
		for (int l8 = 0; l8 < 4096; l8 = faust_wrap_add(l8, 1)) {
			fVec2[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fVec3[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = faust_wrap_add(l10, 1)) {
			iVec4[l10] = 0;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			iVec5[l11] = 0;
		}
		for (int l12 = 0; l12 < 2; l12 = faust_wrap_add(l12, 1)) {
			fRec23[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec22[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			fRec21[l14] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fRec25[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = faust_wrap_add(l18, 1)) {
			fRec26[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2048; l19 = faust_wrap_add(l19, 1)) {
			fRec15[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fRec29[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			fRec30[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = faust_wrap_add(l22, 1)) {
			fRec28[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2048; l23 = faust_wrap_add(l23, 1)) {
			fRec27[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = faust_wrap_add(l24, 1)) {
			fRec33[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec34[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2; l26 = faust_wrap_add(l26, 1)) {
			fRec32[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2048; l27 = faust_wrap_add(l27, 1)) {
			fRec31[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2; l28 = faust_wrap_add(l28, 1)) {
			fRec37[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			fRec38[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2; l30 = faust_wrap_add(l30, 1)) {
			fRec36[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 4096; l31 = faust_wrap_add(l31, 1)) {
			fRec35[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2; l32 = faust_wrap_add(l32, 1)) {
			fRec40[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			fRec39[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 3; l34 = faust_wrap_add(l34, 1)) {
			fRec14[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = faust_wrap_add(l35, 1)) {
			iRec45[l35] = 0;
		}
		for (int l36 = 0; l36 < 2; l36 = faust_wrap_add(l36, 1)) {
			fRec44[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec46[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = faust_wrap_add(l38, 1)) {
			fRec43[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 3; l39 = faust_wrap_add(l39, 1)) {
			fRec42[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = faust_wrap_add(l40, 1)) {
			fVec7[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = faust_wrap_add(l41, 1)) {
			fRec41[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 2; l42 = faust_wrap_add(l42, 1)) {
			fRec48[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec47[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 3; l44 = faust_wrap_add(l44, 1)) {
			fRec13[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 3; l45 = faust_wrap_add(l45, 1)) {
			fRec12[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 3; l46 = faust_wrap_add(l46, 1)) {
			fRec11[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 3; l47 = faust_wrap_add(l47, 1)) {
			fRec10[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 3; l48 = faust_wrap_add(l48, 1)) {
			fRec9[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 3; l49 = faust_wrap_add(l49, 1)) {
			fRec8[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 3; l50 = faust_wrap_add(l50, 1)) {
			fRec7[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 3; l51 = faust_wrap_add(l51, 1)) {
			fRec6[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 3; l52 = faust_wrap_add(l52, 1)) {
			fRec5[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 3; l53 = faust_wrap_add(l53, 1)) {
			fRec4[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 3; l54 = faust_wrap_add(l54, 1)) {
			fRec50[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 3; l55 = faust_wrap_add(l55, 1)) {
			fRec49[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 2; l56 = faust_wrap_add(l56, 1)) {
			iRec51[l56] = 0;
		}
		for (int l57 = 0; l57 < 2; l57 = faust_wrap_add(l57, 1)) {
			fRec52[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 2; l58 = faust_wrap_add(l58, 1)) {
			fRec53[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = faust_wrap_add(l59, 1)) {
			iRec54[l59] = 0;
		}
		for (int l60 = 0; l60 < 2; l60 = faust_wrap_add(l60, 1)) {
			fVec8[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = faust_wrap_add(l61, 1)) {
			fRec55[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 3; l62 = faust_wrap_add(l62, 1)) {
			fRec2[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 3; l63 = faust_wrap_add(l63, 1)) {
			fRec1[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 3; l64 = faust_wrap_add(l64, 1)) {
			fRec0[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 2; l65 = faust_wrap_add(l65, 1)) {
			fRec56[l65] = 0.0f;
		}
		for (int l66 = 0; l66 < 2; l66 = faust_wrap_add(l66, 1)) {
			fRec58[l66] = 0.0f;
		}
		for (int l67 = 0; l67 < 2; l67 = faust_wrap_add(l67, 1)) {
			iVec9[l67] = 0;
		}
		for (int l68 = 0; l68 < 2; l68 = faust_wrap_add(l68, 1)) {
			iRec57[l68] = 0;
		}
		for (int l69 = 0; l69 < 2; l69 = faust_wrap_add(l69, 1)) {
			iRec59[l69] = 0;
		}
	}
	
	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}
	
	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}
	
	virtual UrchinCymbal* clone() {
		return new UrchinCymbal(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("OneTrick URCHIN DSP");
		ui_interface->addHorizontalSlider("Closed", &fHslider9, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.01f));
		ui_interface->addNumEntry("GainAdjustment", &fEntry0, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+02f), FAUSTFLOAT(12.0f), FAUSTFLOAT(0.1f));
		ui_interface->addNumEntry("PitchWheel", &fEntry2, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addNumEntry("Transpose", &fEntry1, FAUSTFLOAT(0.0f), FAUSTFLOAT(-12.0f), FAUSTFLOAT(12.0f), FAUSTFLOAT(0.001f));
		ui_interface->addNumEntry("Trigger", &fEntry3, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.01f));
		ui_interface->addButton("WakeUp", &fButton0);
		ui_interface->declare(&fHslider8, "010", "");
		ui_interface->declare(&fHslider8, "export", "Damp");
		ui_interface->declare(&fHslider8, "unit", "%");
		ui_interface->addHorizontalSlider("Damp", &fHslider8, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.0001f));
		ui_interface->declare(&fHslider4, "020", "");
		ui_interface->declare(&fHslider4, "export", "Size");
		ui_interface->declare(&fHslider4, "unit", "in");
		ui_interface->addHorizontalSlider("Cymbal_Size", &fHslider4, FAUSTFLOAT(14.0f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(24.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider6, "030", "");
		ui_interface->declare(&fHslider6, "export", "Crash");
		ui_interface->declare(&fHslider6, "unit", "%");
		ui_interface->addHorizontalSlider("Cymbal_Crash", &fHslider6, FAUSTFLOAT(5e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider7, "1010", "");
		ui_interface->declare(&fHslider7, "export", "Lateness");
		ui_interface->declare(&fHslider7, "unit", "ms");
		ui_interface->addHorizontalSlider("Lateness", &fHslider7, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider0, "1020", "");
		ui_interface->declare(&fHslider0, "export", "Gain");
		ui_interface->declare(&fHslider0, "unit", "dB");
		ui_interface->addHorizontalSlider("Voice_Gain", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+02f), FAUSTFLOAT(6.0f), FAUSTFLOAT(0.1f));
		ui_interface->declare(&fHslider1, "1030", "");
		ui_interface->declare(&fHslider1, "export", "Pan");
		ui_interface->declare(&fHslider1, "unit", "%");
		ui_interface->addHorizontalSlider("Voice_Pan", &fHslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+02f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider15, "1050", "");
		ui_interface->declare(&fHslider15, "export", "Reverb Send");
		ui_interface->declare(&fHslider15, "unit", "%");
		ui_interface->addHorizontalSlider("Voice_Reverb", &fHslider15, FAUSTFLOAT(5e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider12, "1060", "");
		ui_interface->declare(&fHslider12, "export", "Voice Punchiness");
		ui_interface->declare(&fHslider12, "unit", "%");
		ui_interface->addHorizontalSlider("Voice_Punchiness", &fHslider12, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider13, "1070", "");
		ui_interface->declare(&fHslider13, "export", "Cutoff");
		ui_interface->declare(&fHslider13, "unit", "kHz");
		ui_interface->addHorizontalSlider("Sample_Cutoff", &fHslider13, FAUSTFLOAT(2e+01f), FAUSTFLOAT(2.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider2, "1080", "");
		ui_interface->declare(&fHslider2, "export", "Sample ToneFreq");
		ui_interface->declare(&fHslider2, "unit", "kHz");
		ui_interface->addHorizontalSlider("Sample_ToneFreq", &fHslider2, FAUSTFLOAT(1.5f), FAUSTFLOAT(1.0f), FAUSTFLOAT(12.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider3, "1090", "");
		ui_interface->declare(&fHslider3, "export", "Sample ToneGain");
		ui_interface->declare(&fHslider3, "unit", "db");
		ui_interface->addHorizontalSlider("Sample_ToneGain", &fHslider3, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(6.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider14, "1160", "");
		ui_interface->declare(&fHslider14, "export", "Sample Length");
		ui_interface->declare(&fHslider14, "minlabel", "∞");
		ui_interface->declare(&fHslider14, "unit", "s");
		ui_interface->addHorizontalSlider("Sample_Length", &fHslider14, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(3.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider5, "1165", "");
		ui_interface->declare(&fHslider5, "export", "Sample Speed");
		ui_interface->declare(&fHslider5, "unit", "st");
		ui_interface->addHorizontalSlider("Sample_Speed", &fHslider5, FAUSTFLOAT(0.0f), FAUSTFLOAT(-12.0f), FAUSTFLOAT(12.0f), FAUSTFLOAT(0.001f));
		ui_interface->declare(&fHslider11, "1170", "");
		ui_interface->declare(&fHslider11, "export", "Mix Strike");
		ui_interface->declare(&fHslider11, "unit", "%");
		ui_interface->addHorizontalSlider("Mix_Strike", &fHslider11, FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider10, "1180", "");
		ui_interface->declare(&fHslider10, "export", "Strike Brightness");
		ui_interface->declare(&fHslider10, "unit", "%");
		ui_interface->addHorizontalSlider("Strike_Brightness", &fHslider10, FAUSTFLOAT(75.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		FAUSTFLOAT* output2 = outputs[2];
		float fSlow0 = std::pow(1e+01f, 0.05f * std::max<float>(-1e+02f, static_cast<float>(fHslider0) + static_cast<float>(fEntry0) + 6.0f));
		float fSlow1 = 0.01f * static_cast<float>(fHslider1);
		float fSlow2 = 0.5f * fSlow0 * std::min<float>(1.0f, 1.0f - fSlow1);
		float fSlow3 = static_cast<float>(fHslider2);
		float fSlow4 = std::tan(fConst1 * fSlow3);
		float fSlow5 = 1.0f / fSlow4;
		float fSlow6 = static_cast<float>(fHslider3);
		int iSlow7 = fSlow6 > 0.0f;
		float fSlow8 = std::sin(fConst3 * fSlow3);
		float fSlow9 = fConst2 * (fSlow3 * std::pow(1e+01f, 0.05f * std::fabs(fSlow6)) / fSlow8);
		float fSlow10 = fConst2 * (fSlow3 / fSlow8);
		float fSlow11 = ((iSlow7) ? fSlow10 : fSlow9);
		float fSlow12 = 1.0f / ((fSlow5 + fSlow11) / fSlow4 + 1.0f);
		float fSlow13 = 2.0f * (1.0f - 1.0f / UrchinCymbal_faustpower2_f(fSlow4));
		float fSlow14 = static_cast<float>(fHslider4) + -1e+01f;
		float fSlow15 = std::pow(1e+01f, 0.05f * (3.0f - 0.42857143f * fSlow14));
		float fSlow16 = 0.11220185f * fSlow15 * fSlow0;
		float fSlow17 = static_cast<float>(fEntry1) + static_cast<float>(fHslider5) + 12.0f * static_cast<float>(fEntry2);
		float fSlow18 = static_cast<float>(fButton0);
		float fSlow19 = 0.01f * static_cast<float>(fHslider6);
		float fSlow20 = 0.25f * (0.0028571428f * fSlow14 + 0.2f);
		float fSlow21 = static_cast<float>(fEntry3);
		int iSlow22 = static_cast<int>(std::min<float>(fConst11, std::max<float>(0.0f, fConst12 * static_cast<float>(fHslider7))));
		float fSlow23 = static_cast<float>(fHslider9);
		float fSlow24 = (0.9f * (1.0f - 0.01f * static_cast<float>(fHslider8)) + 0.1f) * (3.5f * UrchinCymbal_faustpower3_f(0.071428575f * fSlow14) + 0.5f) * (1.0f - 0.95f * fSlow23);
		float fSlow25 = 0.125f * fSlow24;
		float fSlow26 = 0.3548134f * (1.0f - fSlow19);
		float fSlow27 = 0.3971641f / fSlow15;
		float fSlow28 = static_cast<float>(fHslider10);
		float fSlow29 = std::log(std::pow(2.0f, 0.083333336f * (0.37049553f * fSlow28 + 26.213095f)));
		float fSlow30 = fSlow29 - fConst25;
		float fSlow31 = static_cast<float>(fHslider11);
		float fSlow32 = 0.01f * fSlow31;
		float fSlow33 = fSlow14 * (1.0f - fSlow23 + 1.0f);
		float fSlow34 = std::log(std::pow(2.0f, 0.083333336f * (41.369507f - 0.9698719f * fSlow33)));
		float fSlow35 = fSlow34 - std::log(std::pow(2.0f, 0.083333336f * (41.369507f - 2.142857f * fSlow33)));
		float fSlow36 = 0.25f * fSlow24;
		float fSlow37 = 4.4e+02f * std::pow(2.0f, 0.083333336f * (0.5812045f * fSlow14 + 50.213097f));
		float fSlow38 = 0.015848933f * (fSlow31 * std::pow(1e+01f, 0.05f * (-12.0f - 0.09f * fSlow28)) / fSlow15);
		float fSlow39 = 4.4e+02f * std::pow(2.0f, 0.083333336f * (0.3101955f * fSlow28 + -41.51318f));
		float fSlow40 = 4.4e+02f * std::pow(2.0f, 0.083333336f * (0.5186314f * fSlow28 + 2.213095f));
		float fSlow41 = 0.01f * static_cast<float>(fHslider12);
		float fSlow42 = (fSlow5 - fSlow11) / fSlow4 + 1.0f;
		float fSlow43 = ((iSlow7) ? fSlow9 : fSlow10);
		float fSlow44 = (fSlow5 + fSlow43) / fSlow4 + 1.0f;
		float fSlow45 = (fSlow5 - fSlow43) / fSlow4 + 1.0f;
		float fSlow46 = 1e+03f * static_cast<float>(fHslider13);
		float fSlow47 = static_cast<float>(fHslider14);
		float fSlow48 = static_cast<float>(fSlow47 > 0.0f);
		float fSlow49 = fConst0 * fSlow47;
		float fSlow50 = 0.5f * fSlow0 * std::min<float>(1.0f, fSlow1 + 1.0f);
		float fSlow51 = 0.005f * static_cast<float>(fHslider15) * fSlow0;
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			fVec0[0] = fSlow18;
			iVec1[0] = 1;
			float fTemp0 = static_cast<float>(fSlow18 <= fVec0[1]);
			float fTemp1 = 1.0f - fConst5 * fTemp0;
			fRec3[0] = fSlow17 * fTemp1 + fConst5 * fTemp0 * fRec3[1];
			float fTemp2 = std::pow(2.0f, 0.083333336f * fRec3[0]);
			float fTemp3 = fConst4 * fTemp2;
			float fTemp4 = UrchinCymbal_faustpower2_f(1.0f - fTemp3);
			float fTemp5 = UrchinCymbal_faustpower2_f(1.0f + fTemp3);
			float fTemp6 = 1.0f + fTemp4 / fTemp5;
			float fTemp7 = std::tan(fConst3 * fTemp2);
			float fTemp8 = UrchinCymbal_faustpower2_f(fTemp7);
			float fTemp9 = 1.0f - 1.0f / fTemp8;
			float fTemp10 = fConst6 * fTemp2;
			float fTemp11 = UrchinCymbal_faustpower2_f(1.0f - fTemp10);
			float fTemp12 = UrchinCymbal_faustpower2_f(1.0f + fTemp10);
			float fTemp13 = 1.0f + fTemp11 / fTemp12;
			iRec18[0] = faust_wrap_add(1, iRec18[1]);
			int iTemp14 = (iRec18[1] % iConst8) == 0;
			int iTemp15 = faust_wrap_mul(1103515245, faust_wrap_add(12345, iRec19[1]));
			int iTemp16 = faust_wrap_mul(1103515245, faust_wrap_add(12345, iTemp15));
			int iTemp17 = faust_wrap_mul(1103515245, faust_wrap_add(12345, iTemp16));
			iRec19[0] = faust_wrap_mul(1103515245, faust_wrap_add(12345, iTemp17));
			fRec17[0] = ((iTemp14) ? 4.656613e-10f * static_cast<float>(iRec19[0]) : fRec17[1]);
			float fTemp18 = ((fRec17[0] != fRec17[1]) ? fConst7 : -1.0f + fRec20[1]);
			fRec20[0] = fTemp18;
			fRec16[0] = ((fTemp18 > 0.0f) ? fRec16[1] + (fRec17[0] - fRec16[1]) / fTemp18 : fRec17[0]);
			fVec2[IOTA0 & 4095] = fSlow21;
			float fTemp19 = fVec2[(faust_wrap_sub(IOTA0, iSlow22)) & 4095];
			fVec3[0] = fTemp19;
			int iTemp20 = fTemp19 > fVec3[1];
			iVec4[0] = iTemp20;
			int iTemp21 = iTemp20 > iVec4[1];
			iVec5[0] = iTemp21;
			float fTemp22 = static_cast<float>(faust_wrap_sub(1, iTemp21));
			float fTemp23 = fSlow20 * fTemp22 * (1.75f + 1.7f * (UrchinCymbal_faustpower3_f(1.0f - fRec22[1]) - 1.0f));
			int iTemp24 = std::fabs(fTemp23) < 1.1920929e-07f;
			float fTemp25 = ((iTemp24) ? 0.0f : std::exp(-(fConst13 / ((iTemp24) ? 1.0f : fTemp23))));
			fRec23[0] = ((iTemp20) ? 0.99f * fTemp19 + 0.01f : fRec23[1]);
			float fTemp26 = 0.5f * fRec23[0];
			float fTemp27 = static_cast<float>(iTemp21);
			fRec22[0] = (1.0f - fTemp25) * std::min<float>(1.0f, std::max<float>(fRec22[1] + fTemp26, fRec23[0])) * fTemp27 + fRec22[1] * fTemp25;
			fRec21[0] = fConst10 * fRec22[0] + fConst9 * fRec21[1];
			float fTemp28 = fRec16[0] * fRec21[0];
			float fTemp29 = std::fabs(fTemp28);
			float fTemp30 = 1.0f - 0.7f * fRec23[0];
			float fTemp31 = -2.0f + 1.0f / std::min<float>(std::max<float>(0.5f + 0.4f * fRec23[0], 0.001f), 0.999f);
			float fTemp32 = fSlow24 / fTemp2;
			int iTemp33 = std::fabs(fTemp32) < 1.1920929e-07f;
			float fTemp34 = ((iTemp33) ? 0.0f : std::exp(-(fConst13 / ((iTemp33) ? 1.0f : fTemp32))));
			int iTemp35 = faust_wrap_sub(1, iVec1[1]);
			float fTemp36 = ((iTemp35) ? 0.0f : fConst15 + fRec25[1]);
			fRec25[0] = fTemp36 - std::floor(fTemp36);
			float fTemp37 = ((iTemp35) ? 0.0f : fConst16 + fRec26[1]);
			fRec26[0] = fTemp37 - std::floor(fTemp37);
			float fTemp38 = (1.0f + 0.006f * ftbl0UrchinCymbalSIG0[static_cast<int>(65536.0f * fRec25[0])]) * (1.0f + 0.006f * ftbl0UrchinCymbalSIG0[static_cast<int>(65536.0f * fRec26[0])]);
			float fTemp39 = fConst14 / fTemp38;
			float fTemp40 = static_cast<float>(static_cast<int>(fTemp39));
			float fTemp41 = -0.95f + fTemp40;
			int iTemp42 = static_cast<int>(fTemp41);
			float fTemp43 = std::floor(fTemp41);
			fRec15[IOTA0 & 2047] = fTemp29 * (1.0f - static_cast<float>(faust_wrap_mul(2, fTemp28 < 0.0f))) * fTemp30 / (1.0f + fTemp31 * (1.0f - fTemp29)) - 0.9999f * std::pow(fTemp34, fTemp39) * (fRec15[(faust_wrap_sub(IOTA0, faust_wrap_add(1, static_cast<int>(std::min<float>(fConst17, static_cast<float>(std::max<int>(0, iTemp42))))))) & 2047] * (1.95f - fTemp40 + fTemp43) + (-0.95f - fTemp43 + fTemp40) * fRec15[(faust_wrap_sub(IOTA0, faust_wrap_add(1, static_cast<int>(std::min<float>(fConst17, static_cast<float>(std::max<int>(0, faust_wrap_add(1, iTemp42)))))))) & 2047]);
			fRec29[0] = ((iTemp14) ? 4.656613e-10f * static_cast<float>(iTemp17) : fRec29[1]);
			float fTemp44 = ((fRec29[0] != fRec29[1]) ? fConst7 : -1.0f + fRec30[1]);
			fRec30[0] = fTemp44;
			fRec28[0] = ((fTemp44 > 0.0f) ? fRec28[1] + (fRec29[0] - fRec28[1]) / fTemp44 : fRec29[0]);
			float fTemp45 = fRec21[0] * fRec28[0];
			float fTemp46 = std::fabs(fTemp45);
			float fTemp47 = fConst18 / fTemp38;
			float fTemp48 = static_cast<float>(static_cast<int>(fTemp47));
			float fTemp49 = -0.95f + fTemp48;
			int iTemp50 = static_cast<int>(fTemp49);
			float fTemp51 = std::floor(fTemp49);
			fRec27[IOTA0 & 2047] = fTemp30 * fTemp46 * (1.0f - static_cast<float>(faust_wrap_mul(2, fTemp45 < 0.0f))) / (1.0f + fTemp31 * (1.0f - fTemp46)) - 0.9999f * std::pow(fTemp34, fTemp47) * (fRec27[(faust_wrap_sub(IOTA0, faust_wrap_add(1, static_cast<int>(std::min<float>(fConst17, static_cast<float>(std::max<int>(0, iTemp50))))))) & 2047] * (1.95f - fTemp48 + fTemp51) + (-0.95f - fTemp51 + fTemp48) * fRec27[(faust_wrap_sub(IOTA0, faust_wrap_add(1, static_cast<int>(std::min<float>(fConst17, static_cast<float>(std::max<int>(0, faust_wrap_add(1, iTemp50)))))))) & 2047]);
			fRec33[0] = ((iTemp14) ? 4.656613e-10f * static_cast<float>(iTemp16) : fRec33[1]);
			float fTemp52 = ((fRec33[0] != fRec33[1]) ? fConst7 : -1.0f + fRec34[1]);
			fRec34[0] = fTemp52;
			fRec32[0] = ((fTemp52 > 0.0f) ? fRec32[1] + (fRec33[0] - fRec32[1]) / fTemp52 : fRec33[0]);
			float fTemp53 = fRec21[0] * fRec32[0];
			float fTemp54 = std::fabs(fTemp53);
			float fTemp55 = fConst19 / fTemp38;
			float fTemp56 = static_cast<float>(static_cast<int>(fTemp55));
			float fTemp57 = -0.95f + fTemp56;
			int iTemp58 = static_cast<int>(fTemp57);
			float fTemp59 = std::floor(fTemp57);
			fRec31[IOTA0 & 2047] = fTemp30 * fTemp54 * (1.0f - static_cast<float>(faust_wrap_mul(2, fTemp53 < 0.0f))) / (1.0f + fTemp31 * (1.0f - fTemp54)) - 0.9999f * std::pow(fTemp34, fTemp55) * (fRec31[(faust_wrap_sub(IOTA0, faust_wrap_add(1, static_cast<int>(std::min<float>(fConst17, static_cast<float>(std::max<int>(0, iTemp58))))))) & 2047] * (1.95f - fTemp56 + fTemp59) + (-0.95f - fTemp59 + fTemp56) * fRec31[(faust_wrap_sub(IOTA0, faust_wrap_add(1, static_cast<int>(std::min<float>(fConst17, static_cast<float>(std::max<int>(0, faust_wrap_add(1, iTemp58)))))))) & 2047]);
			fRec37[0] = ((iTemp14) ? 4.656613e-10f * static_cast<float>(iTemp15) : fRec37[1]);
			float fTemp60 = ((fRec37[0] != fRec37[1]) ? fConst7 : -1.0f + fRec38[1]);
			fRec38[0] = fTemp60;
			fRec36[0] = ((fTemp60 > 0.0f) ? fRec36[1] + (fRec37[0] - fRec36[1]) / fTemp60 : fRec37[0]);
			float fTemp61 = fRec21[0] * fRec36[0];
			float fTemp62 = std::fabs(fTemp61);
			float fTemp63 = fConst20 / fTemp38;
			float fTemp64 = static_cast<float>(static_cast<int>(fTemp63));
			float fTemp65 = -0.95f + fTemp64;
			int iTemp66 = static_cast<int>(fTemp65);
			float fTemp67 = std::floor(fTemp65);
			fRec35[IOTA0 & 4095] = fTemp30 * fTemp62 * (1.0f - static_cast<float>(faust_wrap_mul(2, fTemp61 < 0.0f))) / (1.0f + fTemp31 * (1.0f - fTemp62)) - 0.9999f * std::pow(fTemp34, fTemp63) * (fRec35[(faust_wrap_sub(IOTA0, faust_wrap_add(1, static_cast<int>(std::min<float>(fConst17, static_cast<float>(std::max<int>(0, iTemp66))))))) & 4095] * (1.95f - fTemp64 + fTemp67) + (-0.95f - fTemp67 + fTemp64) * fRec35[(faust_wrap_sub(IOTA0, faust_wrap_add(1, static_cast<int>(std::min<float>(fConst17, static_cast<float>(std::max<int>(0, faust_wrap_add(1, iTemp66)))))))) & 4095]);
			float fTemp68 = 0.1998f * (fRec15[(faust_wrap_sub(IOTA0, 1)) & 2047] + fRec27[(faust_wrap_sub(IOTA0, 1)) & 2047]) + 0.15984f * fRec31[(faust_wrap_sub(IOTA0, 1)) & 2047] + 0.11988f * fRec35[(faust_wrap_sub(IOTA0, 1)) & 4095];
			float fTemp69 = fSlow25 * (fTemp22 * (1.75f + 1.7f * (UrchinCymbal_faustpower3_f(1.0f - fRec40[1]) - 1.0f)) / fTemp2);
			int iTemp70 = std::fabs(fTemp69) < 1.1920929e-07f;
			float fTemp71 = ((iTemp70) ? 0.0f : std::exp(-(fConst13 / ((iTemp70) ? 1.0f : fTemp69))));
			fRec40[0] = fTemp27 * (1.0f - fTemp71) * std::min<float>(1.0f, std::max<float>(fTemp26 + fRec40[1], fRec23[0])) + fRec40[1] * fTemp71;
			fRec39[0] = fConst10 * fRec40[0] + fConst9 * fRec39[1];
			float fTemp72 = fTemp2 * std::pow(2.0f, 0.083333336f * (63.26265f - 64.91269f * fRec39[0]));
			float fTemp73 = std::min<float>(fConst22, std::max<float>(2e+01f, 2.2e+02f * fTemp72));
			float fTemp74 = std::tan(fConst21 * fTemp73);
			float fTemp75 = 1.0f / fTemp74;
			float fTemp76 = std::min<float>(fConst22, std::max<float>(2e+01f, 6.6e+02f * fTemp72)) / std::sin(fConst23 * fTemp73);
			float fTemp77 = fConst21 * fTemp76;
			float fTemp78 = 2.0f * fRec14[1] * (1.0f - 1.0f / UrchinCymbal_faustpower2_f(fTemp74));
			float fTemp79 = 1.0f + (fTemp75 + fTemp77) / fTemp74;
			fRec14[0] = 0.3548134f * fTemp68 - (fRec14[2] * (1.0f + (fTemp75 - fTemp77) / fTemp74) + fTemp78) / fTemp79;
			float fTemp80 = fConst24 * fTemp76;
			float fTemp81 = 1.0f / std::tan(fConst21 * std::min<float>(fConst22, 4.4e+02f * fTemp2 * std::pow(2.0f, 1.442695f * (fSlow29 - fSlow30 * fRec21[0]))));
			iRec45[0] = faust_wrap_add(12345, faust_wrap_mul(1103515245, iRec45[1]));
			fRec44[0] = ((iTemp14) ? 4.656613e-10f * static_cast<float>(iRec45[0]) : fRec44[1]);
			float fTemp82 = ((fRec44[0] != fRec44[1]) ? fConst7 : -1.0f + fRec46[1]);
			fRec46[0] = fTemp82;
			fRec43[0] = ((fTemp82 > 0.0f) ? fRec43[1] + (fRec44[0] - fRec43[1]) / fTemp82 : fRec44[0]);
			float fTemp83 = 1.0f / fTemp7;
			float fTemp84 = 1.0f + (1.4142135f + fTemp83) / fTemp7;
			fRec42[0] = fSlow32 * fRec21[0] * fRec43[0] - (fRec42[2] * (1.0f + (-1.4142135f + fTemp83) / fTemp7) + 2.0f * fRec42[1] * fTemp9) / fTemp84;
			float fTemp85 = (fRec42[2] + (fRec42[0] - 2.0f * fRec42[1])) / (fTemp8 * fTemp84);
			fVec7[0] = fTemp85;
			fRec41[0] = -((fRec41[1] * (1.0f - fTemp81) - (fTemp85 + fVec7[1])) / (1.0f + fTemp81));
			float fTemp86 = fSlow36 * (fTemp22 * (1.75f + 1.7f * (UrchinCymbal_faustpower3_f(1.0f - fRec48[1]) - 1.0f)) / fTemp2);
			int iTemp87 = std::fabs(fTemp86) < 1.1920929e-07f;
			float fTemp88 = ((iTemp87) ? 0.0f : std::exp(-(fConst13 / ((iTemp87) ? 1.0f : fTemp86))));
			fRec48[0] = fTemp27 * (1.0f - fTemp88) * std::min<float>(1.0f, std::max<float>(fTemp26 + fRec48[1], fRec23[0])) + fRec48[1] * fTemp88;
			fRec47[0] = fConst10 * fRec48[0] + fConst9 * fRec47[1];
			float fTemp89 = std::tan(fConst21 * std::min<float>(fConst22, std::max<float>(2e+01f, 4.4e+02f * fTemp2 * std::pow(2.0f, 1.442695f * (fSlow34 - fSlow35 * fRec47[0])))));
			float fTemp90 = 1.0f / fTemp89;
			float fTemp91 = UrchinCymbal_faustpower2_f(fTemp89);
			float fTemp92 = 1.0f + (1.4142135f + fTemp90) / fTemp89;
			fRec13[0] = fSlow19 * ((fTemp78 + fRec14[0] * (1.0f + (fTemp75 + fTemp80) / fTemp74) + fRec14[2] * (1.0f + (fTemp75 - fTemp80) / fTemp74)) / fTemp79) + fSlow26 * fTemp68 + fSlow27 * fRec41[0] - (fRec13[2] * (1.0f + (-1.4142135f + fTemp90) / fTemp89) + 2.0f * fRec13[1] * (1.0f - 1.0f / fTemp91)) / fTemp92;
			float fTemp93 = std::tan(fConst26 * fTemp2);
			float fTemp94 = 1.0f / fTemp93;
			float fTemp95 = UrchinCymbal_faustpower2_f(fTemp93);
			float fTemp96 = 1.0f - 1.0f / fTemp95;
			float fTemp97 = 1.0f + (1.847759f + fTemp94) / fTemp93;
			fRec12[0] = (fRec13[2] + (fRec13[0] - 2.0f * fRec13[1])) / (fTemp91 * fTemp92) - (fRec12[2] * (1.0f + (-1.847759f + fTemp94) / fTemp93) + 2.0f * fRec12[1] * fTemp96) / fTemp97;
			float fTemp98 = 1.0f + (0.76536685f + fTemp94) / fTemp93;
			fRec11[0] = (fRec12[2] + (fRec12[0] - 2.0f * fRec12[1])) / (fTemp95 * fTemp97) - (fRec11[2] * (1.0f + (-0.76536685f + fTemp94) / fTemp93) + 2.0f * fTemp96 * fRec11[1]) / fTemp98;
			float fTemp99 = std::tan(fConst2 * fTemp2);
			float fTemp100 = 1.0f / fTemp99;
			float fTemp101 = UrchinCymbal_faustpower2_f(fTemp99);
			float fTemp102 = 1.0f + (1.4142135f + fTemp100) / fTemp99;
			fRec10[0] = (fRec11[2] + (fRec11[0] - 2.0f * fRec11[1])) / (fTemp95 * fTemp98) - (fRec10[2] * (1.0f + (-1.4142135f + fTemp100) / fTemp99) + 2.0f * fRec10[1] * (1.0f - 1.0f / fTemp101)) / fTemp102;
			float fTemp103 = std::tan(fConst21 * std::min<float>(fConst22, fSlow37 * fTemp2));
			float fTemp104 = 1.0f / fTemp103;
			float fTemp105 = 1.0f + (1.4142135f + fTemp104) / fTemp103;
			fRec9[0] = (fRec10[2] + (fRec10[0] - 2.0f * fRec10[1])) / (fTemp101 * fTemp102) - (fRec9[2] * (1.0f + (-1.4142135f + fTemp104) / fTemp103) + 2.0f * fRec9[1] * (1.0f - 1.0f / UrchinCymbal_faustpower2_f(fTemp103))) / fTemp105;
			float fTemp106 = std::cos(fConst27 * fTemp2);
			fRec8[0] = (fRec9[2] + fRec9[0] + 2.0f * fRec9[1]) / fTemp105 + fRec8[1] * fTemp13 * fTemp106 - fTemp11 * fRec8[2] / fTemp12;
			float fTemp107 = fRec7[1] * std::cos(fConst28 * fTemp2);
			fRec7[0] = fTemp13 * (0.5f * fRec8[0] - fRec8[1] * fTemp106 + 0.5f * fRec8[2] + fTemp107) - fTemp11 * fRec7[2] / fTemp12;
			float fTemp108 = std::tan(fConst29 * fTemp2);
			float fTemp109 = 1.0f / fTemp108;
			float fTemp110 = fTemp2 / std::sin(fConst31 * fTemp2);
			float fTemp111 = fConst30 * fTemp110;
			float fTemp112 = 2.0f * fRec6[1] * (1.0f - 1.0f / UrchinCymbal_faustpower2_f(fTemp108));
			float fTemp113 = 1.0f + (fTemp109 + fTemp111) / fTemp108;
			fRec6[0] = fTemp13 * (0.5f * fRec7[0] - fTemp107 + 0.5f * fRec7[2]) - (fRec6[2] * (1.0f + (fTemp109 - fTemp111) / fTemp108) + fTemp112) / fTemp113;
			float fTemp114 = fConst2 * fTemp110;
			float fTemp115 = fTemp2 / std::sin(fConst32 * fTemp2);
			float fTemp116 = fConst30 * fTemp115;
			float fTemp117 = 2.0f * fTemp9 * fRec5[1];
			float fTemp118 = 1.0f + (fTemp83 + fTemp116) / fTemp7;
			fRec5[0] = (fTemp112 + fRec6[0] * (1.0f + (fTemp109 + fTemp114) / fTemp108) + fRec6[2] * (1.0f + (fTemp109 - fTemp114) / fTemp108)) / fTemp113 - (fRec5[2] * (1.0f + (fTemp83 - fTemp116) / fTemp7) + fTemp117) / fTemp118;
			float fTemp119 = fConst2 * fTemp115;
			float fTemp120 = std::cos(fConst33 * fTemp2);
			fRec4[0] = (fTemp117 + fRec5[0] * (1.0f + (fTemp83 + fTemp119) / fTemp7) + fRec5[2] * (1.0f + (fTemp83 - fTemp119) / fTemp7)) / fTemp118 + fRec4[1] * fTemp6 * fTemp120 - fTemp4 * fRec4[2] / fTemp5;
			float fTemp121 = std::tan(fConst21 * std::max<float>(4e+01f, fSlow39 * fTemp2));
			float fTemp122 = 1.0f / fTemp121;
			float fTemp123 = UrchinCymbal_faustpower2_f(fTemp121);
			float fTemp124 = 1.0f + (1.4142135f + fTemp122) / fTemp121;
			fRec50[0] = fRec43[0] - (fRec50[2] * (1.0f + (-1.4142135f + fTemp122) / fTemp121) + 2.0f * fRec50[1] * (1.0f - 1.0f / fTemp123)) / fTemp124;
			float fTemp125 = std::tan(fConst21 * std::min<float>(fConst22, fSlow40 * fTemp2));
			float fTemp126 = 1.0f / fTemp125;
			float fTemp127 = 1.0f + (1.4142135f + fTemp126) / fTemp125;
			fRec49[0] = (fRec50[2] + (fRec50[0] - 2.0f * fRec50[1])) / (fTemp123 * fTemp124) - (fRec49[2] * (1.0f + (-1.4142135f + fTemp126) / fTemp125) + 2.0f * fRec49[1] * (1.0f - 1.0f / UrchinCymbal_faustpower2_f(fTemp125))) / fTemp127;
			iRec51[0] = faust_wrap_add(iTemp21, faust_wrap_mul(faust_wrap_add(iRec51[1], iRec51[1] > 0), iTemp20 <= iVec4[1]));
			float fTemp128 = static_cast<float>(iRec51[0]);
			float fTemp129 = std::max<float>(1.0f, fConst34 / fTemp2);
			fRec52[0] = fSlow41 * fTemp1 + fConst5 * fTemp0 * fRec52[1];
			fRec53[0] = ((iTemp21 > 0) ? fConst36 : std::max<float>(0.0f, -1.0f + fRec53[1]));
			iRec54[0] = (iTemp21 > iVec5[1]) + faust_wrap_mul(faust_wrap_add(iRec54[1], iRec54[1] > 0), iTemp21 <= iVec5[1]);
			float fTemp130 = static_cast<float>(iRec54[0]);
			float fTemp131 = std::max<float>(0.0f, std::min<float>(fConst38 * fTemp130, 1.0f + (fConst37 - fTemp130) / std::max<float>(1.0f, fConst0 * ((0.5f - 0.35f * fRec52[0]) / fTemp2))));
			fVec8[0] = fTemp131;
			fRec55[0] = ((iTemp21) ? fVec8[1] : fRec55[1]);
			float fTemp132 = fSlow13 * fRec2[1];
			fRec2[0] = fSlow16 * (fTemp6 * (0.5f * fRec4[0] - fRec4[1] * fTemp120 + 0.5f * fRec4[2]) + fSlow38 * (fRec23[0] * (fRec49[2] + fRec49[0] + 2.0f * fRec49[1]) * std::max<float>(0.0f, std::min<float>(fTemp128 / fTemp129, 1.0f + (fTemp129 - fTemp128) / std::max<float>(1.0f, fConst35 / fTemp2))) / fTemp127)) * (1.0f - fRec52[0] + fRec52[0] * ((fRec53[0] > 0.0f) ? fRec55[0] + fTemp131 * (1.0f - fRec55[0]) : fTemp131)) - fSlow12 * (fSlow42 * fRec2[2] + fTemp132);
			float fTemp133 = std::tan(fConst21 * std::min<float>(fConst22, fSlow46 * fTemp2));
			float fTemp134 = 1.0f / fTemp133;
			float fTemp135 = 1.0f - 1.0f / UrchinCymbal_faustpower2_f(fTemp133);
			float fTemp136 = 1.0f + (1.847759f + fTemp134) / fTemp133;
			fRec1[0] = fSlow12 * (fTemp132 + fSlow44 * fRec2[0] + fSlow45 * fRec2[2]) - (fRec1[2] * (1.0f + (-1.847759f + fTemp134) / fTemp133) + 2.0f * fRec1[1] * fTemp135) / fTemp136;
			float fTemp137 = 1.0f + (0.76536685f + fTemp134) / fTemp133;
			fRec0[0] = (fRec1[2] + fRec1[0] + 2.0f * fRec1[1]) / fTemp136 - (fRec0[2] * (1.0f + (-0.76536685f + fTemp134) / fTemp133) + 2.0f * fTemp135 * fRec0[1]) / fTemp137;
			float fTemp138 = (fRec0[2] + fRec0[0] + 2.0f * fRec0[1]) / fTemp137;
			fRec56[0] = fSlow48 * fTemp1 + fConst5 * fTemp0 * fRec56[1];
			fRec58[0] = ((iTemp20 > 0) ? fSlow49 / fTemp2 : std::max<float>(0.0f, -1.0f + fRec58[1]));
			int iTemp139 = fRec58[0] > 0.0f;
			iVec9[0] = iTemp139;
			iRec57[0] = faust_wrap_add(iTemp139, faust_wrap_mul(iRec57[1], iVec9[1] >= iTemp139));
			iRec59[0] = faust_wrap_mul(faust_wrap_add(1, iRec59[1]), iTemp139 == 0);
			float fTemp140 = (fTemp138 + tanhf(fTemp138)) * (1.0f + fRec56[0] * (-1.0f + std::max<float>(0.0f, std::min<float>(static_cast<float>(iRec57[0]), 1.0f) * (1.0f - fConst39 * static_cast<float>(iRec59[0])))));
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow2 * fTemp140);
			output1[i0] = static_cast<FAUSTFLOAT>(fSlow50 * fTemp140);
			output2[i0] = static_cast<FAUSTFLOAT>(fSlow51 * fTemp140);
			fVec0[1] = fVec0[0];
			iVec1[1] = iVec1[0];
			fRec3[1] = fRec3[0];
			iRec18[1] = iRec18[0];
			iRec19[1] = iRec19[0];
			fRec17[1] = fRec17[0];
			fRec20[1] = fRec20[0];
			fRec16[1] = fRec16[0];
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fVec3[1] = fVec3[0];
			iVec4[1] = iVec4[0];
			iVec5[1] = iVec5[0];
			fRec23[1] = fRec23[0];
			fRec22[1] = fRec22[0];
			fRec21[1] = fRec21[0];
			fRec25[1] = fRec25[0];
			fRec26[1] = fRec26[0];
			fRec29[1] = fRec29[0];
			fRec30[1] = fRec30[0];
			fRec28[1] = fRec28[0];
			fRec33[1] = fRec33[0];
			fRec34[1] = fRec34[0];
			fRec32[1] = fRec32[0];
			fRec37[1] = fRec37[0];
			fRec38[1] = fRec38[0];
			fRec36[1] = fRec36[0];
			fRec40[1] = fRec40[0];
			fRec39[1] = fRec39[0];
			fRec14[2] = fRec14[1];
			fRec14[1] = fRec14[0];
			iRec45[1] = iRec45[0];
			fRec44[1] = fRec44[0];
			fRec46[1] = fRec46[0];
			fRec43[1] = fRec43[0];
			fRec42[2] = fRec42[1];
			fRec42[1] = fRec42[0];
			fVec7[1] = fVec7[0];
			fRec41[1] = fRec41[0];
			fRec48[1] = fRec48[0];
			fRec47[1] = fRec47[0];
			fRec13[2] = fRec13[1];
			fRec13[1] = fRec13[0];
			fRec12[2] = fRec12[1];
			fRec12[1] = fRec12[0];
			fRec11[2] = fRec11[1];
			fRec11[1] = fRec11[0];
			fRec10[2] = fRec10[1];
			fRec10[1] = fRec10[0];
			fRec9[2] = fRec9[1];
			fRec9[1] = fRec9[0];
			fRec8[2] = fRec8[1];
			fRec8[1] = fRec8[0];
			fRec7[2] = fRec7[1];
			fRec7[1] = fRec7[0];
			fRec6[2] = fRec6[1];
			fRec6[1] = fRec6[0];
			fRec5[2] = fRec5[1];
			fRec5[1] = fRec5[0];
			fRec4[2] = fRec4[1];
			fRec4[1] = fRec4[0];
			fRec50[2] = fRec50[1];
			fRec50[1] = fRec50[0];
			fRec49[2] = fRec49[1];
			fRec49[1] = fRec49[0];
			iRec51[1] = iRec51[0];
			fRec52[1] = fRec52[0];
			fRec53[1] = fRec53[0];
			iRec54[1] = iRec54[0];
			fVec8[1] = fVec8[0];
			fRec55[1] = fRec55[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			fRec56[1] = fRec56[0];
			fRec58[1] = fRec58[0];
			iVec9[1] = iVec9[0];
			iRec57[1] = iRec57[0];
			iRec59[1] = iRec59[0];
		}
	}

};

#endif
