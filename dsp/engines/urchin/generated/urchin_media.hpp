/* ------------------------------------------------------------
copyright: "Copyright (c) 2023 Punk Labs LLC"
license: "GPLv3 (or later)"
name: "OneTrick URCHIN media stage (DR32 per-pad)"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 0 -cn UrchinMedia -dtl 65536 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __UrchinMedia_H__
#define  __UrchinMedia_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

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
#define FAUSTCLASS UrchinMedia
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

class UrchinMediaSIG0 {
	
  private:
	
	int iVec1[2];
	int iRec21[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsUrchinMediaSIG0() {
		return 0;
	}
	int getNumOutputsUrchinMediaSIG0() {
		return 1;
	}
	
	void instanceInitUrchinMediaSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			iVec1[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			iRec21[l3] = 0;
		}
	}
	
	void fillUrchinMediaSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec1[0] = 1;
			iRec21[0] = (faust_wrap_add(iVec1[1], iRec21[1])) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec21[0]));
			iVec1[1] = iVec1[0];
			iRec21[1] = iRec21[0];
		}
	}

};

static UrchinMediaSIG0* newUrchinMediaSIG0() { return (UrchinMediaSIG0*)new UrchinMediaSIG0(); }
static void deleteUrchinMediaSIG0(UrchinMediaSIG0* dsp) { delete dsp; }

static float UrchinMedia_faustpower2_f(float value) {
	return value * value;
}
static float ftbl0UrchinMediaSIG0[65536];

class UrchinMedia : public dsp {
	
 private:
	
	FAUSTFLOAT fHslider0;
	FAUSTFLOAT fHslider1;
	int fSampleRate;
	float fConst0;
	FAUSTFLOAT fHslider2;
	float fConst1;
	float fConst2;
	int iVec0[2];
	int iRec5[2];
	FAUSTFLOAT fHslider3;
	float fConst3;
	float fConst4;
	float fConst5;
	float fConst6;
	float fConst7;
	float fConst8;
	float fConst9;
	float fConst10;
	float fConst11;
	float fConst12;
	float fConst13;
	float fConst14;
	float fConst15;
	float fConst16;
	float fConst17;
	float fConst18;
	float fConst19;
	float fConst20;
	float fConst21;
	float fConst22;
	float fConst23;
	float fConst24;
	float fConst25;
	float fConst26;
	float fConst27;
	float fRec22[2];
	float fConst28;
	float fRec23[2];
	float fConst29;
	float fRec24[2];
	float fConst30;
	float fConst31;
	int iConst32;
	int iRec27[2];
	float fRec26[2];
	float fRec28[2];
	float fRec25[2];
	float fConst33;
	float fConst34;
	float fConst35;
	float fConst36;
	float fConst37;
	float fConst38;
	float fConst39;
	float fConst40;
	float fRec31[2];
	float fRec32[2];
	float fRec30[2];
	int iRec35[2];
	float fVec2[2];
	float fConst41;
	float fRec36[2];
	float fVec3[2];
	float fRec37[2];
	float fRec34[2];
	float fConst42;
	float fRec38[2];
	int iVec4[2];
	int iRec33[2];
	float fConst43;
	int iConst44;
	float fRec41[2];
	float fRec42[2];
	float fRec40[2];
	float fRec39[2];
	float fConst45;
	float fConst46;
	float fConst47;
	float fRec29[3];
	float fConst48;
	float fRec46[2];
	float fVec5[2];
	float fRec47[2];
	float fRec45[2];
	float fRec48[2];
	int iVec6[2];
	int iRec44[2];
	float fConst49;
	float fRec43[3];
	float fVec7[2];
	float fConst50;
	float fRec20[2];
	float fRec49[2];
	float fVec8[2];
	float fRec19[2];
	float fConst51;
	float fConst52;
	float fRec18[3];
	float fConst53;
	float fConst54;
	float fRec17[3];
	float fConst55;
	float fRec16[3];
	float fConst56;
	float fRec15[3];
	float fConst57;
	float fRec14[3];
	float fConst58;
	float fRec13[3];
	float fConst59;
	float fRec12[3];
	float fConst60;
	float fRec11[3];
	float fConst61;
	float fRec10[3];
	float fConst62;
	float fRec9[3];
	float fConst63;
	float fRec8[3];
	float fConst64;
	float fRec7[3];
	float fConst65;
	float fRec6[3];
	FAUSTFLOAT fHslider4;
	float fConst66;
	float fConst67;
	float fConst68;
	float fConst69;
	float fConst70;
	float fConst71;
	float fConst72;
	float fConst73;
	float fConst74;
	float fConst75;
	float fConst76;
	float fConst77;
	float fConst78;
	float fConst79;
	float fConst80;
	int iRec65[2];
	float fRec64[2];
	float fRec66[2];
	float fRec63[2];
	float fRec62[2];
	float fRec67[2];
	float fConst81;
	float fConst82;
	float fRec61[3];
	float fConst83;
	float fRec60[3];
	float fConst84;
	float fRec59[3];
	float fConst85;
	float fRec58[3];
	float fConst86;
	float fRec57[3];
	float fConst87;
	float fRec56[3];
	float fConst88;
	float fRec55[3];
	float fConst89;
	float fRec54[3];
	float fConst90;
	float fRec53[3];
	float fConst91;
	float fRec52[3];
	float fConst92;
	float fRec51[3];
	float fConst93;
	float fConst94;
	float fRec50[3];
	float fConst95;
	float fConst96;
	float fConst97;
	float fConst98;
	float fRec69[2];
	float fRec68[2];
	float fRec4[2];
	float fRec3[3];
	float fRec2[3];
	float fRec1[3];
	float fRec0[3];
	
 public:
	UrchinMedia() {
	}
	
	UrchinMedia(const UrchinMedia&) = default;
	
	virtual ~UrchinMedia() = default;
	
	UrchinMedia& operator=(const UrchinMedia&) = default;
	
	void metadata(Meta* m) { 
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/bitcrusher:author", "Julius O. Smith III, revised by Stephane Letz");
		m->declare("basics.lib/downSample:author", "Romain Michon");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/sAndH:author", "Romain Michon");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 0 -cn UrchinMedia -dtl 65536 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("envelopes.lib/ar:author", "Yann Orlarey, Stéphane Letz");
		m->declare("envelopes.lib/author", "GRAME");
		m->declare("envelopes.lib/copyright", "GRAME");
		m->declare("envelopes.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("envelopes.lib/name", "Faust Envelope Library");
		m->declare("envelopes.lib/version", "1.3.0");
		m->declare("filename", "media.dsp");
		m->declare("filters.lib/bandpass0_bandstop1:author", "Julius O. Smith III");
		m->declare("filters.lib/bandpass0_bandstop1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/bandpass0_bandstop1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/bandpass:author", "Julius O. Smith III");
		m->declare("filters.lib/bandpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/bandpass:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/filterbank:author", "Julius O. Smith III");
		m->declare("filters.lib/filterbank:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/filterbank:license", "LicenseRef-STK-4.3");
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
		m->declare("filters.lib/lowshelf:author", "Julius O. Smith III");
		m->declare("filters.lib/lowshelf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowshelf:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/peak_eq:author", "Julius O. Smith III");
		m->declare("filters.lib/peak_eq:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/peak_eq:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf1:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf1s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1s:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf1sb:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1sb:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1sb:license", "LicenseRef-STK-4.3");
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
		m->declare("name", "OneTrick URCHIN media stage (DR32 per-pad)");
		m->declare("noises.lib/name", "Faust Noise Generator Library");
		m->declare("noises.lib/version", "1.6.0");
		m->declare("onetrick.lib/copyright", "Copyright (c) 2023 Punk Labs LLC");
		m->declare("onetrick.lib/license", "GPLv3 (or later)");
		m->declare("onetrick.lib/name", "OneTrick DSP Library");
		m->declare("oscillators.lib/lf_sawpos:author", "Bart Brouns, revised by Stéphane Letz");
		m->declare("oscillators.lib/lf_sawpos:licence", "LicenseRef-STK-4.3");
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
		return 1;
	}
	virtual int getNumOutputs() {
		return 1;
	}
	
	static void classInit(int sample_rate) {
		UrchinMediaSIG0* sig0 = newUrchinMediaSIG0();
		sig0->instanceInitUrchinMediaSIG0(sample_rate);
		sig0->fillUrchinMediaSIG0(65536, ftbl0UrchinMediaSIG0);
		deleteUrchinMediaSIG0(sig0);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 3.1415927f / fConst0;
		fConst2 = 0.475f * fConst0;
		fConst3 = std::tan(25132.742f / fConst0);
		fConst4 = 1.0f / fConst3;
		fConst5 = 1.0f / ((fConst4 + 0.13080625f) / fConst3 + 1.0f);
		fConst6 = 1.0f / ((fConst4 + 0.39018065f) / fConst3 + 1.0f);
		fConst7 = 1.0f / ((fConst4 + 0.64287895f) / fConst3 + 1.0f);
		fConst8 = 1.0f / ((fConst4 + 0.8845774f) / fConst3 + 1.0f);
		fConst9 = 1.0f / ((fConst4 + 1.1111405f) / fConst3 + 1.0f);
		fConst10 = 1.0f / ((fConst4 + 1.3186916f) / fConst3 + 1.0f);
		fConst11 = 1.0f / ((fConst4 + 1.5036796f) / fConst3 + 1.0f);
		fConst12 = 1.0f / ((fConst4 + 1.6629392f) / fConst3 + 1.0f);
		fConst13 = 1.0f / ((fConst4 + 1.7937455f) / fConst3 + 1.0f);
		fConst14 = 1.0f / ((fConst4 + 1.8938602f) / fConst3 + 1.0f);
		fConst15 = 1.0f / ((fConst4 + 1.9615705f) / fConst3 + 1.0f);
		fConst16 = 1.0f / ((fConst4 + 1.9957179f) / fConst3 + 1.0f);
		fConst17 = std::tan(4712.389f / fConst0);
		fConst18 = 2.0f * (1.0f - 1.0f / UrchinMedia_faustpower2_f(fConst17));
		fConst19 = std::tan(1570.7964f / fConst0);
		fConst20 = 1.0f / fConst19;
		fConst21 = 1.0f / (fConst20 + 1.0f);
		fConst22 = 1.0f - fConst20;
		fConst23 = std::tan(314.15927f / fConst0);
		fConst24 = 1.0f / fConst23;
		fConst25 = 1.0f / (fConst24 + 1.0f);
		fConst26 = 0.0891251f / fConst23;
		fConst27 = 1.3f / fConst0;
		fConst28 = 2.6f / fConst0;
		fConst29 = 5.2f / fConst0;
		fConst30 = std::min<float>(4.8e+04f, fConst0);
		fConst31 = fConst0 / std::min<float>(fConst30, fConst0);
		iConst32 = static_cast<int>(fConst31);
		fConst33 = std::tan(12566.371f / fConst0);
		fConst34 = fConst0 * fConst33;
		fConst35 = UrchinMedia_faustpower2_f(std::sqrt(4.0f * UrchinMedia_faustpower2_f(fConst0) * fConst19 * fConst33));
		fConst36 = 2.0f * fConst34 - 0.5f * (fConst35 / fConst34);
		fConst37 = UrchinMedia_faustpower2_f(1.0f / fConst0) * fConst35;
		fConst38 = 2.0f * (fConst36 / fConst0);
		fConst39 = fConst37 + fConst38 + 4.0f;
		fConst40 = fConst36 / (fConst0 * fConst39);
		fConst41 = 1.5f / fConst0;
		fConst42 = std::round(fConst0 / fConst30);
		fConst43 = 0.001f * fConst0;
		iConst44 = static_cast<int>(0.1f * fConst0);
		fConst45 = 1.0f / fConst39;
		fConst46 = 2.0f * fConst37 + -8.0f;
		fConst47 = fConst37 + (4.0f - fConst38);
		fConst48 = 1e+01f / fConst0;
		fConst49 = 0.00025f * fConst0;
		fConst50 = 1.0f - fConst24;
		fConst51 = 1.0f / fConst17;
		fConst52 = 10995.574f / (fConst0 * std::sin(9424.778f / fConst0));
		fConst53 = (fConst4 + -1.9957179f) / fConst3 + 1.0f;
		fConst54 = 2.0f * (1.0f - 1.0f / UrchinMedia_faustpower2_f(fConst3));
		fConst55 = (fConst4 + -1.9615705f) / fConst3 + 1.0f;
		fConst56 = (fConst4 + -1.8938602f) / fConst3 + 1.0f;
		fConst57 = (fConst4 + -1.7937455f) / fConst3 + 1.0f;
		fConst58 = (fConst4 + -1.6629392f) / fConst3 + 1.0f;
		fConst59 = (fConst4 + -1.5036796f) / fConst3 + 1.0f;
		fConst60 = (fConst4 + -1.3186916f) / fConst3 + 1.0f;
		fConst61 = (fConst4 + -1.1111405f) / fConst3 + 1.0f;
		fConst62 = (fConst4 + -0.8845774f) / fConst3 + 1.0f;
		fConst63 = (fConst4 + -0.64287895f) / fConst3 + 1.0f;
		fConst64 = (fConst4 + -0.39018065f) / fConst3 + 1.0f;
		fConst65 = (fConst4 + -0.13080625f) / fConst3 + 1.0f;
		fConst66 = std::tan(3.1415927f * (std::min<float>(fConst2, 1.65e+04f) / fConst0));
		fConst67 = 1.0f / fConst66;
		fConst68 = (fConst67 + 0.13080625f) / fConst66 + 1.0f;
		fConst69 = 0.06309573f / fConst68;
		fConst70 = 1.0f / ((fConst67 + 0.39018065f) / fConst66 + 1.0f);
		fConst71 = 1.0f / ((fConst67 + 0.64287895f) / fConst66 + 1.0f);
		fConst72 = 1.0f / ((fConst67 + 0.8845774f) / fConst66 + 1.0f);
		fConst73 = 1.0f / ((fConst67 + 1.1111405f) / fConst66 + 1.0f);
		fConst74 = 1.0f / ((fConst67 + 1.3186916f) / fConst66 + 1.0f);
		fConst75 = 1.0f / ((fConst67 + 1.5036796f) / fConst66 + 1.0f);
		fConst76 = 1.0f / ((fConst67 + 1.6629392f) / fConst66 + 1.0f);
		fConst77 = 1.0f / ((fConst67 + 1.7937455f) / fConst66 + 1.0f);
		fConst78 = 1.0f / ((fConst67 + 1.8938602f) / fConst66 + 1.0f);
		fConst79 = 1.0f / ((fConst67 + 1.9615705f) / fConst66 + 1.0f);
		fConst80 = 1.0f / ((fConst67 + 1.9957179f) / fConst66 + 1.0f);
		fConst81 = (fConst67 + -1.9957179f) / fConst66 + 1.0f;
		fConst82 = 2.0f * (1.0f - 1.0f / UrchinMedia_faustpower2_f(fConst66));
		fConst83 = (fConst67 + -1.9615705f) / fConst66 + 1.0f;
		fConst84 = (fConst67 + -1.8938602f) / fConst66 + 1.0f;
		fConst85 = (fConst67 + -1.7937455f) / fConst66 + 1.0f;
		fConst86 = (fConst67 + -1.6629392f) / fConst66 + 1.0f;
		fConst87 = (fConst67 + -1.5036796f) / fConst66 + 1.0f;
		fConst88 = (fConst67 + -1.3186916f) / fConst66 + 1.0f;
		fConst89 = (fConst67 + -1.1111405f) / fConst66 + 1.0f;
		fConst90 = (fConst67 + -0.8845774f) / fConst66 + 1.0f;
		fConst91 = (fConst67 + -0.64287895f) / fConst66 + 1.0f;
		fConst92 = (fConst67 + -0.39018065f) / fConst66 + 1.0f;
		fConst93 = 1.0f / fConst68;
		fConst94 = (fConst67 + -0.13080625f) / fConst66 + 1.0f;
		fConst95 = std::exp(-(1e+04f / fConst0));
		fConst96 = 1.0f - fConst95;
		fConst97 = std::exp(-(2e+01f / fConst0));
		fConst98 = 1.0f - fConst97;
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider1 = static_cast<FAUSTFLOAT>(16.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(44.1f);
		fHslider3 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider4 = static_cast<FAUSTFLOAT>(0.0f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			iRec5[l1] = 0;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			fRec22[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec23[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = faust_wrap_add(l6, 1)) {
			fRec24[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			iRec27[l7] = 0;
		}
		for (int l8 = 0; l8 < 2; l8 = faust_wrap_add(l8, 1)) {
			fRec26[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec28[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = faust_wrap_add(l10, 1)) {
			fRec25[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec31[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = faust_wrap_add(l12, 1)) {
			fRec32[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec30[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			iRec35[l14] = 0;
		}
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			fVec2[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = faust_wrap_add(l16, 1)) {
			fRec36[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fVec3[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = faust_wrap_add(l18, 1)) {
			fRec37[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			fRec34[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fRec38[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			iVec4[l21] = 0;
		}
		for (int l22 = 0; l22 < 2; l22 = faust_wrap_add(l22, 1)) {
			iRec33[l22] = 0;
		}
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			fRec41[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = faust_wrap_add(l24, 1)) {
			fRec42[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec40[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2; l26 = faust_wrap_add(l26, 1)) {
			fRec39[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 3; l27 = faust_wrap_add(l27, 1)) {
			fRec29[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2; l28 = faust_wrap_add(l28, 1)) {
			fRec46[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			fVec5[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2; l30 = faust_wrap_add(l30, 1)) {
			fRec47[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			fRec45[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2; l32 = faust_wrap_add(l32, 1)) {
			fRec48[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			iVec6[l33] = 0;
		}
		for (int l34 = 0; l34 < 2; l34 = faust_wrap_add(l34, 1)) {
			iRec44[l34] = 0;
		}
		for (int l35 = 0; l35 < 3; l35 = faust_wrap_add(l35, 1)) {
			fRec43[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = faust_wrap_add(l36, 1)) {
			fVec7[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec20[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = faust_wrap_add(l38, 1)) {
			fRec49[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			fVec8[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = faust_wrap_add(l40, 1)) {
			fRec19[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 3; l41 = faust_wrap_add(l41, 1)) {
			fRec18[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 3; l42 = faust_wrap_add(l42, 1)) {
			fRec17[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 3; l43 = faust_wrap_add(l43, 1)) {
			fRec16[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 3; l44 = faust_wrap_add(l44, 1)) {
			fRec15[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 3; l45 = faust_wrap_add(l45, 1)) {
			fRec14[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 3; l46 = faust_wrap_add(l46, 1)) {
			fRec13[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 3; l47 = faust_wrap_add(l47, 1)) {
			fRec12[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 3; l48 = faust_wrap_add(l48, 1)) {
			fRec11[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 3; l49 = faust_wrap_add(l49, 1)) {
			fRec10[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 3; l50 = faust_wrap_add(l50, 1)) {
			fRec9[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 3; l51 = faust_wrap_add(l51, 1)) {
			fRec8[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 3; l52 = faust_wrap_add(l52, 1)) {
			fRec7[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 3; l53 = faust_wrap_add(l53, 1)) {
			fRec6[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 2; l54 = faust_wrap_add(l54, 1)) {
			iRec65[l54] = 0;
		}
		for (int l55 = 0; l55 < 2; l55 = faust_wrap_add(l55, 1)) {
			fRec64[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 2; l56 = faust_wrap_add(l56, 1)) {
			fRec66[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 2; l57 = faust_wrap_add(l57, 1)) {
			fRec63[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 2; l58 = faust_wrap_add(l58, 1)) {
			fRec62[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = faust_wrap_add(l59, 1)) {
			fRec67[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 3; l60 = faust_wrap_add(l60, 1)) {
			fRec61[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 3; l61 = faust_wrap_add(l61, 1)) {
			fRec60[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 3; l62 = faust_wrap_add(l62, 1)) {
			fRec59[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 3; l63 = faust_wrap_add(l63, 1)) {
			fRec58[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 3; l64 = faust_wrap_add(l64, 1)) {
			fRec57[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 3; l65 = faust_wrap_add(l65, 1)) {
			fRec56[l65] = 0.0f;
		}
		for (int l66 = 0; l66 < 3; l66 = faust_wrap_add(l66, 1)) {
			fRec55[l66] = 0.0f;
		}
		for (int l67 = 0; l67 < 3; l67 = faust_wrap_add(l67, 1)) {
			fRec54[l67] = 0.0f;
		}
		for (int l68 = 0; l68 < 3; l68 = faust_wrap_add(l68, 1)) {
			fRec53[l68] = 0.0f;
		}
		for (int l69 = 0; l69 < 3; l69 = faust_wrap_add(l69, 1)) {
			fRec52[l69] = 0.0f;
		}
		for (int l70 = 0; l70 < 3; l70 = faust_wrap_add(l70, 1)) {
			fRec51[l70] = 0.0f;
		}
		for (int l71 = 0; l71 < 3; l71 = faust_wrap_add(l71, 1)) {
			fRec50[l71] = 0.0f;
		}
		for (int l72 = 0; l72 < 2; l72 = faust_wrap_add(l72, 1)) {
			fRec69[l72] = 0.0f;
		}
		for (int l73 = 0; l73 < 2; l73 = faust_wrap_add(l73, 1)) {
			fRec68[l73] = 0.0f;
		}
		for (int l74 = 0; l74 < 2; l74 = faust_wrap_add(l74, 1)) {
			fRec4[l74] = 0.0f;
		}
		for (int l75 = 0; l75 < 3; l75 = faust_wrap_add(l75, 1)) {
			fRec3[l75] = 0.0f;
		}
		for (int l76 = 0; l76 < 3; l76 = faust_wrap_add(l76, 1)) {
			fRec2[l76] = 0.0f;
		}
		for (int l77 = 0; l77 < 3; l77 = faust_wrap_add(l77, 1)) {
			fRec1[l77] = 0.0f;
		}
		for (int l78 = 0; l78 < 3; l78 = faust_wrap_add(l78, 1)) {
			fRec0[l78] = 0.0f;
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
	
	virtual UrchinMedia* clone() {
		return new UrchinMedia(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("OneTrick URCHIN media stage (DR32 per-pad)");
		ui_interface->declare(&fHslider3, "100", "");
		ui_interface->declare(&fHslider3, "export", "Noise Amount");
		ui_interface->declare(&fHslider3, "unit", "%");
		ui_interface->addHorizontalSlider("Global_Noise", &fHslider3, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider4, "110", "");
		ui_interface->declare(&fHslider4, "enum", "Vinyl,Tape");
		ui_interface->declare(&fHslider4, "export", "Noise Type");
		ui_interface->addHorizontalSlider("Global_NoiseType", &fHslider4, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->declare(&fHslider0, "120", "");
		ui_interface->declare(&fHslider0, "export", "Saturation");
		ui_interface->declare(&fHslider0, "unit", "%");
		ui_interface->addHorizontalSlider("Global_Saturation", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider2, "140", "");
		ui_interface->declare(&fHslider2, "export", "Samplerate");
		ui_interface->declare(&fHslider2, "unit", "kHz");
		ui_interface->addHorizontalSlider("Global_SampleRate", &fHslider2, FAUSTFLOAT(44.1f), FAUSTFLOAT(11.025f), FAUSTFLOAT(44.1f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fHslider1, "150", "");
		ui_interface->declare(&fHslider1, "export", "Bits");
		ui_interface->declare(&fHslider1, "unit", "bit");
		ui_interface->addHorizontalSlider("Global_Bits", &fHslider1, FAUSTFLOAT(16.0f), FAUSTFLOAT(4.0f), FAUSTFLOAT(16.0f), FAUSTFLOAT(1.0f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		float fSlow0 = static_cast<float>(fHslider0);
		float fSlow1 = 1.0f - 0.007f * fSlow0;
		float fSlow2 = static_cast<float>(fHslider1);
		float fSlow3 = static_cast<float>(fSlow2 < 15.5f);
		float fSlow4 = std::pow(2.0f, fSlow2) + -1.0f;
		float fSlow5 = fSlow3 / fSlow4;
		float fSlow6 = std::min<float>(fConst0, 1e+03f * static_cast<float>(fHslider2));
		float fSlow7 = static_cast<float>(fSlow6 < 44099.5f);
		float fSlow8 = std::tan(fConst1 * std::min<float>(fConst2, 0.5f * fSlow6));
		float fSlow9 = 1.0f / fSlow8;
		float fSlow10 = (fSlow9 + 0.39018065f) / fSlow8 + 1.0f;
		float fSlow11 = fSlow7 / fSlow10;
		float fSlow12 = 1.0f / ((fSlow9 + 1.1111405f) / fSlow8 + 1.0f);
		float fSlow13 = 1.0f / ((fSlow9 + 1.6629392f) / fSlow8 + 1.0f);
		float fSlow14 = 1.0f / ((fSlow9 + 1.9615705f) / fSlow8 + 1.0f);
		int iSlow15 = static_cast<int>(fConst0 / fSlow6);
		float fSlow16 = 0.00063095737f * static_cast<float>(fHslider3);
		float fSlow17 = static_cast<float>(fHslider4);
		float fSlow18 = (fSlow9 + -1.9615705f) / fSlow8 + 1.0f;
		float fSlow19 = 2.0f * (1.0f - 1.0f / UrchinMedia_faustpower2_f(fSlow8));
		float fSlow20 = (fSlow9 + -1.6629392f) / fSlow8 + 1.0f;
		float fSlow21 = (fSlow9 + -1.1111405f) / fSlow8 + 1.0f;
		float fSlow22 = 1.0f / fSlow10;
		float fSlow23 = (fSlow9 + -0.39018065f) / fSlow8 + 1.0f;
		float fSlow24 = 1.0f - fSlow7;
		float fSlow25 = 1.0f - fSlow3;
		float fSlow26 = 1.0f / std::min<float>(std::max<float>(0.004f * fSlow0 + 0.5f, 0.001f), 0.999f) + -2.0f;
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			iVec0[0] = 1;
			iRec5[0] = faust_wrap_add(1, iRec5[1]);
			float fTemp0 = static_cast<float>(input0[i0]);
			int iTemp1 = faust_wrap_sub(1, iVec0[1]);
			float fTemp2 = ((iTemp1) ? 0.0f : fConst27 + fRec22[1]);
			fRec22[0] = fTemp2 - std::floor(fTemp2);
			float fTemp3 = ((iTemp1) ? 0.0f : fConst28 + fRec23[1]);
			fRec23[0] = fTemp3 - std::floor(fTemp3);
			float fTemp4 = ((iTemp1) ? 0.0f : fConst29 + fRec24[1]);
			fRec24[0] = fTemp4 - std::floor(fTemp4);
			float fTemp5 = 0.8f + 0.025f * (1.0f + ftbl0UrchinMediaSIG0[static_cast<int>(65536.0f * fRec22[0])]) * (1.0f + ftbl0UrchinMediaSIG0[static_cast<int>(65536.0f * fRec23[0])]) * (1.0f + ftbl0UrchinMediaSIG0[static_cast<int>(65536.0f * fRec24[0])]);
			int iTemp6 = (iRec5[1] % iConst32) == 0;
			int iTemp7 = faust_wrap_mul(1103515245, faust_wrap_add(12345, faust_wrap_mul(1103515245, faust_wrap_add(12345, iRec27[1]))));
			iRec27[0] = faust_wrap_mul(1103515245, faust_wrap_add(12345, faust_wrap_mul(1103515245, faust_wrap_add(12345, iTemp7))));
			fRec26[0] = ((iTemp6) ? 4.656613e-10f * static_cast<float>(iRec27[0]) : fRec26[1]);
			float fTemp8 = ((fRec26[0] != fRec26[1]) ? fConst31 : -1.0f + fRec28[1]);
			fRec28[0] = fTemp8;
			fRec25[0] = ((fTemp8 > 0.0f) ? fRec25[1] + (fRec26[0] - fRec25[1]) / fTemp8 : fRec26[0]);
			fRec31[0] = ((iTemp6) ? 4.656613e-10f * static_cast<float>(iTemp7) : fRec31[1]);
			float fTemp9 = ((fRec31[0] != fRec31[1]) ? fConst31 : -1.0f + fRec32[1]);
			fRec32[0] = fTemp9;
			fRec30[0] = ((fTemp9 > 0.0f) ? fRec30[1] + (fRec31[0] - fRec30[1]) / fTemp9 : fRec31[0]);
			iRec35[0] = faust_wrap_add(12345, faust_wrap_mul(1103515245, iRec35[1]));
			float fTemp10 = static_cast<float>(iRec35[0]);
			fVec2[0] = fTemp10;
			float fTemp11 = ((iTemp1) ? 0.0f : fConst41 + fRec36[1]);
			fRec36[0] = fTemp11 - std::floor(fTemp11);
			float fTemp12 = fRec36[0] - fRec36[1];
			fVec3[0] = fTemp12;
			int iTemp13 = (fVec3[1] <= 0.0f) & (fTemp12 > 0.0f);
			fRec37[0] = fRec37[1] * static_cast<float>(faust_wrap_sub(1, iTemp13)) + 4.656613e-10f * fTemp10 * static_cast<float>(iTemp13);
			float fTemp14 = 0.5f * (1.0f + fRec37[0]);
			float fTemp15 = 4.656613e-10f * fVec2[1] * static_cast<float>((fRec36[0] >= fTemp14) * (fRec36[1] < fTemp14));
			int iTemp16 = std::abs((fTemp15 > 0.0f) - (fTemp15 < 0.0f));
			fRec34[0] = ((iTemp16) ? fTemp15 : fRec34[1]);
			fRec38[0] = ((iTemp16 > 0) ? fConst42 : std::max<float>(0.0f, -1.0f + fRec38[1]));
			float fTemp17 = fRec34[0] * static_cast<float>(fRec38[0] > 0.0f);
			int iTemp18 = (fTemp17 > 0.0f) - (fTemp17 < 0.0f);
			iVec4[0] = iTemp18;
			iRec33[0] = faust_wrap_add(faust_wrap_mul(faust_wrap_add(iRec33[1], iRec33[1] > 0), iTemp18 <= iVec4[1]), iTemp18 > iVec4[1]);
			fRec41[0] = ((iTemp6) ? 4.656613e-10f * fTemp10 : fRec41[1]);
			float fTemp19 = ((fRec41[0] != fRec41[1]) ? fConst31 : -1.0f + fRec42[1]);
			fRec42[0] = fTemp19;
			fRec40[0] = ((fTemp19 > 0.0f) ? fRec40[1] + (fRec41[0] - fRec40[1]) / fTemp19 : fRec41[0]);
			fRec39[0] = (((iRec5[1] % iConst44) == 0) ? 1.0f + 3.5f * (1.0f + fRec40[0]) : fRec39[1]);
			float fTemp20 = static_cast<float>(iRec33[0]) / std::max<float>(1.0f, fConst43 * fRec39[0]);
			fRec29[0] = fRec30[0] * std::max<float>(0.0f, std::min<float>(fTemp20, 2.0f - fTemp20)) - fConst45 * (fConst46 * fRec29[1] + fConst47 * fRec29[2]);
			float fTemp21 = ((iTemp1) ? 0.0f : fRec46[1] + fConst48 * fTemp5);
			fRec46[0] = fTemp21 - std::floor(fTemp21);
			float fTemp22 = fRec46[0] - fRec46[1];
			fVec5[0] = fTemp22;
			int iTemp23 = (fVec5[1] <= 0.0f) & (fTemp22 > 0.0f);
			fRec47[0] = fRec47[1] * static_cast<float>(faust_wrap_sub(1, iTemp23)) + 4.656613e-10f * fTemp10 * static_cast<float>(iTemp23);
			float fTemp24 = 0.5f * (1.0f + fRec47[0]);
			float fTemp25 = 4.656613e-10f * fVec2[1] * static_cast<float>((fRec46[0] >= fTemp24) * (fRec46[1] < fTemp24));
			int iTemp26 = std::abs((fTemp25 > 0.0f) - (fTemp25 < 0.0f));
			fRec45[0] = ((iTemp26) ? fTemp25 : fRec45[1]);
			fRec48[0] = ((iTemp26 > 0) ? fConst42 : std::max<float>(0.0f, -1.0f + fRec48[1]));
			float fTemp27 = fRec45[0] * static_cast<float>(fRec48[0] > 0.0f);
			int iTemp28 = (fTemp27 > 0.0f) - (fTemp27 < 0.0f);
			iVec6[0] = iTemp28;
			iRec44[0] = faust_wrap_add(faust_wrap_mul(faust_wrap_add(iRec44[1], iRec44[1] > 0), iTemp28 <= iVec6[1]), iTemp28 > iVec6[1]);
			float fTemp29 = static_cast<float>(iRec44[0]) / std::max<float>(1.0f, fConst49 * fRec39[0]);
			fRec43[0] = fRec30[0] * std::max<float>(0.0f, std::min<float>(fTemp29, 2.0f - fTemp29)) - fConst45 * (fConst46 * fRec43[1] + fConst47 * fRec43[2]);
			float fTemp30 = fTemp5 * (fRec25[0] + fConst40 * (3e+01f * (fRec29[0] - fRec29[2]) + 2e+01f * (fRec43[0] - fRec43[2])));
			fVec7[0] = fTemp30;
			fRec20[0] = fConst25 * (fConst26 * (fTemp30 - fVec7[1]) - fConst50 * fRec20[1]);
			fRec49[0] = fConst25 * (0.0891251f * (fTemp30 + fVec7[1]) - fConst50 * fRec49[1]);
			float fTemp31 = fRec20[0] + 7.0794578f * fRec49[0];
			fVec8[0] = fTemp31;
			fRec19[0] = -(fConst21 * (fConst22 * fRec19[1] - (fTemp31 + fVec8[1])));
			float fTemp32 = 1e+01f * fTemp5;
			int iTemp33 = fTemp32 > 0.0f;
			float fTemp34 = fConst52 * std::pow(1e+01f, 0.05f * std::fabs(fTemp32));
			float fTemp35 = ((iTemp33) ? fConst52 : fTemp34);
			float fTemp36 = fConst18 * fRec18[1];
			float fTemp37 = 1.0f + fConst51 * (fConst51 + fTemp35);
			fRec18[0] = 0.75f * fRec19[0] + 0.25f * fTemp31 - (fRec18[2] * (1.0f + fConst51 * (fConst51 - fTemp35)) + fTemp36) / fTemp37;
			float fTemp38 = ((iTemp33) ? fTemp34 : fConst52);
			fRec17[0] = (fTemp36 + fRec18[0] * (1.0f + fConst51 * (fConst51 + fTemp38)) + fRec18[2] * (1.0f + fConst51 * (fConst51 - fTemp38))) / fTemp37 - fConst16 * (fConst53 * fRec17[2] + fConst54 * fRec17[1]);
			fRec16[0] = fConst16 * (fRec17[2] + fRec17[0] + 2.0f * fRec17[1]) - fConst15 * (fConst55 * fRec16[2] + fConst54 * fRec16[1]);
			fRec15[0] = fConst15 * (fRec16[2] + fRec16[0] + 2.0f * fRec16[1]) - fConst14 * (fConst56 * fRec15[2] + fConst54 * fRec15[1]);
			fRec14[0] = fConst14 * (fRec15[2] + fRec15[0] + 2.0f * fRec15[1]) - fConst13 * (fConst57 * fRec14[2] + fConst54 * fRec14[1]);
			fRec13[0] = fConst13 * (fRec14[2] + fRec14[0] + 2.0f * fRec14[1]) - fConst12 * (fConst58 * fRec13[2] + fConst54 * fRec13[1]);
			fRec12[0] = fConst12 * (fRec13[2] + fRec13[0] + 2.0f * fRec13[1]) - fConst11 * (fConst59 * fRec12[2] + fConst54 * fRec12[1]);
			fRec11[0] = fConst11 * (fRec12[2] + fRec12[0] + 2.0f * fRec12[1]) - fConst10 * (fConst60 * fRec11[2] + fConst54 * fRec11[1]);
			fRec10[0] = fConst10 * (fRec11[2] + fRec11[0] + 2.0f * fRec11[1]) - fConst9 * (fConst61 * fRec10[2] + fConst54 * fRec10[1]);
			fRec9[0] = fConst9 * (fRec10[2] + fRec10[0] + 2.0f * fRec10[1]) - fConst8 * (fConst62 * fRec9[2] + fConst54 * fRec9[1]);
			fRec8[0] = fConst8 * (fRec9[2] + fRec9[0] + 2.0f * fRec9[1]) - fConst7 * (fConst63 * fRec8[2] + fConst54 * fRec8[1]);
			fRec7[0] = fConst7 * (fRec8[2] + fRec8[0] + 2.0f * fRec8[1]) - fConst6 * (fConst64 * fRec7[2] + fConst54 * fRec7[1]);
			fRec6[0] = fConst6 * (fRec7[2] + fRec7[0] + 2.0f * fRec7[1]) - fConst5 * (fConst65 * fRec6[2] + fConst54 * fRec6[1]);
			float fTemp39 = fConst5 * (fRec6[2] + fRec6[0] + 2.0f * fRec6[1]);
			iRec65[0] = faust_wrap_mul(1103515245, faust_wrap_add(12345, faust_wrap_mul(1103515245, faust_wrap_add(12345, iRec65[1]))));
			fRec64[0] = ((iTemp6) ? 4.656613e-10f * static_cast<float>(iRec65[0]) : fRec64[1]);
			float fTemp40 = ((fRec64[0] != fRec64[1]) ? fConst31 : -1.0f + fRec66[1]);
			fRec66[0] = fTemp40;
			fRec63[0] = ((fTemp40 > 0.0f) ? fRec63[1] + (fRec64[0] - fRec63[1]) / fTemp40 : fRec64[0]);
			fRec62[0] = -(fConst25 * (fConst50 * fRec62[1] - fConst24 * (fRec63[0] - fRec63[1])));
			fRec67[0] = -(fConst25 * (fConst50 * fRec67[1] - (fRec63[1] + fRec63[0])));
			fRec61[0] = fRec62[0] + 7.0794578f * fRec67[0] - fConst80 * (fConst81 * fRec61[2] + fConst82 * fRec61[1]);
			fRec60[0] = fConst80 * (fRec61[2] + fRec61[0] + 2.0f * fRec61[1]) - fConst79 * (fConst83 * fRec60[2] + fConst82 * fRec60[1]);
			fRec59[0] = fConst79 * (fRec60[2] + fRec60[0] + 2.0f * fRec60[1]) - fConst78 * (fConst84 * fRec59[2] + fConst82 * fRec59[1]);
			fRec58[0] = fConst78 * (fRec59[2] + fRec59[0] + 2.0f * fRec59[1]) - fConst77 * (fConst85 * fRec58[2] + fConst82 * fRec58[1]);
			fRec57[0] = fConst77 * (fRec58[2] + fRec58[0] + 2.0f * fRec58[1]) - fConst76 * (fConst86 * fRec57[2] + fConst82 * fRec57[1]);
			fRec56[0] = fConst76 * (fRec57[2] + fRec57[0] + 2.0f * fRec57[1]) - fConst75 * (fConst87 * fRec56[2] + fConst82 * fRec56[1]);
			fRec55[0] = fConst75 * (fRec56[2] + fRec56[0] + 2.0f * fRec56[1]) - fConst74 * (fConst88 * fRec55[2] + fConst82 * fRec55[1]);
			fRec54[0] = fConst74 * (fRec55[2] + fRec55[0] + 2.0f * fRec55[1]) - fConst73 * (fConst89 * fRec54[2] + fConst82 * fRec54[1]);
			fRec53[0] = fConst73 * (fRec54[2] + fRec54[0] + 2.0f * fRec54[1]) - fConst72 * (fConst90 * fRec53[2] + fConst82 * fRec53[1]);
			fRec52[0] = fConst72 * (fRec53[2] + fRec53[0] + 2.0f * fRec53[1]) - fConst71 * (fConst91 * fRec52[2] + fConst82 * fRec52[1]);
			fRec51[0] = fConst71 * (fRec52[2] + fRec52[0] + 2.0f * fRec52[1]) - fConst70 * (fConst92 * fRec51[2] + fConst82 * fRec51[1]);
			fRec50[0] = fConst70 * (fRec51[2] + fRec51[0] + 2.0f * fRec51[1]) - fConst93 * (fConst94 * fRec50[2] + fConst82 * fRec50[1]);
			float fTemp41 = std::fabs(fTemp0);
			fRec69[0] = std::max<float>(fTemp41, fConst98 * fTemp41 + fConst97 * fRec69[1]);
			fRec68[0] = fConst96 * fRec69[0] + fConst95 * fRec68[1];
			float fTemp42 = fTemp0 + fSlow16 * (fTemp39 + fSlow17 * (fConst69 * (fRec50[2] + fRec50[0] + 2.0f * fRec50[1]) - fTemp39)) * std::min<float>(1.0f, 1e+04f * fRec68[0]);
			fRec4[0] = (((iRec5[1] % iSlow15) == 0) ? fTemp42 : fRec4[1]);
			fRec3[0] = fRec4[0] - fSlow14 * (fSlow18 * fRec3[2] + fSlow19 * fRec3[1]);
			fRec2[0] = fSlow14 * (fRec3[2] + fRec3[0] + 2.0f * fRec3[1]) - fSlow13 * (fSlow20 * fRec2[2] + fSlow19 * fRec2[1]);
			fRec1[0] = fSlow13 * (fRec2[2] + fRec2[0] + 2.0f * fRec2[1]) - fSlow12 * (fSlow21 * fRec1[2] + fSlow19 * fRec1[1]);
			fRec0[0] = fSlow12 * (fRec1[2] + fRec1[0] + 2.0f * fRec1[1]) - fSlow22 * (fSlow23 * fRec0[2] + fSlow19 * fRec0[1]);
			float fTemp43 = fSlow11 * (fRec0[2] + fRec0[0] + 2.0f * fRec0[1]) + fSlow24 * fTemp42;
			float fTemp44 = fSlow5 * std::round(fSlow4 * fTemp43) + fSlow25 * fTemp43;
			float fTemp45 = std::fabs(fTemp44);
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow1 * (fTemp45 * (1.0f - static_cast<float>(faust_wrap_mul(2, fTemp44 < 0.0f))) / (1.0f + fSlow26 * (1.0f - fTemp45))));
			iVec0[1] = iVec0[0];
			iRec5[1] = iRec5[0];
			fRec22[1] = fRec22[0];
			fRec23[1] = fRec23[0];
			fRec24[1] = fRec24[0];
			iRec27[1] = iRec27[0];
			fRec26[1] = fRec26[0];
			fRec28[1] = fRec28[0];
			fRec25[1] = fRec25[0];
			fRec31[1] = fRec31[0];
			fRec32[1] = fRec32[0];
			fRec30[1] = fRec30[0];
			iRec35[1] = iRec35[0];
			fVec2[1] = fVec2[0];
			fRec36[1] = fRec36[0];
			fVec3[1] = fVec3[0];
			fRec37[1] = fRec37[0];
			fRec34[1] = fRec34[0];
			fRec38[1] = fRec38[0];
			iVec4[1] = iVec4[0];
			iRec33[1] = iRec33[0];
			fRec41[1] = fRec41[0];
			fRec42[1] = fRec42[0];
			fRec40[1] = fRec40[0];
			fRec39[1] = fRec39[0];
			fRec29[2] = fRec29[1];
			fRec29[1] = fRec29[0];
			fRec46[1] = fRec46[0];
			fVec5[1] = fVec5[0];
			fRec47[1] = fRec47[0];
			fRec45[1] = fRec45[0];
			fRec48[1] = fRec48[0];
			iVec6[1] = iVec6[0];
			iRec44[1] = iRec44[0];
			fRec43[2] = fRec43[1];
			fRec43[1] = fRec43[0];
			fVec7[1] = fVec7[0];
			fRec20[1] = fRec20[0];
			fRec49[1] = fRec49[0];
			fVec8[1] = fVec8[0];
			fRec19[1] = fRec19[0];
			fRec18[2] = fRec18[1];
			fRec18[1] = fRec18[0];
			fRec17[2] = fRec17[1];
			fRec17[1] = fRec17[0];
			fRec16[2] = fRec16[1];
			fRec16[1] = fRec16[0];
			fRec15[2] = fRec15[1];
			fRec15[1] = fRec15[0];
			fRec14[2] = fRec14[1];
			fRec14[1] = fRec14[0];
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
			iRec65[1] = iRec65[0];
			fRec64[1] = fRec64[0];
			fRec66[1] = fRec66[0];
			fRec63[1] = fRec63[0];
			fRec62[1] = fRec62[0];
			fRec67[1] = fRec67[0];
			fRec61[2] = fRec61[1];
			fRec61[1] = fRec61[0];
			fRec60[2] = fRec60[1];
			fRec60[1] = fRec60[0];
			fRec59[2] = fRec59[1];
			fRec59[1] = fRec59[0];
			fRec58[2] = fRec58[1];
			fRec58[1] = fRec58[0];
			fRec57[2] = fRec57[1];
			fRec57[1] = fRec57[0];
			fRec56[2] = fRec56[1];
			fRec56[1] = fRec56[0];
			fRec55[2] = fRec55[1];
			fRec55[1] = fRec55[0];
			fRec54[2] = fRec54[1];
			fRec54[1] = fRec54[0];
			fRec53[2] = fRec53[1];
			fRec53[1] = fRec53[0];
			fRec52[2] = fRec52[1];
			fRec52[1] = fRec52[0];
			fRec51[2] = fRec51[1];
			fRec51[1] = fRec51[0];
			fRec50[2] = fRec50[1];
			fRec50[1] = fRec50[0];
			fRec69[1] = fRec69[0];
			fRec68[1] = fRec68[0];
			fRec4[1] = fRec4[0];
			fRec3[2] = fRec3[1];
			fRec3[1] = fRec3[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
		}
	}

};

#endif
