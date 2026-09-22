/* ------------------------------------------------------------
copyright: "Copyright (c) 2023 Punk Labs LLC"
license: "GPLv3 (or later)"
name: "OneTrick URCHIN DSP"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 0 -cn UrchinDrum -dtl 65536 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __UrchinDrum_H__
#define  __UrchinDrum_H__

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
#define FAUSTCLASS UrchinDrum
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

class UrchinDrumSIG0 {
	
  private:
	
	int iVec6[2];
	int iRec24[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsUrchinDrumSIG0() {
		return 0;
	}
	int getNumOutputsUrchinDrumSIG0() {
		return 1;
	}
	
	void instanceInitUrchinDrumSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			iVec6[l23] = 0;
		}
		for (int l24 = 0; l24 < 2; l24 = faust_wrap_add(l24, 1)) {
			iRec24[l24] = 0;
		}
	}
	
	void fillUrchinDrumSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec6[0] = 1;
			iRec24[0] = (faust_wrap_add(iVec6[1], iRec24[1])) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec24[0]));
			iVec6[1] = iVec6[0];
			iRec24[1] = iRec24[0];
		}
	}

};

static UrchinDrumSIG0* newUrchinDrumSIG0() { return (UrchinDrumSIG0*)new UrchinDrumSIG0(); }
static void deleteUrchinDrumSIG0(UrchinDrumSIG0* dsp) { delete dsp; }

static float UrchinDrum_faustpower2_f(float value) {
	return value * value;
}
static float UrchinDrum_faustpower3_f(float value) {
	return value * value * value;
}
static float ftbl0UrchinDrumSIG0[65536];

class UrchinDrum : public dsp {
	
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
	float fConst4;
	float fConst5;
	float fConst6;
	int iVec0[2];
	float fConst7;
	float fConst8;
	float fRec5[2];
	float fRec6[2];
	float fConst9;
	FAUSTFLOAT fHslider4;
	FAUSTFLOAT fEntry1;
	FAUSTFLOAT fHslider5;
	FAUSTFLOAT fEntry2;
	float fConst10;
	FAUSTFLOAT fButton0;
	float fVec1[2];
	float fRec9[2];
	FAUSTFLOAT fHslider6;
	float fRec10[2];
	FAUSTFLOAT fHslider7;
	FAUSTFLOAT fHslider8;
	int iRec15[2];
	float fConst11;
	int iConst12;
	int iRec16[2];
	float fRec14[2];
	float fRec17[2];
	float fRec13[2];
	float fConst13;
	float fRec12[3];
	float fConst14;
	float fRec11[3];
	FAUSTFLOAT fEntry3;
	int IOTA0;
	float fVec2[4096];
	float fConst15;
	float fConst16;
	FAUSTFLOAT fHslider9;
	float fVec3[2];
	int iVec4[2];
	int iVec5[2];
	int iRec18[2];
	float fConst17;
	float fConst18;
	float fRec19[2];
	float fConst19;
	float fConst20;
	float fConst21;
	float fRec21[2];
	float fRec20[2];
	float fRec23[3];
	float fRec22[3];
	FAUSTFLOAT fHslider10;
	FAUSTFLOAT fHslider11;
	FAUSTFLOAT fHslider12;
	FAUSTFLOAT fHslider13;
	float fRec27[2];
	float fRec26[2];
	float fRec25[2];
	float fRec29[2];
	float fRec28[2];
	float fRec30[2];
	float fRec32[2];
	float fRec31[2];
	float fRec33[2];
	float fRec35[2];
	float fRec34[2];
	float fRec36[2];
	float fRec38[2];
	float fRec37[2];
	float fRec39[2];
	float fRec41[2];
	float fRec40[2];
	FAUSTFLOAT fHslider14;
	FAUSTFLOAT fHslider15;
	FAUSTFLOAT fHslider16;
	float fRec44[2];
	float fRec43[2];
	float fRec42[2];
	float fRec46[2];
	float fRec45[2];
	float fRec47[2];
	float fRec49[2];
	float fRec48[2];
	float fRec50[2];
	float fRec52[2];
	float fRec51[2];
	float fRec53[2];
	float fRec55[2];
	float fRec54[2];
	float fRec56[2];
	float fRec58[2];
	float fRec57[2];
	FAUSTFLOAT fHslider17;
	float fConst22;
	float fConst23;
	float fRec60[2];
	float fRec59[2];
	float fRec62[2];
	float fRec61[2];
	float fVec7[2];
	float fVec8[1024];
	float fConst24;
	float fRec7[2];
	float fVec9[1024];
	float fRec8[2];
	float fConst25;
	float fRec4[2];
	float fRec3[2];
	FAUSTFLOAT fHslider18;
	float fRec63[2];
	float fConst26;
	float fRec64[2];
	float fConst27;
	float fConst28;
	int iRec65[2];
	float fVec10[2];
	float fRec66[2];
	float fRec2[3];
	FAUSTFLOAT fHslider19;
	float fRec1[3];
	float fRec0[3];
	FAUSTFLOAT fHslider20;
	float fRec67[2];
	float fRec69[2];
	int iVec11[2];
	int iRec68[2];
	float fConst29;
	int iRec70[2];
	FAUSTFLOAT fHslider21;
	
 public:
	UrchinDrum() {
	}
	
	UrchinDrum(const UrchinDrum&) = default;
	
	virtual ~UrchinDrum() = default;
	
	UrchinDrum& operator=(const UrchinDrum&) = default;
	
	void metadata(Meta* m) { 
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/downSample:author", "Romain Michon");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/sAndH:author", "Romain Michon");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 0 -cn UrchinDrum -dtl 65536 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("delays.lib/fdelay4:author", "Julius O. Smith III");
		m->declare("delays.lib/fdelayltv:author", "Julius O. Smith III");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("drum.lib/copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("drum.lib/license", "GPLv3 (or later)");
		m->declare("drum.lib/name", "OneTrick URCHIN DSP");
		m->declare("envelopes.lib/adsr:author", "Yann Orlarey and Andrey Bundin");
		m->declare("envelopes.lib/ar:author", "Yann Orlarey, Stéphane Letz");
		m->declare("envelopes.lib/are:author", "Julius O. Smith III, revised by Stephane Letz");
		m->declare("envelopes.lib/are:licence", "LicenseRef-STK-4.3");
		m->declare("envelopes.lib/asrfe:author", "Julius O. Smith III, revised by Stephane Letz");
		m->declare("envelopes.lib/asrfe:licence", "LicenseRef-STK-4.3");
		m->declare("envelopes.lib/author", "GRAME");
		m->declare("envelopes.lib/copyright", "GRAME");
		m->declare("envelopes.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("envelopes.lib/name", "Faust Envelope Library");
		m->declare("envelopes.lib/version", "1.3.0");
		m->declare("filename", "drum.dsp");
		m->declare("filters.lib/bandpass0_bandstop1:author", "Julius O. Smith III");
		m->declare("filters.lib/bandpass0_bandstop1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/bandpass0_bandstop1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/bandpass:author", "Julius O. Smith III");
		m->declare("filters.lib/bandpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/bandpass:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/dcblockerat:author", "Julius O. Smith III");
		m->declare("filters.lib/dcblockerat:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/dcblockerat:license", "LicenseRef-STK-4.3");
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
		m->declare("filters.lib/peak_eq:author", "Julius O. Smith III");
		m->declare("filters.lib/peak_eq:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/peak_eq:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/pole:author", "Julius O. Smith III");
		m->declare("filters.lib/pole:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/pole:license", "LicenseRef-STK-4.3");
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
		m->declare("filters.lib/tf2sb:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2sb:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2sb:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/version", "1.9.0");
		m->declare("filters.lib/zero:author", "Julius O. Smith III");
		m->declare("filters.lib/zero:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/zero:license", "LicenseRef-STK-4.3");
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
		m->declare("misceffects.lib/gate_gain_mono:author", "Julius O. Smith III");
		m->declare("misceffects.lib/gate_gain_mono:license", "LicenseRef-STK-4.3");
		m->declare("misceffects.lib/gate_mono:author", "Julius O. Smith III");
		m->declare("misceffects.lib/gate_mono:license", "LicenseRef-STK-4.3");
		m->declare("misceffects.lib/name", "Misc Effects Library");
		m->declare("misceffects.lib/version", "2.6.0");
		m->declare("name", "OneTrick URCHIN DSP");
		m->declare("noises.lib/name", "Faust Noise Generator Library");
		m->declare("noises.lib/version", "1.6.0");
		m->declare("onetrick.lib/copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("onetrick.lib/license", "GPLv3 (or later)");
		m->declare("onetrick.lib/name", "OneTrick DSP Library");
		m->declare("oscillators.lib/lf_sawpos:author", "Bart Brouns, revised by Stéphane Letz");
		m->declare("oscillators.lib/lf_sawpos:licence", "LicenseRef-STK-4.3");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.8.0");
		m->declare("physmodels.lib/name", "Faust Physical Models Library");
		m->declare("physmodels.lib/version", "1.2.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("routes.lib/name", "Faust Signal Routing Library");
		m->declare("routes.lib/version", "1.4.0");
		m->declare("shared.lib/copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("shared.lib/license", "GPLv3 (or later)");
		m->declare("shared.lib/name", "OneTrick URCHIN DSP");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/onePoleSwitching:author", "Jonatan Liljedahl, revised by Dario Sanfilippo");
		m->declare("signals.lib/onePoleSwitching:licence", "LicenseRef-STK-4.3");
		m->declare("signals.lib/version", "1.7.0");
	}

	virtual int getNumInputs() {
		return 0;
	}
	virtual int getNumOutputs() {
		return 3;
	}
	
	static void classInit(int sample_rate) {
		UrchinDrumSIG0* sig0 = newUrchinDrumSIG0();
		sig0->instanceInitUrchinDrumSIG0(sample_rate);
		sig0->fillUrchinDrumSIG0(65536, ftbl0UrchinDrumSIG0);
		deleteUrchinDrumSIG0(sig0);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 3141.5928f / fConst0;
		fConst2 = 1570.7964f / fConst0;
		fConst3 = 6283.1855f / fConst0;
		fConst4 = 1.0f / std::tan(125.663704f / fConst0);
		fConst5 = 1.0f / (fConst4 + 1.0f);
		fConst6 = 1.0f - fConst4;
		fConst7 = 62.831852f / fConst0;
		fConst8 = 1.0f / (fConst7 + 1.0f);
		fConst9 = 1.867647e-05f * fConst0;
		fConst10 = std::exp(-(1e+02f / fConst0));
		fConst11 = fConst0 / std::min<float>(std::min<float>(4.8e+04f, fConst0), fConst0);
		iConst12 = static_cast<int>(fConst11);
		fConst13 = 3.1415927f / fConst0;
		fConst14 = 0.475f * fConst0;
		fConst15 = 0.02f * fConst0;
		fConst16 = 0.001f * fConst0;
		fConst17 = 0.003f * fConst0;
		fConst18 = 0.005f * fConst0;
		fConst19 = std::exp(-(1e+03f / fConst0));
		fConst20 = 1.0f - fConst19;
		fConst21 = 1.0f / fConst0;
		fConst22 = 4.4e+02f / fConst0;
		fConst23 = 117.123f / fConst0;
		fConst24 = 0.0029411765f * fConst0;
		fConst25 = 1.0f - fConst7;
		fConst26 = 0.00015f * fConst0;
		fConst27 = std::max<float>(1.0f, fConst26);
		fConst28 = 1.0f / fConst27;
		fConst29 = 1.0f / std::max<float>(1.0f, fConst16);
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.0f);
		fEntry0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(1.5f);
		fHslider3 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider4 = static_cast<FAUSTFLOAT>(12.0f);
		fEntry1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider5 = static_cast<FAUSTFLOAT>(0.0f);
		fEntry2 = static_cast<FAUSTFLOAT>(0.0f);
		fButton0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider6 = static_cast<FAUSTFLOAT>(3e+01f);
		fHslider7 = static_cast<FAUSTFLOAT>(1e+02f);
		fHslider8 = static_cast<FAUSTFLOAT>(75.0f);
		fEntry3 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider9 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider10 = static_cast<FAUSTFLOAT>(1.2e+02f);
		fHslider11 = static_cast<FAUSTFLOAT>(8.0f);
		fHslider12 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider13 = static_cast<FAUSTFLOAT>(5e+01f);
		fHslider14 = static_cast<FAUSTFLOAT>(1.0f);
		fHslider15 = static_cast<FAUSTFLOAT>(1e+02f);
		fHslider16 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider17 = static_cast<FAUSTFLOAT>(35.0f);
		fHslider18 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider19 = static_cast<FAUSTFLOAT>(2e+01f);
		fHslider20 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider21 = static_cast<FAUSTFLOAT>(5e+01f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fRec5[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec6[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fVec1[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			fRec9[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec10[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = faust_wrap_add(l6, 1)) {
			iRec15[l6] = 0;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			iRec16[l7] = 0;
		}
		for (int l8 = 0; l8 < 2; l8 = faust_wrap_add(l8, 1)) {
			fRec14[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec17[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = faust_wrap_add(l10, 1)) {
			fRec13[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 3; l11 = faust_wrap_add(l11, 1)) {
			fRec12[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec11[l12] = 0.0f;
		}
		IOTA0 = 0;
		for (int l13 = 0; l13 < 4096; l13 = faust_wrap_add(l13, 1)) {
			fVec2[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			fVec3[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			iVec4[l15] = 0;
		}
		for (int l16 = 0; l16 < 2; l16 = faust_wrap_add(l16, 1)) {
			iVec5[l16] = 0;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			iRec18[l17] = 0;
		}
		for (int l18 = 0; l18 < 2; l18 = faust_wrap_add(l18, 1)) {
			fRec19[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			fRec21[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fRec20[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 3; l21 = faust_wrap_add(l21, 1)) {
			fRec23[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 3; l22 = faust_wrap_add(l22, 1)) {
			fRec22[l22] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec27[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2; l26 = faust_wrap_add(l26, 1)) {
			fRec26[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2; l27 = faust_wrap_add(l27, 1)) {
			fRec25[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2; l28 = faust_wrap_add(l28, 1)) {
			fRec29[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			fRec28[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2; l30 = faust_wrap_add(l30, 1)) {
			fRec30[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			fRec32[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2; l32 = faust_wrap_add(l32, 1)) {
			fRec31[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			fRec33[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 2; l34 = faust_wrap_add(l34, 1)) {
			fRec35[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = faust_wrap_add(l35, 1)) {
			fRec34[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = faust_wrap_add(l36, 1)) {
			fRec36[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec38[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = faust_wrap_add(l38, 1)) {
			fRec37[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			fRec39[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = faust_wrap_add(l40, 1)) {
			fRec41[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = faust_wrap_add(l41, 1)) {
			fRec40[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 2; l42 = faust_wrap_add(l42, 1)) {
			fRec44[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec43[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 2; l44 = faust_wrap_add(l44, 1)) {
			fRec42[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 2; l45 = faust_wrap_add(l45, 1)) {
			fRec46[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 2; l46 = faust_wrap_add(l46, 1)) {
			fRec45[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 2; l47 = faust_wrap_add(l47, 1)) {
			fRec47[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 2; l48 = faust_wrap_add(l48, 1)) {
			fRec49[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 2; l49 = faust_wrap_add(l49, 1)) {
			fRec48[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 2; l50 = faust_wrap_add(l50, 1)) {
			fRec50[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 2; l51 = faust_wrap_add(l51, 1)) {
			fRec52[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 2; l52 = faust_wrap_add(l52, 1)) {
			fRec51[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 2; l53 = faust_wrap_add(l53, 1)) {
			fRec53[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 2; l54 = faust_wrap_add(l54, 1)) {
			fRec55[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 2; l55 = faust_wrap_add(l55, 1)) {
			fRec54[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 2; l56 = faust_wrap_add(l56, 1)) {
			fRec56[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 2; l57 = faust_wrap_add(l57, 1)) {
			fRec58[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 2; l58 = faust_wrap_add(l58, 1)) {
			fRec57[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = faust_wrap_add(l59, 1)) {
			fRec60[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 2; l60 = faust_wrap_add(l60, 1)) {
			fRec59[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = faust_wrap_add(l61, 1)) {
			fRec62[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 2; l62 = faust_wrap_add(l62, 1)) {
			fRec61[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 2; l63 = faust_wrap_add(l63, 1)) {
			fVec7[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 1024; l64 = faust_wrap_add(l64, 1)) {
			fVec8[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 2; l65 = faust_wrap_add(l65, 1)) {
			fRec7[l65] = 0.0f;
		}
		for (int l66 = 0; l66 < 1024; l66 = faust_wrap_add(l66, 1)) {
			fVec9[l66] = 0.0f;
		}
		for (int l67 = 0; l67 < 2; l67 = faust_wrap_add(l67, 1)) {
			fRec8[l67] = 0.0f;
		}
		for (int l68 = 0; l68 < 2; l68 = faust_wrap_add(l68, 1)) {
			fRec4[l68] = 0.0f;
		}
		for (int l69 = 0; l69 < 2; l69 = faust_wrap_add(l69, 1)) {
			fRec3[l69] = 0.0f;
		}
		for (int l70 = 0; l70 < 2; l70 = faust_wrap_add(l70, 1)) {
			fRec63[l70] = 0.0f;
		}
		for (int l71 = 0; l71 < 2; l71 = faust_wrap_add(l71, 1)) {
			fRec64[l71] = 0.0f;
		}
		for (int l72 = 0; l72 < 2; l72 = faust_wrap_add(l72, 1)) {
			iRec65[l72] = 0;
		}
		for (int l73 = 0; l73 < 2; l73 = faust_wrap_add(l73, 1)) {
			fVec10[l73] = 0.0f;
		}
		for (int l74 = 0; l74 < 2; l74 = faust_wrap_add(l74, 1)) {
			fRec66[l74] = 0.0f;
		}
		for (int l75 = 0; l75 < 3; l75 = faust_wrap_add(l75, 1)) {
			fRec2[l75] = 0.0f;
		}
		for (int l76 = 0; l76 < 3; l76 = faust_wrap_add(l76, 1)) {
			fRec1[l76] = 0.0f;
		}
		for (int l77 = 0; l77 < 3; l77 = faust_wrap_add(l77, 1)) {
			fRec0[l77] = 0.0f;
		}
		for (int l78 = 0; l78 < 2; l78 = faust_wrap_add(l78, 1)) {
			fRec67[l78] = 0.0f;
		}
		for (int l79 = 0; l79 < 2; l79 = faust_wrap_add(l79, 1)) {
			fRec69[l79] = 0.0f;
		}
		for (int l80 = 0; l80 < 2; l80 = faust_wrap_add(l80, 1)) {
			iVec11[l80] = 0;
		}
		for (int l81 = 0; l81 < 2; l81 = faust_wrap_add(l81, 1)) {
			iRec68[l81] = 0;
		}
		for (int l82 = 0; l82 < 2; l82 = faust_wrap_add(l82, 1)) {
			iRec70[l82] = 0;
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
	
	virtual UrchinDrum* clone() {
		return new UrchinDrum(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("OneTrick URCHIN DSP");
		ui_interface->addNumEntry("GainAdjustment", &fEntry0, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+02f), FAUSTFLOAT(12.0f), FAUSTFLOAT(0.1f));
		ui_interface->addNumEntry("PitchWheel", &fEntry2, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addNumEntry("Transpose", &fEntry1, FAUSTFLOAT(0.0f), FAUSTFLOAT(-12.0f), FAUSTFLOAT(12.0f), FAUSTFLOAT(0.001f));
		ui_interface->addNumEntry("Trigger", &fEntry3, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.01f));
		ui_interface->addButton("WakeUp", &fButton0);
		ui_interface->declare(&fHslider10, "070", "");
		ui_interface->declare(&fHslider10, "export", "Transpose");
		ui_interface->declare(&fHslider10, "unit", "Hz");
		ui_interface->addHorizontalSlider("Tuning", &fHslider10, FAUSTFLOAT(1.2e+02f), FAUSTFLOAT(4e+01f), FAUSTFLOAT(2.4e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider4, "090", "");
		ui_interface->declare(&fHslider4, "export", "Shell Depth");
		ui_interface->declare(&fHslider4, "unit", "in");
		ui_interface->addHorizontalSlider("Shell_Depth", &fHslider4, FAUSTFLOAT(12.0f), FAUSTFLOAT(5.0f), FAUSTFLOAT(24.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider9, "1010", "");
		ui_interface->declare(&fHslider9, "export", "Lateness");
		ui_interface->declare(&fHslider9, "unit", "ms");
		ui_interface->addHorizontalSlider("Lateness", &fHslider9, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider0, "1020", "");
		ui_interface->declare(&fHslider0, "export", "Gain");
		ui_interface->declare(&fHslider0, "unit", "dB");
		ui_interface->addHorizontalSlider("Voice_Gain", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+02f), FAUSTFLOAT(6.0f), FAUSTFLOAT(0.1f));
		ui_interface->declare(&fHslider1, "1030", "");
		ui_interface->declare(&fHslider1, "export", "Pan");
		ui_interface->declare(&fHslider1, "unit", "%");
		ui_interface->addHorizontalSlider("Voice_Pan", &fHslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1e+02f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider21, "1050", "");
		ui_interface->declare(&fHslider21, "export", "Reverb Send");
		ui_interface->declare(&fHslider21, "unit", "%");
		ui_interface->addHorizontalSlider("Voice_Reverb", &fHslider21, FAUSTFLOAT(5e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider18, "1060", "");
		ui_interface->declare(&fHslider18, "export", "Voice Punchiness");
		ui_interface->declare(&fHslider18, "unit", "%");
		ui_interface->addHorizontalSlider("Voice_Punchiness", &fHslider18, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider19, "1070", "");
		ui_interface->declare(&fHslider19, "export", "Cutoff");
		ui_interface->declare(&fHslider19, "unit", "kHz");
		ui_interface->addHorizontalSlider("Sample_Cutoff", &fHslider19, FAUSTFLOAT(2e+01f), FAUSTFLOAT(2.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider2, "1080", "");
		ui_interface->declare(&fHslider2, "export", "Sample ToneFreq");
		ui_interface->declare(&fHslider2, "unit", "kHz");
		ui_interface->addHorizontalSlider("Sample_ToneFreq", &fHslider2, FAUSTFLOAT(1.5f), FAUSTFLOAT(1.0f), FAUSTFLOAT(12.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider3, "1090", "");
		ui_interface->declare(&fHslider3, "export", "Sample ToneGain");
		ui_interface->declare(&fHslider3, "unit", "db");
		ui_interface->addHorizontalSlider("Sample_ToneGain", &fHslider3, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(6.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider6, "110", "");
		ui_interface->declare(&fHslider6, "export", "Shell Damp");
		ui_interface->declare(&fHslider6, "unit", "%");
		ui_interface->addHorizontalSlider("Shell_Damp", &fHslider6, FAUSTFLOAT(3e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider20, "1160", "");
		ui_interface->declare(&fHslider20, "export", "Sample Length");
		ui_interface->declare(&fHslider20, "minlabel", "∞");
		ui_interface->declare(&fHslider20, "unit", "s");
		ui_interface->addHorizontalSlider("Sample_Length", &fHslider20, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(3.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider5, "1165", "");
		ui_interface->declare(&fHslider5, "export", "Sample Speed");
		ui_interface->declare(&fHslider5, "unit", "st");
		ui_interface->addHorizontalSlider("Sample_Speed", &fHslider5, FAUSTFLOAT(0.0f), FAUSTFLOAT(-12.0f), FAUSTFLOAT(12.0f), FAUSTFLOAT(0.001f));
		ui_interface->declare(&fHslider7, "1170", "");
		ui_interface->declare(&fHslider7, "export", "Mix Strike");
		ui_interface->declare(&fHslider7, "unit", "%");
		ui_interface->addHorizontalSlider("Mix_Strike", &fHslider7, FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider8, "1180", "");
		ui_interface->declare(&fHslider8, "export", "Strike Brightness");
		ui_interface->declare(&fHslider8, "unit", "%");
		ui_interface->addHorizontalSlider("Strike_Brightness", &fHslider8, FAUSTFLOAT(75.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.001f));
		ui_interface->declare(&fHslider16, "120", "");
		ui_interface->declare(&fHslider16, "export", "Reso Tuning");
		ui_interface->declare(&fHslider16, "unit", "st");
		ui_interface->addHorizontalSlider("Reso_Tuning", &fHslider16, FAUSTFLOAT(0.0f), FAUSTFLOAT(-6.0f), FAUSTFLOAT(6.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider12, "130", "");
		ui_interface->declare(&fHslider12, "export", "Decay");
		ui_interface->declare(&fHslider12, "unit", "s");
		ui_interface->addHorizontalSlider("Decay", &fHslider12, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.15f), FAUSTFLOAT(5.0f), FAUSTFLOAT(0.0001f));
		ui_interface->declare(&fHslider13, "140", "");
		ui_interface->declare(&fHslider13, "export", "Beater Damp");
		ui_interface->declare(&fHslider13, "unit", "%");
		ui_interface->addHorizontalSlider("Beater_Dampening", &fHslider13, FAUSTFLOAT(5e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.0001f));
		ui_interface->declare(&fHslider14, "160", "");
		ui_interface->declare(&fHslider14, "export", "Reso Head");
		ui_interface->declare(&fHslider14, "type", "bool");
		ui_interface->addHorizontalSlider("Reso_Head", &fHslider14, FAUSTFLOAT(1.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->declare(&fHslider17, "200", "");
		ui_interface->declare(&fHslider17, "export", "Mix Ring");
		ui_interface->declare(&fHslider17, "unit", "%");
		ui_interface->addHorizontalSlider("Mix_Ring", &fHslider17, FAUSTFLOAT(35.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider15, "210", "");
		ui_interface->declare(&fHslider15, "export", "Mix Reso");
		ui_interface->declare(&fHslider15, "unit", "%");
		ui_interface->addHorizontalSlider("Mix_Reso", &fHslider15, FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider11, "250", "");
		ui_interface->declare(&fHslider11, "export", "Detune Range");
		ui_interface->declare(&fHslider11, "unit", "st");
		ui_interface->addHorizontalSlider("Detune_Range", &fHslider11, FAUSTFLOAT(8.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(32.0f), FAUSTFLOAT(0.001f));
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
		float fSlow13 = 2.0f * (1.0f - 1.0f / UrchinDrum_faustpower2_f(fSlow4));
		float fSlow14 = static_cast<float>(fHslider4);
		float fSlow15 = fConst9 * fSlow14;
		float fSlow16 = static_cast<float>(fEntry1) + static_cast<float>(fHslider5) + 12.0f * static_cast<float>(fEntry2);
		float fSlow17 = static_cast<float>(fButton0);
		float fSlow18 = 1.0f - 0.009999f * static_cast<float>(fHslider6);
		float fSlow19 = static_cast<float>(fHslider7);
		float fSlow20 = static_cast<float>(fHslider8);
		float fSlow21 = 0.007079458f * std::pow(1e+01f, 0.05f * (-12.0f - 0.09f * fSlow20));
		float fSlow22 = 4.4e+02f * std::pow(2.0f, 0.083333336f * (0.3101955f * fSlow20 + -41.51318f));
		float fSlow23 = 4.4e+02f * std::pow(2.0f, 0.083333336f * (0.5186314f * fSlow20 + 2.213095f));
		float fSlow24 = static_cast<float>(fEntry3);
		int iSlow25 = static_cast<int>(std::min<float>(fConst15, std::max<float>(0.0f, fConst16 * static_cast<float>(fHslider9))));
		float fSlow26 = 0.0067f * fSlow20 + 0.33f;
		float fSlow27 = fSlow14 + -5.0f;
		float fSlow28 = fConst13 * (3e+03f - 105.26316f * fSlow27);
		float fSlow29 = fConst13 * (4.4e+03f - 152.63158f * fSlow27);
		float fSlow30 = static_cast<float>(fHslider10);
		float fSlow31 = 0.083333336f * static_cast<float>(fHslider11);
		float fSlow32 = static_cast<float>(fHslider12);
		float fSlow33 = 0.25f * fSlow32 * (1.0f - 0.008f * static_cast<float>(fHslider13));
		float fSlow34 = 0.002f * static_cast<float>(fHslider14) * static_cast<float>(fHslider15);
		float fSlow35 = fSlow30 * std::pow(2.0f, 0.083333336f * static_cast<float>(fHslider16));
		float fSlow36 = 0.25f * fSlow32;
		float fSlow37 = 0.25f * fSlow32 * fSlow30;
		float fSlow38 = 0.00019952623f * static_cast<float>(fHslider17) * std::pow(1e+01f, -(0.031578947f * fSlow27));
		float fSlow39 = fConst22 * std::pow(2.0f, 0.083333336f * (8.283786f - 0.31951007f * fSlow27));
		float fSlow40 = 0.125f * fSlow32;
		float fSlow41 = 0.01f * static_cast<float>(fHslider18);
		float fSlow42 = (fSlow5 - fSlow11) / fSlow4 + 1.0f;
		float fSlow43 = ((iSlow7) ? fSlow9 : fSlow10);
		float fSlow44 = (fSlow5 + fSlow43) / fSlow4 + 1.0f;
		float fSlow45 = (fSlow5 - fSlow43) / fSlow4 + 1.0f;
		float fSlow46 = 1e+03f * static_cast<float>(fHslider19);
		float fSlow47 = static_cast<float>(fHslider20);
		float fSlow48 = static_cast<float>(fSlow47 > 0.0f);
		float fSlow49 = fConst0 * fSlow47;
		float fSlow50 = 0.5f * fSlow0 * std::min<float>(1.0f, fSlow1 + 1.0f);
		float fSlow51 = 0.005f * static_cast<float>(fHslider21) * fSlow0;
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			iVec0[0] = 1;
			fRec5[0] = 0.08f * fRec8[1] + 0.92f * fRec5[1];
			fRec6[0] = 0.08f * fRec7[1] + 0.92f * fRec6[1];
			fVec1[0] = fSlow17;
			float fTemp0 = static_cast<float>(fSlow17 <= fVec1[1]);
			float fTemp1 = 1.0f - fConst10 * fTemp0;
			fRec9[0] = fSlow16 * fTemp1 + fConst10 * fTemp0 * fRec9[1];
			float fTemp2 = std::pow(2.0f, 0.083333336f * fRec9[0]);
			float fTemp3 = fSlow15 / fTemp2;
			float fTemp4 = -1.499995f + fTemp3;
			float fTemp5 = std::floor(fTemp4);
			float fTemp6 = -4.0f - fTemp5 + fTemp3;
			float fTemp7 = -3.0f - fTemp5 + fTemp3;
			float fTemp8 = -2.0f - fTemp5 + fTemp3;
			fRec10[0] = fSlow18 * fTemp1 + fConst10 * fTemp0 * fRec10[1];
			float fTemp9 = std::max<float>(0.01f, fRec10[0]);
			iRec15[0] = faust_wrap_add(1, iRec15[1]);
			iRec16[0] = faust_wrap_add(12345, faust_wrap_mul(1103515245, iRec16[1]));
			fRec14[0] = (((iRec15[1] % iConst12) == 0) ? 4.656613e-10f * static_cast<float>(iRec16[0]) : fRec14[1]);
			float fTemp10 = ((fRec14[0] != fRec14[1]) ? fConst11 : -1.0f + fRec17[1]);
			fRec17[0] = fTemp10;
			fRec13[0] = ((fTemp10 > 0.0f) ? fRec13[1] + (fRec14[0] - fRec13[1]) / fTemp10 : fRec14[0]);
			float fTemp11 = std::tan(fConst13 * std::max<float>(4e+01f, fSlow22 * fTemp2));
			float fTemp12 = 1.0f / fTemp11;
			float fTemp13 = UrchinDrum_faustpower2_f(fTemp11);
			float fTemp14 = 1.0f + (1.4142135f + fTemp12) / fTemp11;
			fRec12[0] = fRec13[0] - (fRec12[2] * (1.0f + (-1.4142135f + fTemp12) / fTemp11) + 2.0f * fRec12[1] * (1.0f - 1.0f / fTemp13)) / fTemp14;
			float fTemp15 = std::tan(fConst13 * std::min<float>(fConst14, fSlow23 * fTemp2));
			float fTemp16 = 1.0f / fTemp15;
			float fTemp17 = 1.0f + (1.4142135f + fTemp16) / fTemp15;
			fRec11[0] = (fRec12[2] + (fRec12[0] - 2.0f * fRec12[1])) / (fTemp13 * fTemp14) - (fRec11[2] * (1.0f + (-1.4142135f + fTemp16) / fTemp15) + 2.0f * fRec11[1] * (1.0f - 1.0f / UrchinDrum_faustpower2_f(fTemp15))) / fTemp17;
			fVec2[IOTA0 & 4095] = fSlow24;
			float fTemp18 = fVec2[(faust_wrap_sub(IOTA0, iSlow25)) & 4095];
			fVec3[0] = fTemp18;
			int iTemp19 = fTemp18 > fVec3[1];
			iVec4[0] = iTemp19;
			int iTemp20 = iTemp19 > iVec4[1];
			iVec5[0] = iTemp20;
			iRec18[0] = faust_wrap_add(iTemp20, faust_wrap_mul(faust_wrap_add(iRec18[1], iRec18[1] > 0), iTemp19 <= iVec4[1]));
			float fTemp21 = static_cast<float>(iRec18[0]);
			float fTemp22 = std::max<float>(1.0f, fConst17 / fTemp2);
			fRec19[0] = ((iTemp19) ? 0.99f * fTemp18 + 0.01f : fRec19[1]);
			float fTemp23 = static_cast<float>(iTemp20);
			float fTemp24 = static_cast<float>(faust_wrap_sub(1, iTemp20));
			float fTemp25 = 0.0375f * fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec21[1]) - 1.0f));
			int iTemp26 = std::fabs(fTemp25) < 1.1920929e-07f;
			float fTemp27 = ((iTemp26) ? 0.0f : std::exp(-(fConst21 / ((iTemp26) ? 1.0f : fTemp25))));
			float fTemp28 = 0.5f * fRec19[0];
			fRec21[0] = fTemp23 * (1.0f - fTemp27) * std::min<float>(1.0f, std::max<float>(fTemp28 + fRec21[1], fRec19[0])) + fRec21[1] * fTemp27;
			fRec20[0] = fConst20 * fRec21[0] + fConst19 * fRec20[1];
			float fTemp29 = std::tan(fSlow28 * fTemp2);
			float fTemp30 = 1.0f / fTemp29;
			float fTemp31 = UrchinDrum_faustpower2_f(fTemp29);
			float fTemp32 = 1.0f + (1.4142135f + fTemp30) / fTemp29;
			fRec23[0] = fSlow26 * fRec13[0] * (0.06f + 0.29f * UrchinDrum_faustpower2_f(fRec19[0])) - (fRec23[2] * (1.0f + (-1.4142135f + fTemp30) / fTemp29) + 2.0f * fRec23[1] * (1.0f - 1.0f / fTemp31)) / fTemp32;
			float fTemp33 = std::tan(fSlow29 * fTemp2 * (0.8f + 0.3f * fRec19[0]) * (0.8f + 0.2f * fRec20[0]));
			float fTemp34 = 1.0f / fTemp33;
			float fTemp35 = 1.0f + (1.4142135f + fTemp34) / fTemp33;
			fRec22[0] = (fRec23[2] + (fRec23[0] - 2.0f * fRec23[1])) / (fTemp31 * fTemp32) - (fRec22[2] * (1.0f + (-1.4142135f + fTemp34) / fTemp33) + 2.0f * fRec22[1] * (1.0f - 1.0f / UrchinDrum_faustpower2_f(fTemp33))) / fTemp35;
			int iTemp36 = faust_wrap_sub(1, iVec0[1]);
			float fTemp37 = fSlow33 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec27[1]) - 1.0f)) / fTemp2);
			int iTemp38 = std::fabs(fTemp37) < 1.1920929e-07f;
			float fTemp39 = ((iTemp38) ? 0.0f : std::exp(-(fConst21 / ((iTemp38) ? 1.0f : fTemp37))));
			fRec27[0] = (1.0f - fTemp39) * std::min<float>(1.0f, std::max<float>(fRec27[1] + fTemp28, fRec19[0])) * fTemp23 + fRec27[1] * fTemp39;
			fRec26[0] = fConst20 * fRec27[0] + fConst19 * fRec26[1];
			float fTemp40 = std::max<float>(4e+01f, fSlow30 * fTemp2 * std::pow(2.0f, fSlow31 * std::max<float>(0.0f, 1.1764706f * (-0.15f + fRec26[0]))));
			float fTemp41 = -2e+01f + fTemp40;
			float fTemp42 = 2e+01f + 2.0f * fTemp41;
			float fTemp43 = ((iTemp36) ? 0.0f : fRec25[1] + fConst21 * fTemp42);
			fRec25[0] = fTemp43 - std::floor(fTemp43);
			float fTemp44 = fTemp24 * fTemp40;
			float fTemp45 = fSlow33 * (fTemp44 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec29[1]) - 1.0f)) / (fTemp2 * fTemp42));
			int iTemp46 = std::fabs(fTemp45) < 1.1920929e-07f;
			float fTemp47 = ((iTemp46) ? 0.0f : std::exp(-(fConst21 / ((iTemp46) ? 1.0f : fTemp45))));
			fRec29[0] = fTemp23 * (1.0f - fTemp47) * std::min<float>(1.0f, std::max<float>(fTemp28 + fRec29[1], fRec19[0])) + fRec29[1] * fTemp47;
			fRec28[0] = fConst20 * fRec29[0] + fConst19 * fRec28[1];
			float fTemp48 = 2e+01f + 4.0f * fTemp41;
			float fTemp49 = ((iTemp36) ? 0.0f : fRec30[1] + fConst21 * fTemp48);
			fRec30[0] = fTemp49 - std::floor(fTemp49);
			float fTemp50 = fSlow33 * (fTemp44 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec32[1]) - 1.0f)) / (fTemp2 * fTemp48));
			int iTemp51 = std::fabs(fTemp50) < 1.1920929e-07f;
			float fTemp52 = ((iTemp51) ? 0.0f : std::exp(-(fConst21 / ((iTemp51) ? 1.0f : fTemp50))));
			fRec32[0] = fTemp23 * (1.0f - fTemp52) * std::min<float>(1.0f, std::max<float>(fTemp28 + fRec32[1], fRec19[0])) + fRec32[1] * fTemp52;
			fRec31[0] = fConst20 * fRec32[0] + fConst19 * fRec31[1];
			float fTemp53 = ((iTemp36) ? 0.0f : fRec33[1] + fConst21 * fTemp40);
			fRec33[0] = fTemp53 - std::floor(fTemp53);
			float fTemp54 = fSlow33 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec35[1]) - 1.0f)) / fTemp2);
			int iTemp55 = std::fabs(fTemp54) < 1.1920929e-07f;
			float fTemp56 = ((iTemp55) ? 0.0f : std::exp(-(fConst21 / ((iTemp55) ? 1.0f : fTemp54))));
			fRec35[0] = fTemp23 * (1.0f - fTemp56) * std::min<float>(1.0f, std::max<float>(fTemp28 + fRec35[1], fRec19[0])) + fRec35[1] * fTemp56;
			fRec34[0] = fConst20 * fRec35[0] + fConst19 * fRec34[1];
			float fTemp57 = 2e+01f + 3.0f * fTemp41;
			float fTemp58 = ((iTemp36) ? 0.0f : fRec36[1] + fConst21 * fTemp57);
			fRec36[0] = fTemp58 - std::floor(fTemp58);
			float fTemp59 = fSlow33 * (fTemp44 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec38[1]) - 1.0f)) / (fTemp2 * fTemp57));
			int iTemp60 = std::fabs(fTemp59) < 1.1920929e-07f;
			float fTemp61 = ((iTemp60) ? 0.0f : std::exp(-(fConst21 / ((iTemp60) ? 1.0f : fTemp59))));
			fRec38[0] = fTemp23 * (1.0f - fTemp61) * std::min<float>(1.0f, std::max<float>(fTemp28 + fRec38[1], fRec19[0])) + fRec38[1] * fTemp61;
			fRec37[0] = fConst20 * fRec38[0] + fConst19 * fRec37[1];
			float fTemp62 = 2e+01f + 5.0f * fTemp41;
			float fTemp63 = ((iTemp36) ? 0.0f : fRec39[1] + fConst21 * fTemp62);
			fRec39[0] = fTemp63 - std::floor(fTemp63);
			float fTemp64 = fSlow33 * (fTemp44 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec41[1]) - 1.0f)) / (fTemp2 * fTemp62));
			int iTemp65 = std::fabs(fTemp64) < 1.1920929e-07f;
			float fTemp66 = ((iTemp65) ? 0.0f : std::exp(-(fConst21 / ((iTemp65) ? 1.0f : fTemp64))));
			fRec41[0] = fTemp23 * (1.0f - fTemp66) * std::min<float>(1.0f, std::max<float>(fTemp28 + fRec41[1], fRec19[0])) + fRec41[1] * fTemp66;
			fRec40[0] = fConst20 * fRec41[0] + fConst19 * fRec40[1];
			float fTemp67 = fSlow36 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec44[1]) - 1.0f)) / fTemp2);
			int iTemp68 = std::fabs(fTemp67) < 1.1920929e-07f;
			float fTemp69 = ((iTemp68) ? 0.0f : std::exp(-(fConst21 / ((iTemp68) ? 1.0f : fTemp67))));
			fRec44[0] = fTemp23 * (1.0f - fTemp69) * std::min<float>(1.0f, std::max<float>(fTemp28 + fRec44[1], fRec19[0])) + fRec44[1] * fTemp69;
			fRec43[0] = fConst20 * fRec44[0] + fConst19 * fRec43[1];
			float fTemp70 = std::max<float>(4e+01f, fSlow35 * fTemp2 * std::pow(2.0f, fSlow31 * std::max<float>(0.0f, 1.1764706f * (-0.15f + fRec43[0]))));
			float fTemp71 = -2e+01f + fTemp70;
			float fTemp72 = 2e+01f + 2.0f * fTemp71;
			float fTemp73 = ((iTemp36) ? 0.0f : fRec42[1] + fConst21 * fTemp72);
			fRec42[0] = fTemp73 - std::floor(fTemp73);
			float fTemp74 = fSlow37 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec46[1]) - 1.0f)) / fTemp72);
			int iTemp75 = std::fabs(fTemp74) < 1.1920929e-07f;
			float fTemp76 = ((iTemp75) ? 0.0f : std::exp(-(fConst21 / ((iTemp75) ? 1.0f : fTemp74))));
			float fTemp77 = fRec19[0] * std::pow(1e+01f, -(0.7f * (1.0f - fRec19[0])));
			float fTemp78 = 0.5f * fTemp77;
			fRec46[0] = fTemp23 * (1.0f - fTemp76) * std::min<float>(1.0f, std::max<float>(fTemp78 + fRec46[1], fTemp77)) + fRec46[1] * fTemp76;
			fRec45[0] = fConst20 * fRec46[0] + fConst19 * fRec45[1];
			float fTemp79 = 2e+01f + 4.0f * fTemp71;
			float fTemp80 = ((iTemp36) ? 0.0f : fRec47[1] + fConst21 * fTemp79);
			fRec47[0] = fTemp80 - std::floor(fTemp80);
			float fTemp81 = fSlow37 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec49[1]) - 1.0f)) / fTemp79);
			int iTemp82 = std::fabs(fTemp81) < 1.1920929e-07f;
			float fTemp83 = ((iTemp82) ? 0.0f : std::exp(-(fConst21 / ((iTemp82) ? 1.0f : fTemp81))));
			fRec49[0] = fTemp23 * (1.0f - fTemp83) * std::min<float>(1.0f, std::max<float>(fTemp78 + fRec49[1], fTemp77)) + fRec49[1] * fTemp83;
			fRec48[0] = fConst20 * fRec49[0] + fConst19 * fRec48[1];
			float fTemp84 = ((iTemp36) ? 0.0f : fRec50[1] + fConst21 * fTemp70);
			fRec50[0] = fTemp84 - std::floor(fTemp84);
			float fTemp85 = fSlow37 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec52[1]) - 1.0f)) / fTemp70);
			int iTemp86 = std::fabs(fTemp85) < 1.1920929e-07f;
			float fTemp87 = ((iTemp86) ? 0.0f : std::exp(-(fConst21 / ((iTemp86) ? 1.0f : fTemp85))));
			fRec52[0] = fTemp23 * (1.0f - fTemp87) * std::min<float>(1.0f, std::max<float>(fRec52[1] + fTemp78, fTemp77)) + fRec52[1] * fTemp87;
			fRec51[0] = fConst20 * fRec52[0] + fConst19 * fRec51[1];
			float fTemp88 = 2e+01f + 3.0f * fTemp71;
			float fTemp89 = ((iTemp36) ? 0.0f : fRec53[1] + fConst21 * fTemp88);
			fRec53[0] = fTemp89 - std::floor(fTemp89);
			float fTemp90 = fSlow37 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec55[1]) - 1.0f)) / fTemp88);
			int iTemp91 = std::fabs(fTemp90) < 1.1920929e-07f;
			float fTemp92 = ((iTemp91) ? 0.0f : std::exp(-(fConst21 / ((iTemp91) ? 1.0f : fTemp90))));
			fRec55[0] = fTemp23 * (1.0f - fTemp92) * std::min<float>(1.0f, std::max<float>(fTemp78 + fRec55[1], fTemp77)) + fRec55[1] * fTemp92;
			fRec54[0] = fConst20 * fRec55[0] + fConst19 * fRec54[1];
			float fTemp93 = 2e+01f + 5.0f * fTemp71;
			float fTemp94 = ((iTemp36) ? 0.0f : fRec56[1] + fConst21 * fTemp93);
			fRec56[0] = fTemp94 - std::floor(fTemp94);
			float fTemp95 = fSlow37 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec58[1]) - 1.0f)) / fTemp93);
			int iTemp96 = std::fabs(fTemp95) < 1.1920929e-07f;
			float fTemp97 = ((iTemp96) ? 0.0f : std::exp(-(fConst21 / ((iTemp96) ? 1.0f : fTemp95))));
			fRec58[0] = fTemp23 * (1.0f - fTemp97) * std::min<float>(1.0f, std::max<float>(fTemp78 + fRec58[1], fTemp77)) + fRec58[1] * fTemp97;
			fRec57[0] = fConst20 * fRec58[0] + fConst19 * fRec57[1];
			float fTemp98 = ((iTemp36) ? 0.0f : fConst23 + fRec60[1]);
			fRec60[0] = fTemp98 - std::floor(fTemp98);
			float fTemp99 = ((iTemp36) ? 0.0f : fRec59[1] + fSlow39 * fTemp2 * (1.0f + 0.25f * ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec60[0])]));
			fRec59[0] = fTemp99 - std::floor(fTemp99);
			float fTemp100 = fSlow40 * (fTemp24 * (1.75f + 1.7f * (UrchinDrum_faustpower3_f(1.0f - fRec62[1]) - 1.0f)) / fTemp2);
			int iTemp101 = std::fabs(fTemp100) < 1.1920929e-07f;
			float fTemp102 = ((iTemp101) ? 0.0f : std::exp(-(fConst21 / ((iTemp101) ? 1.0f : fTemp100))));
			float fTemp103 = UrchinDrum_faustpower3_f(fRec19[0]);
			fRec62[0] = fTemp23 * (1.0f - fTemp102) * std::min<float>(1.0f, std::max<float>(fRec62[1] + 0.5f * fTemp103, fTemp103)) + fRec62[1] * fTemp102;
			fRec61[0] = fConst20 * fRec62[0] + fConst19 * fRec61[1];
			fVec7[0] = fSlow19 * (fSlow21 * ((fRec11[2] + fRec11[0] + 2.0f * fRec11[1]) * std::max<float>(0.0f, std::min<float>(fTemp21 / fTemp22, 1.0f + (fTemp22 - fTemp21) / std::max<float>(1.0f, fConst18 / fTemp2))) * fRec19[0] / fTemp17) + 0.01f * (UrchinDrum_faustpower2_f(fRec20[0]) * (fRec22[2] + fRec22[0] + 2.0f * fRec22[1]) / fTemp35)) + 0.2f * (ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec25[0])] * fRec28[0] * std::pow(fTemp40 / fTemp42, 1.6f) + ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec30[0])] * fRec31[0] * std::pow(fTemp40 / fTemp48, 1.6f) - (ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec33[0])] * fRec34[0] + ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec36[0])] * fRec37[0] * std::pow(fTemp40 / fTemp57, 1.6f) + ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec39[0])] * fRec40[0] * std::pow(fTemp40 / fTemp62, 1.6f))) + fSlow34 * (ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec42[0])] * fRec45[0] * std::pow(fTemp70 / fTemp72, 1.6f) + ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec47[0])] * fRec48[0] * std::pow(fTemp70 / fTemp79, 1.6f) - (ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec50[0])] * fRec51[0] + ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec53[0])] * fRec54[0] * std::pow(fTemp70 / fTemp88, 1.6f) + ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec56[0])] * fRec57[0] * std::pow(fTemp70 / fTemp93, 1.6f))) + fSlow38 * ftbl0UrchinDrumSIG0[static_cast<int>(65536.0f * fRec59[0])] * fRec61[0];
			float fTemp104 = fRec5[0] * fTemp9 + fVec7[1];
			fVec8[IOTA0 & 1023] = fTemp104;
			int iTemp105 = static_cast<int>(fTemp4);
			int iTemp106 = static_cast<int>(std::min<float>(fConst24, static_cast<float>(std::max<int>(0, iTemp105))));
			float fTemp107 = -1.0f - fTemp5 + fTemp3;
			float fTemp108 = fTemp3 - fTemp5;
			int iTemp109 = static_cast<int>(std::min<float>(fConst24, static_cast<float>(std::max<int>(0, faust_wrap_add(1, iTemp105)))));
			float fTemp110 = fTemp108 * fTemp107;
			int iTemp111 = static_cast<int>(std::min<float>(fConst24, static_cast<float>(std::max<int>(0, faust_wrap_add(2, iTemp105)))));
			float fTemp112 = fTemp110 * fTemp8;
			int iTemp113 = static_cast<int>(std::min<float>(fConst24, static_cast<float>(std::max<int>(0, faust_wrap_add(3, iTemp105)))));
			float fTemp114 = fTemp112 * fTemp7;
			int iTemp115 = static_cast<int>(std::min<float>(fConst24, static_cast<float>(std::max<int>(0, faust_wrap_add(4, iTemp105)))));
			fRec7[0] = fTemp6 * (fTemp7 * (fTemp8 * (0.041666668f * fVec8[(faust_wrap_sub(IOTA0, iTemp106)) & 1023] * fTemp107 - 0.16666667f * fTemp108 * fVec8[(faust_wrap_sub(IOTA0, iTemp109)) & 1023]) + 0.25f * fTemp110 * fVec8[(faust_wrap_sub(IOTA0, iTemp111)) & 1023]) - 0.16666667f * fTemp112 * fVec8[(faust_wrap_sub(IOTA0, iTemp113)) & 1023]) + 0.041666668f * fTemp114 * fVec8[(faust_wrap_sub(IOTA0, iTemp115)) & 1023];
			float fTemp116 = fTemp9 * fRec6[0];
			fVec9[IOTA0 & 1023] = fTemp116;
			fRec8[0] = fTemp6 * (fTemp7 * (fTemp8 * (0.041666668f * fTemp107 * fVec9[(faust_wrap_sub(IOTA0, iTemp106)) & 1023] - 0.16666667f * fTemp108 * fVec9[(faust_wrap_sub(IOTA0, iTemp109)) & 1023]) + 0.25f * fTemp110 * fVec9[(faust_wrap_sub(IOTA0, iTemp111)) & 1023]) - 0.16666667f * fTemp112 * fVec9[(faust_wrap_sub(IOTA0, iTemp113)) & 1023]) + 0.041666668f * fTemp114 * fVec9[(faust_wrap_sub(IOTA0, iTemp115)) & 1023];
			fRec4[0] = fConst8 * (fRec7[0] - fRec7[1] + fConst25 * fRec4[1]);
			fRec3[0] = -(fConst5 * (fConst6 * fRec3[1] - fConst4 * (fRec4[0] - fRec4[1])));
			fRec63[0] = fSlow41 * fTemp1 + fConst10 * fTemp0 * fRec63[1];
			fRec64[0] = ((iTemp20 > 0) ? fConst26 : std::max<float>(0.0f, -1.0f + fRec64[1]));
			iRec65[0] = (iTemp20 > iVec5[1]) + faust_wrap_mul(faust_wrap_add(iRec65[1], iRec65[1] > 0), iTemp20 <= iVec5[1]);
			float fTemp117 = static_cast<float>(iRec65[0]);
			float fTemp118 = std::max<float>(0.0f, std::min<float>(fConst28 * fTemp117, 1.0f + (fConst27 - fTemp117) / std::max<float>(1.0f, fConst0 * ((0.5f - 0.35f * fRec63[0]) / fTemp2))));
			fVec10[0] = fTemp118;
			fRec66[0] = ((iTemp20) ? fVec10[1] : fRec66[1]);
			float fTemp119 = fSlow13 * fRec2[1];
			fRec2[0] = 1.5848932f * fRec3[0] * (1.0f - fRec63[0] + fRec63[0] * ((fRec64[0] > 0.0f) ? fRec66[0] + fTemp118 * (1.0f - fRec66[0]) : fTemp118)) - fSlow12 * (fSlow42 * fRec2[2] + fTemp119);
			float fTemp120 = std::tan(fConst13 * std::min<float>(fConst14, fSlow46 * fTemp2));
			float fTemp121 = 1.0f / fTemp120;
			float fTemp122 = 1.0f - 1.0f / UrchinDrum_faustpower2_f(fTemp120);
			float fTemp123 = 1.0f + (1.847759f + fTemp121) / fTemp120;
			fRec1[0] = fSlow12 * (fTemp119 + fSlow44 * fRec2[0] + fSlow45 * fRec2[2]) - (fRec1[2] * (1.0f + (-1.847759f + fTemp121) / fTemp120) + 2.0f * fRec1[1] * fTemp122) / fTemp123;
			float fTemp124 = 1.0f + (0.76536685f + fTemp121) / fTemp120;
			fRec0[0] = (fRec1[2] + fRec1[0] + 2.0f * fRec1[1]) / fTemp123 - (fRec0[2] * (1.0f + (-0.76536685f + fTemp121) / fTemp120) + 2.0f * fTemp122 * fRec0[1]) / fTemp124;
			float fTemp125 = (fRec0[2] + fRec0[0] + 2.0f * fRec0[1]) / fTemp124;
			fRec67[0] = fSlow48 * fTemp1 + fConst10 * fTemp0 * fRec67[1];
			fRec69[0] = ((iTemp19 > 0) ? fSlow49 / fTemp2 : std::max<float>(0.0f, -1.0f + fRec69[1]));
			int iTemp126 = fRec69[0] > 0.0f;
			iVec11[0] = iTemp126;
			iRec68[0] = faust_wrap_add(iTemp126, faust_wrap_mul(iRec68[1], iVec11[1] >= iTemp126));
			iRec70[0] = faust_wrap_mul(faust_wrap_add(1, iRec70[1]), iTemp126 == 0);
			float fTemp127 = (fTemp125 + tanhf(fTemp125)) * (1.0f + fRec67[0] * (-1.0f + std::max<float>(0.0f, std::min<float>(static_cast<float>(iRec68[0]), 1.0f) * (1.0f - fConst29 * static_cast<float>(iRec70[0])))));
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow2 * fTemp127);
			output1[i0] = static_cast<FAUSTFLOAT>(fSlow50 * fTemp127);
			output2[i0] = static_cast<FAUSTFLOAT>(fSlow51 * fTemp127);
			iVec0[1] = iVec0[0];
			fRec5[1] = fRec5[0];
			fRec6[1] = fRec6[0];
			fVec1[1] = fVec1[0];
			fRec9[1] = fRec9[0];
			fRec10[1] = fRec10[0];
			iRec15[1] = iRec15[0];
			iRec16[1] = iRec16[0];
			fRec14[1] = fRec14[0];
			fRec17[1] = fRec17[0];
			fRec13[1] = fRec13[0];
			fRec12[2] = fRec12[1];
			fRec12[1] = fRec12[0];
			fRec11[2] = fRec11[1];
			fRec11[1] = fRec11[0];
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fVec3[1] = fVec3[0];
			iVec4[1] = iVec4[0];
			iVec5[1] = iVec5[0];
			iRec18[1] = iRec18[0];
			fRec19[1] = fRec19[0];
			fRec21[1] = fRec21[0];
			fRec20[1] = fRec20[0];
			fRec23[2] = fRec23[1];
			fRec23[1] = fRec23[0];
			fRec22[2] = fRec22[1];
			fRec22[1] = fRec22[0];
			fRec27[1] = fRec27[0];
			fRec26[1] = fRec26[0];
			fRec25[1] = fRec25[0];
			fRec29[1] = fRec29[0];
			fRec28[1] = fRec28[0];
			fRec30[1] = fRec30[0];
			fRec32[1] = fRec32[0];
			fRec31[1] = fRec31[0];
			fRec33[1] = fRec33[0];
			fRec35[1] = fRec35[0];
			fRec34[1] = fRec34[0];
			fRec36[1] = fRec36[0];
			fRec38[1] = fRec38[0];
			fRec37[1] = fRec37[0];
			fRec39[1] = fRec39[0];
			fRec41[1] = fRec41[0];
			fRec40[1] = fRec40[0];
			fRec44[1] = fRec44[0];
			fRec43[1] = fRec43[0];
			fRec42[1] = fRec42[0];
			fRec46[1] = fRec46[0];
			fRec45[1] = fRec45[0];
			fRec47[1] = fRec47[0];
			fRec49[1] = fRec49[0];
			fRec48[1] = fRec48[0];
			fRec50[1] = fRec50[0];
			fRec52[1] = fRec52[0];
			fRec51[1] = fRec51[0];
			fRec53[1] = fRec53[0];
			fRec55[1] = fRec55[0];
			fRec54[1] = fRec54[0];
			fRec56[1] = fRec56[0];
			fRec58[1] = fRec58[0];
			fRec57[1] = fRec57[0];
			fRec60[1] = fRec60[0];
			fRec59[1] = fRec59[0];
			fRec62[1] = fRec62[0];
			fRec61[1] = fRec61[0];
			fVec7[1] = fVec7[0];
			fRec7[1] = fRec7[0];
			fRec8[1] = fRec8[0];
			fRec4[1] = fRec4[0];
			fRec3[1] = fRec3[0];
			fRec63[1] = fRec63[0];
			fRec64[1] = fRec64[0];
			iRec65[1] = iRec65[0];
			fVec10[1] = fVec10[0];
			fRec66[1] = fRec66[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			fRec67[1] = fRec67[0];
			fRec69[1] = fRec69[0];
			iVec11[1] = iVec11[0];
			iRec68[1] = iRec68[0];
			iRec70[1] = iRec70[0];
		}
	}

};

#endif
