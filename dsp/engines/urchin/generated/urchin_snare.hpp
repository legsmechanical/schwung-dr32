/* ------------------------------------------------------------
copyright: "Copyright (c) 2023 Punk Labs LLC"
license: "GPLv3 (or later)"
name: "OneTrick URCHIN DSP"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 0 -cn UrchinSnare -dtl 65536 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __UrchinSnare_H__
#define  __UrchinSnare_H__

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
#define FAUSTCLASS UrchinSnare
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

class UrchinSnareSIG0 {
	
  private:
	
	int iVec8[2];
	int iRec27[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsUrchinSnareSIG0() {
		return 0;
	}
	int getNumOutputsUrchinSnareSIG0() {
		return 1;
	}
	
	void instanceInitUrchinSnareSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			iVec8[l29] = 0;
		}
		for (int l30 = 0; l30 < 2; l30 = faust_wrap_add(l30, 1)) {
			iRec27[l30] = 0;
		}
	}
	
	void fillUrchinSnareSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec8[0] = 1;
			iRec27[0] = (faust_wrap_add(iVec8[1], iRec27[1])) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec27[0]));
			iVec8[1] = iVec8[0];
			iRec27[1] = iRec27[0];
		}
	}

};

static UrchinSnareSIG0* newUrchinSnareSIG0() { return (UrchinSnareSIG0*)new UrchinSnareSIG0(); }
static void deleteUrchinSnareSIG0(UrchinSnareSIG0* dsp) { delete dsp; }

static float UrchinSnare_faustpower2_f(float value) {
	return value * value;
}
static float UrchinSnare_faustpower4_f(float value) {
	return value * value * value * value;
}
static float UrchinSnare_faustpower3_f(float value) {
	return value * value * value;
}
static float ftbl0UrchinSnareSIG0[65536];

class UrchinSnare : public dsp {
	
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
	int IOTA0;
	float fVec2[1024];
	float fConst11;
	float fRec7[2];
	FAUSTFLOAT fHslider7;
	FAUSTFLOAT fHslider8;
	int iRec15[2];
	float fConst12;
	float fConst13;
	int iConst14;
	int iRec16[2];
	float fVec3[2];
	float fRec14[2];
	float fRec17[2];
	float fRec13[2];
	float fConst15;
	float fRec12[3];
	float fConst16;
	float fRec11[3];
	FAUSTFLOAT fEntry3;
	float fVec4[4096];
	float fConst17;
	float fConst18;
	FAUSTFLOAT fHslider9;
	float fVec5[2];
	int iVec6[2];
	int iVec7[2];
	int iRec18[2];
	float fConst19;
	float fConst20;
	float fRec19[2];
	float fConst21;
	float fConst22;
	float fConst23;
	FAUSTFLOAT fHslider10;
	float fConst24;
	float fConst25;
	float fConst26;
	float fConst27;
	float fConst28;
	float fRec21[2];
	float fConst29;
	float fConst30;
	float fConst31;
	float fConst32;
	float fConst33;
	float fConst34;
	float fConst35;
	float fConst36;
	float fRec20[5];
	float fConst37;
	float fConst38;
	float fRec22[5];
	float fConst39;
	float fConst40;
	float fRec24[2];
	float fRec23[2];
	float fRec26[3];
	float fRec25[3];
	FAUSTFLOAT fHslider11;
	FAUSTFLOAT fHslider12;
	FAUSTFLOAT fHslider13;
	FAUSTFLOAT fHslider14;
	float fRec30[2];
	float fRec29[2];
	float fRec28[2];
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
	float fRec42[2];
	float fRec44[2];
	float fRec43[2];
	FAUSTFLOAT fHslider15;
	FAUSTFLOAT fHslider16;
	float fConst41;
	float fConst42;
	FAUSTFLOAT fHslider17;
	float fRec50[2];
	float fRec49[2];
	float fRec48[2];
	float fRec52[2];
	float fRec51[2];
	float fRec53[2];
	float fRec55[2];
	float fRec54[2];
	float fRec56[2];
	float fRec58[2];
	float fRec57[2];
	float fRec59[2];
	float fRec61[2];
	float fRec60[2];
	float fRec62[2];
	float fRec64[2];
	float fRec63[2];
	int iRec67[2];
	float fRec65[2];
	float fRec47[2];
	float fConst43;
	float fRec69[2];
	float fVec9[2];
	float fRec70[2];
	float fRec68[2];
	float fConst44;
	float fRec71[2];
	float fConst45;
	float fConst46;
	float fConst47;
	float fRec46[3];
	float fConst48;
	float fVec10[2];
	float fRec45[2];
	FAUSTFLOAT fHslider18;
	float fConst49;
	float fConst50;
	float fRec73[2];
	float fRec72[2];
	float fRec75[2];
	float fRec74[2];
	float fVec11[2];
	float fVec12[1024];
	float fRec8[2];
	float fConst51;
	float fRec4[2];
	float fRec3[2];
	FAUSTFLOAT fHslider19;
	float fRec76[2];
	float fConst52;
	float fRec77[2];
	float fConst53;
	float fConst54;
	int iRec78[2];
	float fVec13[2];
	float fRec79[2];
	float fRec2[3];
	FAUSTFLOAT fHslider20;
	float fRec1[3];
	float fRec0[3];
	FAUSTFLOAT fHslider21;
	float fRec80[2];
	float fRec82[2];
	int iVec14[2];
	int iRec81[2];
	float fConst55;
	int iRec83[2];
	FAUSTFLOAT fHslider22;
	
 public:
	UrchinSnare() {
	}
	
	UrchinSnare(const UrchinSnare&) = default;
	
	virtual ~UrchinSnare() = default;
	
	UrchinSnare& operator=(const UrchinSnare&) = default;
	
	void metadata(Meta* m) { 
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/downSample:author", "Romain Michon");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/sAndH:author", "Romain Michon");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 0 -cn UrchinSnare -dtl 65536 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
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
		m->declare("filename", "snare.dsp");
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
		UrchinSnareSIG0* sig0 = newUrchinSnareSIG0();
		sig0->instanceInitUrchinSnareSIG0(sample_rate);
		sig0->fillUrchinSnareSIG0(65536, ftbl0UrchinSnareSIG0);
		deleteUrchinSnareSIG0(sig0);
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
		fConst11 = 0.0029411765f * fConst0;
		fConst12 = std::min<float>(4.8e+04f, fConst0);
		fConst13 = fConst0 / std::min<float>(fConst12, fConst0);
		iConst14 = static_cast<int>(fConst13);
		fConst15 = 3.1415927f / fConst0;
		fConst16 = 0.475f * fConst0;
		fConst17 = 0.02f * fConst0;
		fConst18 = 0.001f * fConst0;
		fConst19 = 0.003f * fConst0;
		fConst20 = 0.005f * fConst0;
		fConst21 = 1.0f / fConst0;
		fConst22 = UrchinSnare_faustpower2_f(fConst21);
		fConst23 = 0.31622776f * fConst22;
		fConst24 = 2.0f * fConst0;
		fConst25 = 4806.6367f / fConst0;
		fConst26 = 0.5f / fConst0;
		fConst27 = 4.0f * UrchinSnare_faustpower2_f(fConst0);
		fConst28 = 3337.9421f / fConst0;
		fConst29 = UrchinSnare_faustpower4_f(fConst21);
		fConst30 = 4.0f * fConst29;
		fConst31 = UrchinSnare_faustpower3_f(fConst21);
		fConst32 = 5.656854f * fConst31;
		fConst33 = 22.627417f / fConst0;
		fConst34 = 6.0f * fConst29;
		fConst35 = 11.313708f / fConst0;
		fConst36 = 2.828427f * fConst31;
		fConst37 = 9613.273f / fConst0;
		fConst38 = 6675.8843f / fConst0;
		fConst39 = std::exp(-(1e+03f / fConst0));
		fConst40 = 1.0f - fConst39;
		fConst41 = std::exp(-(5e+01f / fConst0));
		fConst42 = 1.0f - fConst41;
		fConst43 = 5e+03f / fConst0;
		fConst44 = std::round(fConst0 / fConst12);
		fConst45 = 7539.8223f / fConst0;
		fConst46 = 3769.9111f / fConst0;
		fConst47 = 15079.645f / fConst0;
		fConst48 = 5974.9067f / fConst0;
		fConst49 = 4.4e+02f / fConst0;
		fConst50 = 117.123f / fConst0;
		fConst51 = 1.0f - fConst7;
		fConst52 = 0.00015f * fConst0;
		fConst53 = std::max<float>(1.0f, fConst52);
		fConst54 = 1.0f / fConst53;
		fConst55 = 1.0f / std::max<float>(1.0f, fConst18);
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
		fHslider10 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider11 = static_cast<FAUSTFLOAT>(1.2e+02f);
		fHslider12 = static_cast<FAUSTFLOAT>(8.0f);
		fHslider13 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider14 = static_cast<FAUSTFLOAT>(5e+01f);
		fHslider15 = static_cast<FAUSTFLOAT>(1.0f);
		fHslider16 = static_cast<FAUSTFLOAT>(1e+02f);
		fHslider17 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider18 = static_cast<FAUSTFLOAT>(35.0f);
		fHslider19 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider20 = static_cast<FAUSTFLOAT>(2e+01f);
		fHslider21 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider22 = static_cast<FAUSTFLOAT>(5e+01f);
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
		IOTA0 = 0;
		for (int l6 = 0; l6 < 1024; l6 = faust_wrap_add(l6, 1)) {
			fVec2[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec7[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = faust_wrap_add(l8, 1)) {
			iRec15[l8] = 0;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			iRec16[l9] = 0;
		}
		for (int l10 = 0; l10 < 2; l10 = faust_wrap_add(l10, 1)) {
			fVec3[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec14[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = faust_wrap_add(l12, 1)) {
			fRec17[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec13[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 3; l14 = faust_wrap_add(l14, 1)) {
			fRec12[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 3; l15 = faust_wrap_add(l15, 1)) {
			fRec11[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 4096; l16 = faust_wrap_add(l16, 1)) {
			fVec4[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fVec5[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = faust_wrap_add(l18, 1)) {
			iVec6[l18] = 0;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			iVec7[l19] = 0;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			iRec18[l20] = 0;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			fRec19[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = faust_wrap_add(l22, 1)) {
			fRec21[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 5; l23 = faust_wrap_add(l23, 1)) {
			fRec20[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 5; l24 = faust_wrap_add(l24, 1)) {
			fRec22[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec24[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2; l26 = faust_wrap_add(l26, 1)) {
			fRec23[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 3; l27 = faust_wrap_add(l27, 1)) {
			fRec26[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 3; l28 = faust_wrap_add(l28, 1)) {
			fRec25[l28] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			fRec30[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2; l32 = faust_wrap_add(l32, 1)) {
			fRec29[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			fRec28[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 2; l34 = faust_wrap_add(l34, 1)) {
			fRec32[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = faust_wrap_add(l35, 1)) {
			fRec31[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = faust_wrap_add(l36, 1)) {
			fRec33[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec35[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = faust_wrap_add(l38, 1)) {
			fRec34[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			fRec36[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = faust_wrap_add(l40, 1)) {
			fRec38[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = faust_wrap_add(l41, 1)) {
			fRec37[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 2; l42 = faust_wrap_add(l42, 1)) {
			fRec39[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec41[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 2; l44 = faust_wrap_add(l44, 1)) {
			fRec40[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 2; l45 = faust_wrap_add(l45, 1)) {
			fRec42[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 2; l46 = faust_wrap_add(l46, 1)) {
			fRec44[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 2; l47 = faust_wrap_add(l47, 1)) {
			fRec43[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 2; l48 = faust_wrap_add(l48, 1)) {
			fRec50[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 2; l49 = faust_wrap_add(l49, 1)) {
			fRec49[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 2; l50 = faust_wrap_add(l50, 1)) {
			fRec48[l50] = 0.0f;
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
			fRec59[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 2; l60 = faust_wrap_add(l60, 1)) {
			fRec61[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = faust_wrap_add(l61, 1)) {
			fRec60[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 2; l62 = faust_wrap_add(l62, 1)) {
			fRec62[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 2; l63 = faust_wrap_add(l63, 1)) {
			fRec64[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 2; l64 = faust_wrap_add(l64, 1)) {
			fRec63[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 2; l65 = faust_wrap_add(l65, 1)) {
			iRec67[l65] = 0;
		}
		for (int l66 = 0; l66 < 2; l66 = faust_wrap_add(l66, 1)) {
			fRec65[l66] = 0.0f;
		}
		for (int l67 = 0; l67 < 2; l67 = faust_wrap_add(l67, 1)) {
			fRec47[l67] = 0.0f;
		}
		for (int l68 = 0; l68 < 2; l68 = faust_wrap_add(l68, 1)) {
			fRec69[l68] = 0.0f;
		}
		for (int l69 = 0; l69 < 2; l69 = faust_wrap_add(l69, 1)) {
			fVec9[l69] = 0.0f;
		}
		for (int l70 = 0; l70 < 2; l70 = faust_wrap_add(l70, 1)) {
			fRec70[l70] = 0.0f;
		}
		for (int l71 = 0; l71 < 2; l71 = faust_wrap_add(l71, 1)) {
			fRec68[l71] = 0.0f;
		}
		for (int l72 = 0; l72 < 2; l72 = faust_wrap_add(l72, 1)) {
			fRec71[l72] = 0.0f;
		}
		for (int l73 = 0; l73 < 3; l73 = faust_wrap_add(l73, 1)) {
			fRec46[l73] = 0.0f;
		}
		for (int l74 = 0; l74 < 2; l74 = faust_wrap_add(l74, 1)) {
			fVec10[l74] = 0.0f;
		}
		for (int l75 = 0; l75 < 2; l75 = faust_wrap_add(l75, 1)) {
			fRec45[l75] = 0.0f;
		}
		for (int l76 = 0; l76 < 2; l76 = faust_wrap_add(l76, 1)) {
			fRec73[l76] = 0.0f;
		}
		for (int l77 = 0; l77 < 2; l77 = faust_wrap_add(l77, 1)) {
			fRec72[l77] = 0.0f;
		}
		for (int l78 = 0; l78 < 2; l78 = faust_wrap_add(l78, 1)) {
			fRec75[l78] = 0.0f;
		}
		for (int l79 = 0; l79 < 2; l79 = faust_wrap_add(l79, 1)) {
			fRec74[l79] = 0.0f;
		}
		for (int l80 = 0; l80 < 2; l80 = faust_wrap_add(l80, 1)) {
			fVec11[l80] = 0.0f;
		}
		for (int l81 = 0; l81 < 1024; l81 = faust_wrap_add(l81, 1)) {
			fVec12[l81] = 0.0f;
		}
		for (int l82 = 0; l82 < 2; l82 = faust_wrap_add(l82, 1)) {
			fRec8[l82] = 0.0f;
		}
		for (int l83 = 0; l83 < 2; l83 = faust_wrap_add(l83, 1)) {
			fRec4[l83] = 0.0f;
		}
		for (int l84 = 0; l84 < 2; l84 = faust_wrap_add(l84, 1)) {
			fRec3[l84] = 0.0f;
		}
		for (int l85 = 0; l85 < 2; l85 = faust_wrap_add(l85, 1)) {
			fRec76[l85] = 0.0f;
		}
		for (int l86 = 0; l86 < 2; l86 = faust_wrap_add(l86, 1)) {
			fRec77[l86] = 0.0f;
		}
		for (int l87 = 0; l87 < 2; l87 = faust_wrap_add(l87, 1)) {
			iRec78[l87] = 0;
		}
		for (int l88 = 0; l88 < 2; l88 = faust_wrap_add(l88, 1)) {
			fVec13[l88] = 0.0f;
		}
		for (int l89 = 0; l89 < 2; l89 = faust_wrap_add(l89, 1)) {
			fRec79[l89] = 0.0f;
		}
		for (int l90 = 0; l90 < 3; l90 = faust_wrap_add(l90, 1)) {
			fRec2[l90] = 0.0f;
		}
		for (int l91 = 0; l91 < 3; l91 = faust_wrap_add(l91, 1)) {
			fRec1[l91] = 0.0f;
		}
		for (int l92 = 0; l92 < 3; l92 = faust_wrap_add(l92, 1)) {
			fRec0[l92] = 0.0f;
		}
		for (int l93 = 0; l93 < 2; l93 = faust_wrap_add(l93, 1)) {
			fRec80[l93] = 0.0f;
		}
		for (int l94 = 0; l94 < 2; l94 = faust_wrap_add(l94, 1)) {
			fRec82[l94] = 0.0f;
		}
		for (int l95 = 0; l95 < 2; l95 = faust_wrap_add(l95, 1)) {
			iVec14[l95] = 0;
		}
		for (int l96 = 0; l96 < 2; l96 = faust_wrap_add(l96, 1)) {
			iRec81[l96] = 0;
		}
		for (int l97 = 0; l97 < 2; l97 = faust_wrap_add(l97, 1)) {
			iRec83[l97] = 0;
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
	
	virtual UrchinSnare* clone() {
		return new UrchinSnare(*this);
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
		ui_interface->declare(&fHslider10, "050", "");
		ui_interface->addHorizontalSlider("Strike_Rimshot", &fHslider10, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider11, "070", "");
		ui_interface->declare(&fHslider11, "export", "Transpose");
		ui_interface->declare(&fHslider11, "unit", "Hz");
		ui_interface->addHorizontalSlider("Tuning", &fHslider11, FAUSTFLOAT(1.2e+02f), FAUSTFLOAT(4e+01f), FAUSTFLOAT(2.4e+02f), FAUSTFLOAT(0.01f));
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
		ui_interface->declare(&fHslider22, "1050", "");
		ui_interface->declare(&fHslider22, "export", "Reverb Send");
		ui_interface->declare(&fHslider22, "unit", "%");
		ui_interface->addHorizontalSlider("Voice_Reverb", &fHslider22, FAUSTFLOAT(5e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider19, "1060", "");
		ui_interface->declare(&fHslider19, "export", "Voice Punchiness");
		ui_interface->declare(&fHslider19, "unit", "%");
		ui_interface->addHorizontalSlider("Voice_Punchiness", &fHslider19, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider20, "1070", "");
		ui_interface->declare(&fHslider20, "export", "Cutoff");
		ui_interface->declare(&fHslider20, "unit", "kHz");
		ui_interface->addHorizontalSlider("Sample_Cutoff", &fHslider20, FAUSTFLOAT(2e+01f), FAUSTFLOAT(2.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.01f));
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
		ui_interface->declare(&fHslider21, "1160", "");
		ui_interface->declare(&fHslider21, "export", "Sample Length");
		ui_interface->declare(&fHslider21, "minlabel", "∞");
		ui_interface->declare(&fHslider21, "unit", "s");
		ui_interface->addHorizontalSlider("Sample_Length", &fHslider21, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(3.0f), FAUSTFLOAT(0.01f));
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
		ui_interface->declare(&fHslider17, "120", "");
		ui_interface->declare(&fHslider17, "export", "Reso Tuning");
		ui_interface->declare(&fHslider17, "unit", "st");
		ui_interface->addHorizontalSlider("Reso_Tuning", &fHslider17, FAUSTFLOAT(0.0f), FAUSTFLOAT(-6.0f), FAUSTFLOAT(6.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider13, "130", "");
		ui_interface->declare(&fHslider13, "export", "Decay");
		ui_interface->declare(&fHslider13, "unit", "s");
		ui_interface->addHorizontalSlider("Decay", &fHslider13, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.15f), FAUSTFLOAT(5.0f), FAUSTFLOAT(0.0001f));
		ui_interface->declare(&fHslider14, "140", "");
		ui_interface->declare(&fHslider14, "export", "Beater Damp");
		ui_interface->declare(&fHslider14, "unit", "%");
		ui_interface->addHorizontalSlider("Beater_Dampening", &fHslider14, FAUSTFLOAT(5e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.0001f));
		ui_interface->declare(&fHslider15, "160", "");
		ui_interface->declare(&fHslider15, "export", "Reso Head");
		ui_interface->declare(&fHslider15, "type", "bool");
		ui_interface->addHorizontalSlider("Reso_Head", &fHslider15, FAUSTFLOAT(1.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->declare(&fHslider18, "200", "");
		ui_interface->declare(&fHslider18, "export", "Mix Ring");
		ui_interface->declare(&fHslider18, "unit", "%");
		ui_interface->addHorizontalSlider("Mix_Ring", &fHslider18, FAUSTFLOAT(35.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider16, "210", "");
		ui_interface->declare(&fHslider16, "export", "Mix Reso");
		ui_interface->declare(&fHslider16, "unit", "%");
		ui_interface->addHorizontalSlider("Mix_Reso", &fHslider16, FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider12, "250", "");
		ui_interface->declare(&fHslider12, "export", "Detune Range");
		ui_interface->declare(&fHslider12, "unit", "st");
		ui_interface->addHorizontalSlider("Detune_Range", &fHslider12, FAUSTFLOAT(8.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(32.0f), FAUSTFLOAT(0.001f));
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
		float fSlow13 = 2.0f * (1.0f - 1.0f / UrchinSnare_faustpower2_f(fSlow4));
		float fSlow14 = static_cast<float>(fHslider4);
		float fSlow15 = fConst9 * fSlow14;
		float fSlow16 = static_cast<float>(fEntry1) + static_cast<float>(fHslider5) + 12.0f * static_cast<float>(fEntry2);
		float fSlow17 = static_cast<float>(fButton0);
		float fSlow18 = 1.0f - 0.009999f * static_cast<float>(fHslider6);
		float fSlow19 = static_cast<float>(fHslider7);
		float fSlow20 = static_cast<float>(fHslider8);
		float fSlow21 = std::pow(1e+01f, 0.05f * (-12.0f - 0.09f * fSlow20));
		float fSlow22 = 4.4e+02f * std::pow(2.0f, 0.083333336f * (0.3101955f * fSlow20 + -41.51318f));
		float fSlow23 = 4.4e+02f * std::pow(2.0f, 0.083333336f * (0.5186314f * fSlow20 + 2.213095f));
		float fSlow24 = static_cast<float>(fEntry3);
		int iSlow25 = static_cast<int>(std::min<float>(fConst17, std::max<float>(0.0f, fConst18 * static_cast<float>(fHslider9))));
		float fSlow26 = static_cast<float>(fHslider10);
		float fSlow27 = fConst23 * fSlow26;
		float fSlow28 = 0.0067f * fSlow20 + 0.33f;
		float fSlow29 = fSlow14 + -5.0f;
		float fSlow30 = fConst15 * (3e+03f - 105.26316f * fSlow29);
		float fSlow31 = fConst15 * (4.4e+03f - 152.63158f * fSlow29);
		float fSlow32 = static_cast<float>(fHslider11);
		float fSlow33 = 0.083333336f * static_cast<float>(fHslider12);
		float fSlow34 = static_cast<float>(fHslider13);
		float fSlow35 = 0.25f * fSlow34 * (1.0f - 0.008f * static_cast<float>(fHslider14));
		float fSlow36 = static_cast<float>(fHslider16);
		float fSlow37 = static_cast<float>(fHslider15) * fSlow36;
		float fSlow38 = 0.01f * fSlow36;
		float fSlow39 = fSlow32 * std::pow(2.0f, 0.083333336f * static_cast<float>(fHslider17));
		float fSlow40 = 0.25f * fSlow34;
		float fSlow41 = 0.25f * fSlow34 * fSlow32;
		float fSlow42 = 0.002f * (1.0f - 0.005f * fSlow36);
		float fSlow43 = 0.01f * static_cast<float>(fHslider18) * std::pow(1e+01f, -(0.031578947f * fSlow29)) * std::pow(1e+01f, 0.05f * (9.0f * fSlow26 + -34.0f));
		float fSlow44 = fConst49 * std::pow(2.0f, 0.083333336f * (8.283786f - 0.31951007f * fSlow29));
		float fSlow45 = 0.125f * fSlow34;
		float fSlow46 = 0.01f * static_cast<float>(fHslider19);
		float fSlow47 = (fSlow5 - fSlow11) / fSlow4 + 1.0f;
		float fSlow48 = ((iSlow7) ? fSlow9 : fSlow10);
		float fSlow49 = (fSlow5 + fSlow48) / fSlow4 + 1.0f;
		float fSlow50 = (fSlow5 - fSlow48) / fSlow4 + 1.0f;
		float fSlow51 = 1e+03f * static_cast<float>(fHslider20);
		float fSlow52 = static_cast<float>(fHslider21);
		float fSlow53 = static_cast<float>(fSlow52 > 0.0f);
		float fSlow54 = fConst0 * fSlow52;
		float fSlow55 = 0.5f * fSlow0 * std::min<float>(1.0f, fSlow1 + 1.0f);
		float fSlow56 = 0.005f * static_cast<float>(fHslider22) * fSlow0;
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			iVec0[0] = 1;
			fRec5[0] = 0.08f * fRec7[1] + 0.92f * fRec5[1];
			fRec6[0] = 0.08f * fRec8[1] + 0.92f * fRec6[1];
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
			float fTemp10 = fRec6[0] * fTemp9;
			fVec2[IOTA0 & 1023] = fTemp10;
			int iTemp11 = static_cast<int>(fTemp4);
			int iTemp12 = static_cast<int>(std::min<float>(fConst11, static_cast<float>(std::max<int>(0, iTemp11))));
			float fTemp13 = -1.0f - fTemp5 + fTemp3;
			float fTemp14 = fTemp3 - fTemp5;
			int iTemp15 = static_cast<int>(std::min<float>(fConst11, static_cast<float>(std::max<int>(0, faust_wrap_add(1, iTemp11)))));
			float fTemp16 = fTemp14 * fTemp13;
			int iTemp17 = static_cast<int>(std::min<float>(fConst11, static_cast<float>(std::max<int>(0, faust_wrap_add(2, iTemp11)))));
			float fTemp18 = fTemp16 * fTemp8;
			int iTemp19 = static_cast<int>(std::min<float>(fConst11, static_cast<float>(std::max<int>(0, faust_wrap_add(3, iTemp11)))));
			float fTemp20 = fTemp18 * fTemp7;
			int iTemp21 = static_cast<int>(std::min<float>(fConst11, static_cast<float>(std::max<int>(0, faust_wrap_add(4, iTemp11)))));
			fRec7[0] = fTemp6 * (fTemp7 * (fTemp8 * (0.041666668f * fVec2[(faust_wrap_sub(IOTA0, iTemp12)) & 1023] * fTemp13 - 0.16666667f * fTemp14 * fVec2[(faust_wrap_sub(IOTA0, iTemp15)) & 1023]) + 0.25f * fTemp16 * fVec2[(faust_wrap_sub(IOTA0, iTemp17)) & 1023]) - 0.16666667f * fTemp18 * fVec2[(faust_wrap_sub(IOTA0, iTemp19)) & 1023]) + 0.041666668f * fTemp20 * fVec2[(faust_wrap_sub(IOTA0, iTemp21)) & 1023];
			iRec15[0] = faust_wrap_add(1, iRec15[1]);
			iRec16[0] = faust_wrap_add(12345, faust_wrap_mul(1103515245, iRec16[1]));
			float fTemp22 = static_cast<float>(iRec16[0]);
			fVec3[0] = fTemp22;
			fRec14[0] = (((iRec15[1] % iConst14) == 0) ? 4.656613e-10f * fTemp22 : fRec14[1]);
			float fTemp23 = ((fRec14[0] != fRec14[1]) ? fConst13 : -1.0f + fRec17[1]);
			fRec17[0] = fTemp23;
			fRec13[0] = ((fTemp23 > 0.0f) ? fRec13[1] + (fRec14[0] - fRec13[1]) / fTemp23 : fRec14[0]);
			float fTemp24 = std::tan(fConst15 * std::max<float>(4e+01f, fSlow22 * fTemp2));
			float fTemp25 = 1.0f / fTemp24;
			float fTemp26 = UrchinSnare_faustpower2_f(fTemp24);
			float fTemp27 = 1.0f + (1.4142135f + fTemp25) / fTemp24;
			fRec12[0] = fRec13[0] - (fRec12[2] * (1.0f + (-1.4142135f + fTemp25) / fTemp24) + 2.0f * fRec12[1] * (1.0f - 1.0f / fTemp26)) / fTemp27;
			float fTemp28 = std::tan(fConst15 * std::min<float>(fConst16, fSlow23 * fTemp2));
			float fTemp29 = 1.0f / fTemp28;
			float fTemp30 = 1.0f + (1.4142135f + fTemp29) / fTemp28;
			fRec11[0] = (fRec12[2] + (fRec12[0] - 2.0f * fRec12[1])) / (fTemp26 * fTemp27) - (fRec11[2] * (1.0f + (-1.4142135f + fTemp29) / fTemp28) + 2.0f * fRec11[1] * (1.0f - 1.0f / UrchinSnare_faustpower2_f(fTemp28))) / fTemp30;
			fVec4[IOTA0 & 4095] = fSlow24;
			float fTemp31 = fVec4[(faust_wrap_sub(IOTA0, iSlow25)) & 4095];
			fVec5[0] = fTemp31;
			int iTemp32 = fTemp31 > fVec5[1];
			iVec6[0] = iTemp32;
			int iTemp33 = iTemp32 > iVec6[1];
			iVec7[0] = iTemp33;
			iRec18[0] = faust_wrap_add(iTemp33, faust_wrap_mul(faust_wrap_add(iRec18[1], iRec18[1] > 0), iTemp32 <= iVec6[1]));
			float fTemp34 = static_cast<float>(iRec18[0]);
			float fTemp35 = std::max<float>(1.0f, fConst19 / fTemp2);
			fRec19[0] = ((iTemp32) ? 0.99f * fTemp31 + 0.01f : fRec19[1]);
			float fTemp36 = std::tan(fConst25 * fTemp2);
			float fTemp37 = std::sqrt(fConst27 * std::tan(fConst28 * fTemp2) * fTemp36);
			float fTemp38 = UrchinSnare_faustpower2_f(fTemp37);
			float fTemp39 = fConst24 * fTemp36 - fConst26 * (fTemp38 / fTemp36);
			float fTemp40 = UrchinSnare_faustpower2_f(fTemp39);
			int iTemp41 = iTemp32 > 0;
			float fTemp42 = 0.1447178f * ((iTemp41) ? 0.0f : 0.12f / fTemp2);
			int iTemp43 = std::fabs(fTemp42) < 1.1920929e-07f;
			float fTemp44 = ((iTemp43) ? 0.0f : std::exp(-(fConst21 / ((iTemp43) ? 1.0f : fTemp42))));
			fRec21[0] = (1.0f - fTemp44) * ((iTemp41) ? static_cast<float>(iTemp41) : 0.0f) + fTemp44 * fRec21[1];
			float fTemp45 = fRec13[0] * fRec19[0] * fRec21[0];
			float fTemp46 = UrchinSnare_faustpower4_f(fTemp37);
			float fTemp47 = fConst30 * fTemp46;
			float fTemp48 = fConst32 * fTemp38;
			float fTemp49 = fTemp39 * (fConst35 + fConst36 * fTemp38);
			float fTemp50 = fConst29 * fTemp46 + fConst22 * (4.0f * fTemp40 + 8.0f * fTemp38);
			float fTemp51 = 16.0f + fTemp49 + fTemp50;
			fRec20[0] = fTemp45 - (fRec20[1] * (-64.0f + fTemp47 + fTemp39 * (fTemp48 - fConst33)) + fRec20[2] * (96.0f - fConst22 * (8.0f * fTemp40 + 16.0f * fTemp38) + fConst34 * fTemp46) + fRec20[3] * (-64.0f + fTemp47 + fTemp39 * (fConst33 - fTemp48)) + fRec20[4] * (16.0f - fTemp49 + fTemp50)) / fTemp51;
			float fTemp52 = std::tan(fConst37 * fTemp2);
			float fTemp53 = std::sqrt(fConst27 * std::tan(fConst38 * fTemp2) * fTemp52);
			float fTemp54 = UrchinSnare_faustpower2_f(fTemp53);
			float fTemp55 = fConst24 * fTemp52 - fConst26 * (fTemp54 / fTemp52);
			float fTemp56 = UrchinSnare_faustpower2_f(fTemp55);
			float fTemp57 = UrchinSnare_faustpower4_f(fTemp53);
			float fTemp58 = fConst30 * fTemp57;
			float fTemp59 = fConst32 * fTemp54;
			float fTemp60 = fTemp55 * (fConst35 + fConst36 * fTemp54);
			float fTemp61 = fConst29 * fTemp57 + fConst22 * (4.0f * fTemp56 + 8.0f * fTemp54);
			float fTemp62 = 16.0f + fTemp60 + fTemp61;
			fRec22[0] = fTemp45 - (fRec22[1] * (-64.0f + fTemp58 + fTemp55 * (fTemp59 - fConst33)) + fRec22[2] * (96.0f - fConst22 * (8.0f * fTemp56 + 16.0f * fTemp54) + fConst34 * fTemp57) + fRec22[3] * (-64.0f + fTemp58 + fTemp55 * (fConst33 - fTemp59)) + fRec22[4] * (16.0f - fTemp60 + fTemp61)) / fTemp62;
			float fTemp63 = static_cast<float>(iTemp33);
			float fTemp64 = static_cast<float>(faust_wrap_sub(1, iTemp33));
			float fTemp65 = 0.0375f * fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec24[1]) - 1.0f));
			int iTemp66 = std::fabs(fTemp65) < 1.1920929e-07f;
			float fTemp67 = ((iTemp66) ? 0.0f : std::exp(-(fConst21 / ((iTemp66) ? 1.0f : fTemp65))));
			float fTemp68 = 0.5f * fRec19[0];
			fRec24[0] = fTemp63 * (1.0f - fTemp67) * std::min<float>(1.0f, std::max<float>(fTemp68 + fRec24[1], fRec19[0])) + fRec24[1] * fTemp67;
			fRec23[0] = fConst40 * fRec24[0] + fConst39 * fRec23[1];
			float fTemp69 = std::tan(fSlow30 * fTemp2);
			float fTemp70 = 1.0f / fTemp69;
			float fTemp71 = UrchinSnare_faustpower2_f(fTemp69);
			float fTemp72 = 1.0f + (1.4142135f + fTemp70) / fTemp69;
			fRec26[0] = fSlow28 * fRec13[0] * (0.06f + 0.29f * UrchinSnare_faustpower2_f(fRec19[0])) - (fRec26[2] * (1.0f + (-1.4142135f + fTemp70) / fTemp69) + 2.0f * fRec26[1] * (1.0f - 1.0f / fTemp71)) / fTemp72;
			float fTemp73 = std::tan(fSlow31 * fTemp2 * (0.8f + 0.3f * fRec19[0]) * (0.8f + 0.2f * fRec23[0]));
			float fTemp74 = 1.0f / fTemp73;
			float fTemp75 = 1.0f + (1.4142135f + fTemp74) / fTemp73;
			fRec25[0] = (fRec26[2] + (fRec26[0] - 2.0f * fRec26[1])) / (fTemp71 * fTemp72) - (fRec25[2] * (1.0f + (-1.4142135f + fTemp74) / fTemp73) + 2.0f * fRec25[1] * (1.0f - 1.0f / UrchinSnare_faustpower2_f(fTemp73))) / fTemp75;
			int iTemp76 = faust_wrap_sub(1, iVec0[1]);
			float fTemp77 = fSlow35 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec30[1]) - 1.0f)) / fTemp2);
			int iTemp78 = std::fabs(fTemp77) < 1.1920929e-07f;
			float fTemp79 = ((iTemp78) ? 0.0f : std::exp(-(fConst21 / ((iTemp78) ? 1.0f : fTemp77))));
			fRec30[0] = (1.0f - fTemp79) * std::min<float>(1.0f, std::max<float>(fRec30[1] + fTemp68, fRec19[0])) * fTemp63 + fRec30[1] * fTemp79;
			fRec29[0] = fConst40 * fRec30[0] + fConst39 * fRec29[1];
			float fTemp80 = std::max<float>(4e+01f, fSlow32 * fTemp2 * std::pow(2.0f, fSlow33 * std::max<float>(0.0f, 1.1764706f * (-0.15f + fRec29[0]))));
			float fTemp81 = -2e+01f + fTemp80;
			float fTemp82 = 2e+01f + 2.0f * fTemp81;
			float fTemp83 = ((iTemp76) ? 0.0f : fRec28[1] + fConst21 * fTemp82);
			fRec28[0] = fTemp83 - std::floor(fTemp83);
			float fTemp84 = fTemp64 * fTemp80;
			float fTemp85 = fSlow35 * (fTemp84 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec32[1]) - 1.0f)) / (fTemp2 * fTemp82));
			int iTemp86 = std::fabs(fTemp85) < 1.1920929e-07f;
			float fTemp87 = ((iTemp86) ? 0.0f : std::exp(-(fConst21 / ((iTemp86) ? 1.0f : fTemp85))));
			fRec32[0] = fTemp63 * (1.0f - fTemp87) * std::min<float>(1.0f, std::max<float>(fTemp68 + fRec32[1], fRec19[0])) + fRec32[1] * fTemp87;
			fRec31[0] = fConst40 * fRec32[0] + fConst39 * fRec31[1];
			float fTemp88 = 2e+01f + 4.0f * fTemp81;
			float fTemp89 = ((iTemp76) ? 0.0f : fRec33[1] + fConst21 * fTemp88);
			fRec33[0] = fTemp89 - std::floor(fTemp89);
			float fTemp90 = fSlow35 * (fTemp84 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec35[1]) - 1.0f)) / (fTemp2 * fTemp88));
			int iTemp91 = std::fabs(fTemp90) < 1.1920929e-07f;
			float fTemp92 = ((iTemp91) ? 0.0f : std::exp(-(fConst21 / ((iTemp91) ? 1.0f : fTemp90))));
			fRec35[0] = fTemp63 * (1.0f - fTemp92) * std::min<float>(1.0f, std::max<float>(fTemp68 + fRec35[1], fRec19[0])) + fRec35[1] * fTemp92;
			fRec34[0] = fConst40 * fRec35[0] + fConst39 * fRec34[1];
			float fTemp93 = ((iTemp76) ? 0.0f : fRec36[1] + fConst21 * fTemp80);
			fRec36[0] = fTemp93 - std::floor(fTemp93);
			float fTemp94 = fSlow35 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec38[1]) - 1.0f)) / fTemp2);
			int iTemp95 = std::fabs(fTemp94) < 1.1920929e-07f;
			float fTemp96 = ((iTemp95) ? 0.0f : std::exp(-(fConst21 / ((iTemp95) ? 1.0f : fTemp94))));
			fRec38[0] = fTemp63 * (1.0f - fTemp96) * std::min<float>(1.0f, std::max<float>(fTemp68 + fRec38[1], fRec19[0])) + fRec38[1] * fTemp96;
			fRec37[0] = fConst40 * fRec38[0] + fConst39 * fRec37[1];
			float fTemp97 = 2e+01f + 3.0f * fTemp81;
			float fTemp98 = ((iTemp76) ? 0.0f : fRec39[1] + fConst21 * fTemp97);
			fRec39[0] = fTemp98 - std::floor(fTemp98);
			float fTemp99 = fSlow35 * (fTemp84 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec41[1]) - 1.0f)) / (fTemp2 * fTemp97));
			int iTemp100 = std::fabs(fTemp99) < 1.1920929e-07f;
			float fTemp101 = ((iTemp100) ? 0.0f : std::exp(-(fConst21 / ((iTemp100) ? 1.0f : fTemp99))));
			fRec41[0] = fTemp63 * (1.0f - fTemp101) * std::min<float>(1.0f, std::max<float>(fTemp68 + fRec41[1], fRec19[0])) + fRec41[1] * fTemp101;
			fRec40[0] = fConst40 * fRec41[0] + fConst39 * fRec40[1];
			float fTemp102 = 2e+01f + 5.0f * fTemp81;
			float fTemp103 = ((iTemp76) ? 0.0f : fRec42[1] + fConst21 * fTemp102);
			fRec42[0] = fTemp103 - std::floor(fTemp103);
			float fTemp104 = fSlow35 * (fTemp84 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec44[1]) - 1.0f)) / (fTemp2 * fTemp102));
			int iTemp105 = std::fabs(fTemp104) < 1.1920929e-07f;
			float fTemp106 = ((iTemp105) ? 0.0f : std::exp(-(fConst21 / ((iTemp105) ? 1.0f : fTemp104))));
			fRec44[0] = fTemp63 * (1.0f - fTemp106) * std::min<float>(1.0f, std::max<float>(fTemp68 + fRec44[1], fRec19[0])) + fRec44[1] * fTemp106;
			fRec43[0] = fConst40 * fRec44[0] + fConst39 * fRec43[1];
			float fTemp107 = 1.0f / std::tan(fConst15 * std::min<float>(fConst16, 7.5e+03f * fTemp2));
			float fTemp108 = fSlow40 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec50[1]) - 1.0f)) / fTemp2);
			int iTemp109 = std::fabs(fTemp108) < 1.1920929e-07f;
			float fTemp110 = ((iTemp109) ? 0.0f : std::exp(-(fConst21 / ((iTemp109) ? 1.0f : fTemp108))));
			fRec50[0] = fTemp63 * (1.0f - fTemp110) * std::min<float>(1.0f, std::max<float>(fTemp68 + fRec50[1], fRec19[0])) + fRec50[1] * fTemp110;
			fRec49[0] = fConst40 * fRec50[0] + fConst39 * fRec49[1];
			float fTemp111 = std::max<float>(4e+01f, fSlow39 * fTemp2 * std::pow(2.0f, fSlow33 * std::max<float>(0.0f, 1.1764706f * (-0.15f + fRec49[0]))));
			float fTemp112 = -2e+01f + fTemp111;
			float fTemp113 = 2e+01f + 2.0f * fTemp112;
			float fTemp114 = ((iTemp76) ? 0.0f : fRec48[1] + fConst21 * fTemp113);
			fRec48[0] = fTemp114 - std::floor(fTemp114);
			float fTemp115 = fSlow41 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec52[1]) - 1.0f)) / fTemp113);
			int iTemp116 = std::fabs(fTemp115) < 1.1920929e-07f;
			float fTemp117 = ((iTemp116) ? 0.0f : std::exp(-(fConst21 / ((iTemp116) ? 1.0f : fTemp115))));
			float fTemp118 = fRec19[0] * std::pow(1e+01f, -(0.7f * (1.0f - fRec19[0])));
			float fTemp119 = 0.5f * fTemp118;
			fRec52[0] = fTemp63 * (1.0f - fTemp117) * std::min<float>(1.0f, std::max<float>(fTemp119 + fRec52[1], fTemp118)) + fRec52[1] * fTemp117;
			fRec51[0] = fConst40 * fRec52[0] + fConst39 * fRec51[1];
			float fTemp120 = 2e+01f + 4.0f * fTemp112;
			float fTemp121 = ((iTemp76) ? 0.0f : fRec53[1] + fConst21 * fTemp120);
			fRec53[0] = fTemp121 - std::floor(fTemp121);
			float fTemp122 = fSlow41 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec55[1]) - 1.0f)) / fTemp120);
			int iTemp123 = std::fabs(fTemp122) < 1.1920929e-07f;
			float fTemp124 = ((iTemp123) ? 0.0f : std::exp(-(fConst21 / ((iTemp123) ? 1.0f : fTemp122))));
			fRec55[0] = fTemp63 * (1.0f - fTemp124) * std::min<float>(1.0f, std::max<float>(fTemp119 + fRec55[1], fTemp118)) + fRec55[1] * fTemp124;
			fRec54[0] = fConst40 * fRec55[0] + fConst39 * fRec54[1];
			float fTemp125 = ((iTemp76) ? 0.0f : fRec56[1] + fConst21 * fTemp111);
			fRec56[0] = fTemp125 - std::floor(fTemp125);
			float fTemp126 = fSlow41 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec58[1]) - 1.0f)) / fTemp111);
			int iTemp127 = std::fabs(fTemp126) < 1.1920929e-07f;
			float fTemp128 = ((iTemp127) ? 0.0f : std::exp(-(fConst21 / ((iTemp127) ? 1.0f : fTemp126))));
			fRec58[0] = fTemp63 * (1.0f - fTemp128) * std::min<float>(1.0f, std::max<float>(fRec58[1] + fTemp119, fTemp118)) + fRec58[1] * fTemp128;
			fRec57[0] = fConst40 * fRec58[0] + fConst39 * fRec57[1];
			float fTemp129 = 2e+01f + 3.0f * fTemp112;
			float fTemp130 = ((iTemp76) ? 0.0f : fRec59[1] + fConst21 * fTemp129);
			fRec59[0] = fTemp130 - std::floor(fTemp130);
			float fTemp131 = fSlow41 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec61[1]) - 1.0f)) / fTemp129);
			int iTemp132 = std::fabs(fTemp131) < 1.1920929e-07f;
			float fTemp133 = ((iTemp132) ? 0.0f : std::exp(-(fConst21 / ((iTemp132) ? 1.0f : fTemp131))));
			fRec61[0] = fTemp63 * (1.0f - fTemp133) * std::min<float>(1.0f, std::max<float>(fTemp119 + fRec61[1], fTemp118)) + fRec61[1] * fTemp133;
			fRec60[0] = fConst40 * fRec61[0] + fConst39 * fRec60[1];
			float fTemp134 = 2e+01f + 5.0f * fTemp112;
			float fTemp135 = ((iTemp76) ? 0.0f : fRec62[1] + fConst21 * fTemp134);
			fRec62[0] = fTemp135 - std::floor(fTemp135);
			float fTemp136 = fSlow41 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec64[1]) - 1.0f)) / fTemp134);
			int iTemp137 = std::fabs(fTemp136) < 1.1920929e-07f;
			float fTemp138 = ((iTemp137) ? 0.0f : std::exp(-(fConst21 / ((iTemp137) ? 1.0f : fTemp136))));
			fRec64[0] = fTemp63 * (1.0f - fTemp138) * std::min<float>(1.0f, std::max<float>(fTemp119 + fRec64[1], fTemp118)) + fRec64[1] * fTemp138;
			fRec63[0] = fConst40 * fRec64[0] + fConst39 * fRec63[1];
			float fTemp139 = ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec48[0])] * fRec51[0] * std::pow(fTemp111 / fTemp113, 1.6f) + ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec53[0])] * fRec54[0] * std::pow(fTemp111 / fTemp120, 1.6f) - (ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec56[0])] * fRec57[0] + ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec59[0])] * fRec60[0] * std::pow(fTemp111 / fTemp129, 1.6f) + ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec62[0])] * fRec63[0] * std::pow(fTemp111 / fTemp134, 1.6f));
			float fRec66 = std::fabs(0.2f * fTemp139);
			iRec67[0] = std::max<int>(0, faust_wrap_add(-1, iRec67[1]));
			float fTemp140 = std::fabs(std::max<float>(static_cast<float>(fRec66 > 0.004698941f), static_cast<float>(iRec67[0] > 0)));
			float fTemp141 = ((fTemp140 > fRec65[1]) ? 0.0f : fConst41);
			fRec65[0] = fTemp140 * (1.0f - fTemp141) + fRec65[1] * fTemp141;
			fRec47[0] = fConst42 * std::fabs(0.2f * fTemp139 * fRec65[0]) + fConst41 * fRec47[1];
			float fTemp142 = ((iTemp76) ? 0.0f : fRec69[1] + fConst43 * (1.0f + fRec47[0]));
			fRec69[0] = fTemp142 - std::floor(fTemp142);
			float fTemp143 = fRec69[0] - fRec69[1];
			fVec9[0] = fTemp143;
			int iTemp144 = (fVec9[1] <= 0.0f) & (fTemp143 > 0.0f);
			fRec70[0] = fRec70[1] * static_cast<float>(faust_wrap_sub(1, iTemp144)) + 4.656613e-10f * fTemp22 * static_cast<float>(iTemp144);
			float fTemp145 = 0.5f * (1.0f + fRec70[0]);
			float fTemp146 = 4.656613e-10f * fVec3[1] * static_cast<float>((fRec69[0] >= fTemp145) * (fRec69[1] < fTemp145));
			int iTemp147 = std::abs((fTemp146 > 0.0f) - (fTemp146 < 0.0f));
			fRec68[0] = ((iTemp147) ? fTemp146 : fRec68[1]);
			fRec71[0] = ((iTemp147 > 0) ? fConst44 : std::max<float>(0.0f, -1.0f + fRec71[1]));
			float fTemp148 = fRec68[0] * static_cast<float>(fRec71[0] > 0.0f);
			float fTemp149 = std::tan(fConst45 * fTemp2);
			float fTemp150 = 1.0f / fTemp149;
			float fTemp151 = fTemp2 / std::sin(fConst47 * fTemp2);
			float fTemp152 = fConst46 * fTemp151;
			float fTemp153 = 2.0f * fRec46[1] * (1.0f - 1.0f / UrchinSnare_faustpower2_f(fTemp149));
			float fTemp154 = 1.0f + (fTemp150 + fTemp152) / fTemp149;
			fRec46[0] = fSlow38 * fRec47[0] * static_cast<float>((fTemp148 > 0.0f) - (fTemp148 < 0.0f)) * (0.25f + 0.375f * (1.0f + 2.3283064e-10f * fTemp22)) - (fRec46[2] * (1.0f + (fTemp150 - fTemp152) / fTemp149) + fTemp153) / fTemp154;
			float fTemp155 = fConst48 * fTemp151;
			float fTemp156 = (fTemp153 + fRec46[0] * (1.0f + (fTemp150 + fTemp155) / fTemp149) + fRec46[2] * (1.0f + (fTemp150 - fTemp155) / fTemp149)) / fTemp154;
			fVec10[0] = fTemp156;
			fRec45[0] = -((fRec45[1] * (1.0f - fTemp107) - (fTemp156 + fVec10[1])) / (1.0f + fTemp107));
			float fTemp157 = ((iTemp76) ? 0.0f : fConst50 + fRec73[1]);
			fRec73[0] = fTemp157 - std::floor(fTemp157);
			float fTemp158 = ((iTemp76) ? 0.0f : fRec72[1] + fSlow44 * fTemp2 * (1.0f + 0.25f * ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec73[0])]));
			fRec72[0] = fTemp158 - std::floor(fTemp158);
			float fTemp159 = fSlow45 * (fTemp64 * (1.75f + 1.7f * (UrchinSnare_faustpower3_f(1.0f - fRec75[1]) - 1.0f)) / fTemp2);
			int iTemp160 = std::fabs(fTemp159) < 1.1920929e-07f;
			float fTemp161 = ((iTemp160) ? 0.0f : std::exp(-(fConst21 / ((iTemp160) ? 1.0f : fTemp159))));
			float fTemp162 = UrchinSnare_faustpower3_f(fRec19[0]);
			fRec75[0] = fTemp63 * (1.0f - fTemp161) * std::min<float>(1.0f, std::max<float>(fRec75[1] + 0.5f * fTemp162, fTemp162)) + fRec75[1] * fTemp161;
			fRec74[0] = fConst40 * fRec75[0] + fConst39 * fRec74[1];
			fVec11[0] = fSlow19 * (0.007079458f * (fSlow21 * ((fRec11[2] + fRec11[0] + 2.0f * fRec11[1]) * std::max<float>(0.0f, std::min<float>(fTemp34 / fTemp35, 1.0f + (fTemp35 - fTemp34) / std::max<float>(1.0f, fConst20 / fTemp2))) * fRec19[0] / fTemp30) + fSlow27 * (fTemp40 * (4.0f * fRec20[0] - 8.0f * fRec20[2] + 4.0f * fRec20[4]) / fTemp51 + 0.5f * (fTemp56 * (4.0f * fRec22[0] - 8.0f * fRec22[2] + 4.0f * fRec22[4]) / fTemp62))) + 0.01f * (UrchinSnare_faustpower2_f(fRec23[0]) * (fRec25[2] + fRec25[0] + 2.0f * fRec25[1]) / fTemp75)) + 0.2f * (ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec28[0])] * fRec31[0] * std::pow(fTemp80 / fTemp82, 1.6f) + ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec33[0])] * fRec34[0] * std::pow(fTemp80 / fTemp88, 1.6f) - (ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec36[0])] * fRec37[0] + ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec39[0])] * fRec40[0] * std::pow(fTemp80 / fTemp97, 1.6f) + ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec42[0])] * fRec43[0] * std::pow(fTemp80 / fTemp102, 1.6f))) + fSlow37 * (0.033496544f * fRec45[0] + fSlow42 * fTemp139) + fSlow43 * ftbl0UrchinSnareSIG0[static_cast<int>(65536.0f * fRec72[0])] * fRec74[0];
			float fTemp163 = fTemp9 * fRec5[0] + fVec11[1];
			fVec12[IOTA0 & 1023] = fTemp163;
			fRec8[0] = fTemp6 * (fTemp7 * (fTemp8 * (0.041666668f * fTemp13 * fVec12[(faust_wrap_sub(IOTA0, iTemp12)) & 1023] - 0.16666667f * fTemp14 * fVec12[(faust_wrap_sub(IOTA0, iTemp15)) & 1023]) + 0.25f * fTemp16 * fVec12[(faust_wrap_sub(IOTA0, iTemp17)) & 1023]) - 0.16666667f * fTemp18 * fVec12[(faust_wrap_sub(IOTA0, iTemp19)) & 1023]) + 0.041666668f * fTemp20 * fVec12[(faust_wrap_sub(IOTA0, iTemp21)) & 1023];
			fRec4[0] = fConst8 * (fRec8[0] - fRec8[1] + fConst51 * fRec4[1]);
			fRec3[0] = -(fConst5 * (fConst6 * fRec3[1] - fConst4 * (fRec4[0] - fRec4[1])));
			fRec76[0] = fSlow46 * fTemp1 + fConst10 * fTemp0 * fRec76[1];
			fRec77[0] = ((iTemp33 > 0) ? fConst52 : std::max<float>(0.0f, -1.0f + fRec77[1]));
			iRec78[0] = (iTemp33 > iVec7[1]) + faust_wrap_mul(faust_wrap_add(iRec78[1], iRec78[1] > 0), iTemp33 <= iVec7[1]);
			float fTemp164 = static_cast<float>(iRec78[0]);
			float fTemp165 = std::max<float>(0.0f, std::min<float>(fConst54 * fTemp164, 1.0f + (fConst53 - fTemp164) / std::max<float>(1.0f, fConst0 * ((0.5f - 0.35f * fRec76[0]) / fTemp2))));
			fVec13[0] = fTemp165;
			fRec79[0] = ((iTemp33) ? fVec13[1] : fRec79[1]);
			float fTemp166 = fSlow13 * fRec2[1];
			fRec2[0] = 1.5848932f * fRec3[0] * (1.0f - fRec76[0] + fRec76[0] * ((fRec77[0] > 0.0f) ? fRec79[0] + fTemp165 * (1.0f - fRec79[0]) : fTemp165)) - fSlow12 * (fSlow47 * fRec2[2] + fTemp166);
			float fTemp167 = std::tan(fConst15 * std::min<float>(fConst16, fSlow51 * fTemp2));
			float fTemp168 = 1.0f / fTemp167;
			float fTemp169 = 1.0f - 1.0f / UrchinSnare_faustpower2_f(fTemp167);
			float fTemp170 = 1.0f + (1.847759f + fTemp168) / fTemp167;
			fRec1[0] = fSlow12 * (fTemp166 + fSlow49 * fRec2[0] + fSlow50 * fRec2[2]) - (fRec1[2] * (1.0f + (-1.847759f + fTemp168) / fTemp167) + 2.0f * fRec1[1] * fTemp169) / fTemp170;
			float fTemp171 = 1.0f + (0.76536685f + fTemp168) / fTemp167;
			fRec0[0] = (fRec1[2] + fRec1[0] + 2.0f * fRec1[1]) / fTemp170 - (fRec0[2] * (1.0f + (-0.76536685f + fTemp168) / fTemp167) + 2.0f * fTemp169 * fRec0[1]) / fTemp171;
			float fTemp172 = (fRec0[2] + fRec0[0] + 2.0f * fRec0[1]) / fTemp171;
			fRec80[0] = fSlow53 * fTemp1 + fConst10 * fTemp0 * fRec80[1];
			fRec82[0] = ((iTemp41) ? fSlow54 / fTemp2 : std::max<float>(0.0f, -1.0f + fRec82[1]));
			int iTemp173 = fRec82[0] > 0.0f;
			iVec14[0] = iTemp173;
			iRec81[0] = faust_wrap_add(iTemp173, faust_wrap_mul(iRec81[1], iVec14[1] >= iTemp173));
			iRec83[0] = faust_wrap_mul(faust_wrap_add(1, iRec83[1]), iTemp173 == 0);
			float fTemp174 = (fTemp172 + tanhf(fTemp172)) * (1.0f + fRec80[0] * (-1.0f + std::max<float>(0.0f, std::min<float>(static_cast<float>(iRec81[0]), 1.0f) * (1.0f - fConst55 * static_cast<float>(iRec83[0])))));
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow2 * fTemp174);
			output1[i0] = static_cast<FAUSTFLOAT>(fSlow55 * fTemp174);
			output2[i0] = static_cast<FAUSTFLOAT>(fSlow56 * fTemp174);
			iVec0[1] = iVec0[0];
			fRec5[1] = fRec5[0];
			fRec6[1] = fRec6[0];
			fVec1[1] = fVec1[0];
			fRec9[1] = fRec9[0];
			fRec10[1] = fRec10[0];
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fRec7[1] = fRec7[0];
			iRec15[1] = iRec15[0];
			iRec16[1] = iRec16[0];
			fVec3[1] = fVec3[0];
			fRec14[1] = fRec14[0];
			fRec17[1] = fRec17[0];
			fRec13[1] = fRec13[0];
			fRec12[2] = fRec12[1];
			fRec12[1] = fRec12[0];
			fRec11[2] = fRec11[1];
			fRec11[1] = fRec11[0];
			fVec5[1] = fVec5[0];
			iVec6[1] = iVec6[0];
			iVec7[1] = iVec7[0];
			iRec18[1] = iRec18[0];
			fRec19[1] = fRec19[0];
			fRec21[1] = fRec21[0];
			for (int j0 = 4; j0 > 0; j0 = faust_wrap_sub(j0, 1)) {
				fRec20[j0] = fRec20[faust_wrap_sub(j0, 1)];
			}
			for (int j1 = 4; j1 > 0; j1 = faust_wrap_sub(j1, 1)) {
				fRec22[j1] = fRec22[faust_wrap_sub(j1, 1)];
			}
			fRec24[1] = fRec24[0];
			fRec23[1] = fRec23[0];
			fRec26[2] = fRec26[1];
			fRec26[1] = fRec26[0];
			fRec25[2] = fRec25[1];
			fRec25[1] = fRec25[0];
			fRec30[1] = fRec30[0];
			fRec29[1] = fRec29[0];
			fRec28[1] = fRec28[0];
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
			fRec42[1] = fRec42[0];
			fRec44[1] = fRec44[0];
			fRec43[1] = fRec43[0];
			fRec50[1] = fRec50[0];
			fRec49[1] = fRec49[0];
			fRec48[1] = fRec48[0];
			fRec52[1] = fRec52[0];
			fRec51[1] = fRec51[0];
			fRec53[1] = fRec53[0];
			fRec55[1] = fRec55[0];
			fRec54[1] = fRec54[0];
			fRec56[1] = fRec56[0];
			fRec58[1] = fRec58[0];
			fRec57[1] = fRec57[0];
			fRec59[1] = fRec59[0];
			fRec61[1] = fRec61[0];
			fRec60[1] = fRec60[0];
			fRec62[1] = fRec62[0];
			fRec64[1] = fRec64[0];
			fRec63[1] = fRec63[0];
			iRec67[1] = iRec67[0];
			fRec65[1] = fRec65[0];
			fRec47[1] = fRec47[0];
			fRec69[1] = fRec69[0];
			fVec9[1] = fVec9[0];
			fRec70[1] = fRec70[0];
			fRec68[1] = fRec68[0];
			fRec71[1] = fRec71[0];
			fRec46[2] = fRec46[1];
			fRec46[1] = fRec46[0];
			fVec10[1] = fVec10[0];
			fRec45[1] = fRec45[0];
			fRec73[1] = fRec73[0];
			fRec72[1] = fRec72[0];
			fRec75[1] = fRec75[0];
			fRec74[1] = fRec74[0];
			fVec11[1] = fVec11[0];
			fRec8[1] = fRec8[0];
			fRec4[1] = fRec4[0];
			fRec3[1] = fRec3[0];
			fRec76[1] = fRec76[0];
			fRec77[1] = fRec77[0];
			iRec78[1] = iRec78[0];
			fVec13[1] = fVec13[0];
			fRec79[1] = fRec79[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			fRec80[1] = fRec80[0];
			fRec82[1] = fRec82[0];
			iVec14[1] = iVec14[0];
			iRec81[1] = iRec81[0];
			iRec83[1] = iRec83[0];
		}
	}

};

#endif
