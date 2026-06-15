// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// GENERATED FILE - do not edit by hand. Regenerate with `make faust-kernels` (cyfaust cpp backend).
// Source: src/engine/reverb/zita.dsp. The generated `class mydsp` is wrapped in namespace spotykach::rv_zita; its
// dsp/UI/Meta base types resolve to the shared arch shim (see engine/faust_arch.h).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#include "engine/faust_arch.h"

namespace spotykach { namespace rv_zita {
/* ------------------------------------------------------------
name: "zita"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __rv_zita_H__
#define  __rv_zita_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 


#ifndef FAUSTCLASS 
#define FAUSTCLASS mydsp
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

static float mydsp_faustpower2_f(float value) {
	return value * value;
}

class mydsp : public dsp {
	
 private:
	
	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fVslider0;
	float fConst2;
	float fRec0[2];
	int IOTA0;
	float fVec0[16384];
	FAUSTFLOAT fVslider1;
	float fRec1[2];
	float fConst3;
	FAUSTFLOAT fVslider2;
	FAUSTFLOAT fVslider3;
	float fConst4;
	float fConst5;
	FAUSTFLOAT fVslider4;
	FAUSTFLOAT fVslider5;
	FAUSTFLOAT fVslider6;
	float fConst6;
	FAUSTFLOAT fVslider7;
	float fRec15[2];
	float fRec14[2];
	float fVec1[32768];
	float fConst7;
	int iConst8;
	float fVec2[16384];
	float fConst9;
	FAUSTFLOAT fVslider8;
	float fVec3[2048];
	int iConst10;
	float fRec12[2];
	float fConst11;
	float fConst12;
	float fRec19[2];
	float fRec18[2];
	float fVec4[32768];
	float fConst13;
	int iConst14;
	float fVec5[4096];
	int iConst15;
	float fRec16[2];
	float fConst16;
	float fConst17;
	float fRec23[2];
	float fRec22[2];
	float fVec6[16384];
	float fConst18;
	int iConst19;
	float fVec7[4096];
	int iConst20;
	float fRec20[2];
	float fConst21;
	float fConst22;
	float fRec27[2];
	float fRec26[2];
	float fVec8[32768];
	float fConst23;
	int iConst24;
	float fVec9[4096];
	int iConst25;
	float fRec24[2];
	float fConst26;
	float fConst27;
	float fRec31[2];
	float fRec30[2];
	float fVec10[16384];
	float fConst28;
	int iConst29;
	float fVec11[2048];
	int iConst30;
	float fRec28[2];
	float fConst31;
	float fConst32;
	float fRec35[2];
	float fRec34[2];
	float fVec12[16384];
	float fConst33;
	int iConst34;
	float fVec13[4096];
	int iConst35;
	float fRec32[2];
	float fConst36;
	float fConst37;
	float fRec39[2];
	float fRec38[2];
	float fVec14[16384];
	float fConst38;
	int iConst39;
	float fVec15[4096];
	int iConst40;
	float fRec36[2];
	float fConst41;
	float fConst42;
	float fRec43[2];
	float fRec42[2];
	float fVec16[16384];
	float fConst43;
	int iConst44;
	float fVec17[2048];
	int iConst45;
	float fRec40[2];
	float fRec4[3];
	float fRec5[3];
	float fRec6[3];
	float fRec7[3];
	float fRec8[3];
	float fRec9[3];
	float fRec10[3];
	float fRec11[3];
	float fRec3[3];
	FAUSTFLOAT fVslider9;
	FAUSTFLOAT fVslider10;
	float fRec2[3];
	float fRec45[3];
	float fRec44[3];
	
 public:
	mydsp() {
	}
	
	mydsp(const mydsp&) = default;
	
	virtual ~mydsp() = default;
	
	mydsp& operator=(const mydsp&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.22.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("demos.lib/name", "Faust Demos Library");
		m->declare("demos.lib/version", "1.4.0");
		m->declare("demos.lib/zita_rev1:author", "Julius O. Smith III");
		m->declare("demos.lib/zita_rev1:licence", "MIT");
		m->declare("description", "Zita-rev1 FDN hall reverb (sk-engines faust engine).");
		m->declare("filename", "zita");
		m->declare("filters.lib/allpass_comb:author", "Julius O. Smith III");
		m->declare("filters.lib/allpass_comb:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/allpass_comb:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/peak_eq_rm:author", "Julius O. Smith III");
		m->declare("filters.lib/peak_eq_rm:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/peak_eq_rm:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf1:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf1s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1s:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "zita");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("reverbs.lib/name", "Faust Reverb Library");
		m->declare("reverbs.lib/version", "1.5.1");
		m->declare("routes.lib/hadamard:author", "Remy Muller, revised by Romain Michon");
		m->declare("routes.lib/name", "Faust Signal Routing Library");
		m->declare("routes.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.6.0");
	}

	virtual int getNumInputs() {
		return 2;
	}
	virtual int getNumOutputs() {
		return 2;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 44.1f / fConst0;
		fConst2 = 1.0f - fConst1;
		fConst3 = 6.2831855f / fConst0;
		fConst4 = std::floor(0.219991f * fConst0 + 0.5f);
		fConst5 = 6.9077554f * (fConst4 / fConst0);
		fConst6 = 3.1415927f / fConst0;
		fConst7 = std::floor(0.019123f * fConst0 + 0.5f);
		iConst8 = static_cast<int>(std::min<float>(16384.0f, std::max<float>(0.0f, fConst4 - fConst7)));
		fConst9 = 0.001f * fConst0;
		iConst10 = static_cast<int>(std::min<float>(1024.0f, std::max<float>(0.0f, fConst7 + -1.0f)));
		fConst11 = std::floor(0.256891f * fConst0 + 0.5f);
		fConst12 = 6.9077554f * (fConst11 / fConst0);
		fConst13 = std::floor(0.027333f * fConst0 + 0.5f);
		iConst14 = static_cast<int>(std::min<float>(16384.0f, std::max<float>(0.0f, fConst11 - fConst13)));
		iConst15 = static_cast<int>(std::min<float>(2048.0f, std::max<float>(0.0f, fConst13 + -1.0f)));
		fConst16 = std::floor(0.192303f * fConst0 + 0.5f);
		fConst17 = 6.9077554f * (fConst16 / fConst0);
		fConst18 = std::floor(0.029291f * fConst0 + 0.5f);
		iConst19 = static_cast<int>(std::min<float>(8192.0f, std::max<float>(0.0f, fConst16 - fConst18)));
		iConst20 = static_cast<int>(std::min<float>(2048.0f, std::max<float>(0.0f, fConst18 + -1.0f)));
		fConst21 = std::floor(0.210389f * fConst0 + 0.5f);
		fConst22 = 6.9077554f * (fConst21 / fConst0);
		fConst23 = std::floor(0.024421f * fConst0 + 0.5f);
		iConst24 = static_cast<int>(std::min<float>(16384.0f, std::max<float>(0.0f, fConst21 - fConst23)));
		iConst25 = static_cast<int>(std::min<float>(2048.0f, std::max<float>(0.0f, fConst23 + -1.0f)));
		fConst26 = std::floor(0.125f * fConst0 + 0.5f);
		fConst27 = 6.9077554f * (fConst26 / fConst0);
		fConst28 = std::floor(0.013458f * fConst0 + 0.5f);
		iConst29 = static_cast<int>(std::min<float>(8192.0f, std::max<float>(0.0f, fConst26 - fConst28)));
		iConst30 = static_cast<int>(std::min<float>(1024.0f, std::max<float>(0.0f, fConst28 + -1.0f)));
		fConst31 = std::floor(0.127837f * fConst0 + 0.5f);
		fConst32 = 6.9077554f * (fConst31 / fConst0);
		fConst33 = std::floor(0.031604f * fConst0 + 0.5f);
		iConst34 = static_cast<int>(std::min<float>(8192.0f, std::max<float>(0.0f, fConst31 - fConst33)));
		iConst35 = static_cast<int>(std::min<float>(2048.0f, std::max<float>(0.0f, fConst33 + -1.0f)));
		fConst36 = std::floor(0.174713f * fConst0 + 0.5f);
		fConst37 = 6.9077554f * (fConst36 / fConst0);
		fConst38 = std::floor(0.022904f * fConst0 + 0.5f);
		iConst39 = static_cast<int>(std::min<float>(8192.0f, std::max<float>(0.0f, fConst36 - fConst38)));
		iConst40 = static_cast<int>(std::min<float>(2048.0f, std::max<float>(0.0f, fConst38 + -1.0f)));
		fConst41 = std::floor(0.153129f * fConst0 + 0.5f);
		fConst42 = 6.9077554f * (fConst41 / fConst0);
		fConst43 = std::floor(0.020346f * fConst0 + 0.5f);
		iConst44 = static_cast<int>(std::min<float>(8192.0f, std::max<float>(0.0f, fConst41 - fConst43)));
		iConst45 = static_cast<int>(std::min<float>(1024.0f, std::max<float>(0.0f, fConst43 + -1.0f)));
	}
	
	virtual void instanceResetUserInterface() {
		fVslider0 = static_cast<FAUSTFLOAT>(-2e+01f);
		fVslider1 = static_cast<FAUSTFLOAT>(0.0f);
		fVslider2 = static_cast<FAUSTFLOAT>(315.0f);
		fVslider3 = static_cast<FAUSTFLOAT>(0.0f);
		fVslider4 = static_cast<FAUSTFLOAT>(2.0f);
		fVslider5 = static_cast<FAUSTFLOAT>(6e+03f);
		fVslider6 = static_cast<FAUSTFLOAT>(3.0f);
		fVslider7 = static_cast<FAUSTFLOAT>(2e+02f);
		fVslider8 = static_cast<FAUSTFLOAT>(6e+01f);
		fVslider9 = static_cast<FAUSTFLOAT>(1.5e+03f);
		fVslider10 = static_cast<FAUSTFLOAT>(0.0f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			fRec0[l0] = 0.0f;
		}
		IOTA0 = 0;
		for (int l1 = 0; l1 < 16384; l1 = l1 + 1) {
			fVec0[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec1[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec15[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec14[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 32768; l5 = l5 + 1) {
			fVec1[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 16384; l6 = l6 + 1) {
			fVec2[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2048; l7 = l7 + 1) {
			fVec3[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec12[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec19[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fRec18[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 32768; l11 = l11 + 1) {
			fVec4[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 4096; l12 = l12 + 1) {
			fVec5[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			fRec16[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = l14 + 1) {
			fRec23[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fRec22[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 16384; l16 = l16 + 1) {
			fVec6[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 4096; l17 = l17 + 1) {
			fVec7[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = l18 + 1) {
			fRec20[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = l19 + 1) {
			fRec27[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = l20 + 1) {
			fRec26[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 32768; l21 = l21 + 1) {
			fVec8[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 4096; l22 = l22 + 1) {
			fVec9[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = l23 + 1) {
			fRec24[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = l24 + 1) {
			fRec31[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = l25 + 1) {
			fRec30[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 16384; l26 = l26 + 1) {
			fVec10[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2048; l27 = l27 + 1) {
			fVec11[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2; l28 = l28 + 1) {
			fRec28[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = l29 + 1) {
			fRec35[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2; l30 = l30 + 1) {
			fRec34[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 16384; l31 = l31 + 1) {
			fVec12[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 4096; l32 = l32 + 1) {
			fVec13[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = l33 + 1) {
			fRec32[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 2; l34 = l34 + 1) {
			fRec39[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = l35 + 1) {
			fRec38[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 16384; l36 = l36 + 1) {
			fVec14[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 4096; l37 = l37 + 1) {
			fVec15[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = l38 + 1) {
			fRec36[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = l39 + 1) {
			fRec43[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = l40 + 1) {
			fRec42[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 16384; l41 = l41 + 1) {
			fVec16[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 2048; l42 = l42 + 1) {
			fVec17[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = l43 + 1) {
			fRec40[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 3; l44 = l44 + 1) {
			fRec4[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 3; l45 = l45 + 1) {
			fRec5[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 3; l46 = l46 + 1) {
			fRec6[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 3; l47 = l47 + 1) {
			fRec7[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 3; l48 = l48 + 1) {
			fRec8[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 3; l49 = l49 + 1) {
			fRec9[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 3; l50 = l50 + 1) {
			fRec10[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 3; l51 = l51 + 1) {
			fRec11[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 3; l52 = l52 + 1) {
			fRec3[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 3; l53 = l53 + 1) {
			fRec2[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 3; l54 = l54 + 1) {
			fRec45[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 3; l55 = l55 + 1) {
			fRec44[l55] = 0.0f;
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
	
	virtual mydsp* clone() {
		return new mydsp(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->declare(0, "0", "");
		ui_interface->declare(0, "tooltip", "~ ZITA REV1 FEEDBACK DELAY NETWORK (FDN) & SCHROEDER     ALLPASS-COMB REVERBERATOR (8x8). See Faust's reverbs.lib for documentation and     references");
		ui_interface->openHorizontalBox("Zita_Rev1");
		ui_interface->declare(0, "1", "");
		ui_interface->openHorizontalBox("Input");
		ui_interface->declare(&fVslider8, "1", "");
		ui_interface->declare(&fVslider8, "style", "knob");
		ui_interface->declare(&fVslider8, "tooltip", "Delay in ms         before reverberation begins");
		ui_interface->declare(&fVslider8, "unit", "ms");
		ui_interface->addVerticalSlider("In Delay", &fVslider8, FAUSTFLOAT(6e+01f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(1.0f));
		ui_interface->closeBox();
		ui_interface->declare(0, "2", "");
		ui_interface->openHorizontalBox("Decay Times in Bands (see tooltips)");
		ui_interface->declare(&fVslider7, "1", "");
		ui_interface->declare(&fVslider7, "scale", "log");
		ui_interface->declare(&fVslider7, "style", "knob");
		ui_interface->declare(&fVslider7, "tooltip", "Crossover frequency (Hz) separating low and middle frequencies");
		ui_interface->declare(&fVslider7, "unit", "Hz");
		ui_interface->addVerticalSlider("LF X", &fVslider7, FAUSTFLOAT(2e+02f), FAUSTFLOAT(5e+01f), FAUSTFLOAT(1e+03f), FAUSTFLOAT(1.0f));
		ui_interface->declare(&fVslider6, "2", "");
		ui_interface->declare(&fVslider6, "scale", "log");
		ui_interface->declare(&fVslider6, "style", "knob");
		ui_interface->declare(&fVslider6, "tooltip", "T60 = time (in seconds) to decay 60dB in low-frequency band");
		ui_interface->declare(&fVslider6, "unit", "s");
		ui_interface->addVerticalSlider("Low RT60", &fVslider6, FAUSTFLOAT(3.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(8.0f), FAUSTFLOAT(0.1f));
		ui_interface->declare(&fVslider4, "3", "");
		ui_interface->declare(&fVslider4, "scale", "log");
		ui_interface->declare(&fVslider4, "style", "knob");
		ui_interface->declare(&fVslider4, "tooltip", "T60 = time (in seconds) to decay 60dB in middle band");
		ui_interface->declare(&fVslider4, "unit", "s");
		ui_interface->addVerticalSlider("Mid RT60", &fVslider4, FAUSTFLOAT(2.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(8.0f), FAUSTFLOAT(0.1f));
		ui_interface->declare(&fVslider5, "4", "");
		ui_interface->declare(&fVslider5, "scale", "log");
		ui_interface->declare(&fVslider5, "style", "knob");
		ui_interface->declare(&fVslider5, "tooltip", "Frequency (Hz) at which the high-frequency T60 is half the middle-band's T60");
		ui_interface->declare(&fVslider5, "unit", "Hz");
		ui_interface->addVerticalSlider("HF Damping", &fVslider5, FAUSTFLOAT(6e+03f), FAUSTFLOAT(1.5e+03f), FAUSTFLOAT(2.352e+04f), FAUSTFLOAT(1.0f));
		ui_interface->closeBox();
		ui_interface->declare(0, "3", "");
		ui_interface->openHorizontalBox("RM Peaking Equalizer 1");
		ui_interface->declare(&fVslider2, "1", "");
		ui_interface->declare(&fVslider2, "scale", "log");
		ui_interface->declare(&fVslider2, "style", "knob");
		ui_interface->declare(&fVslider2, "tooltip", "Center-frequency of second-order Regalia-Mitra peaking equalizer section 1");
		ui_interface->declare(&fVslider2, "unit", "Hz");
		ui_interface->addVerticalSlider("Eq1 Freq", &fVslider2, FAUSTFLOAT(315.0f), FAUSTFLOAT(4e+01f), FAUSTFLOAT(2.5e+03f), FAUSTFLOAT(1.0f));
		ui_interface->declare(&fVslider3, "2", "");
		ui_interface->declare(&fVslider3, "style", "knob");
		ui_interface->declare(&fVslider3, "tooltip", "Peak level         in dB of second-order Regalia-Mitra peaking equalizer section 1");
		ui_interface->declare(&fVslider3, "unit", "dB");
		ui_interface->addVerticalSlider("Eq1 Level", &fVslider3, FAUSTFLOAT(0.0f), FAUSTFLOAT(-15.0f), FAUSTFLOAT(15.0f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
		ui_interface->declare(0, "4", "");
		ui_interface->openHorizontalBox("RM Peaking Equalizer 2");
		ui_interface->declare(&fVslider9, "1", "");
		ui_interface->declare(&fVslider9, "scale", "log");
		ui_interface->declare(&fVslider9, "style", "knob");
		ui_interface->declare(&fVslider9, "tooltip", "Center-frequency of second-order Regalia-Mitra peaking equalizer section 2");
		ui_interface->declare(&fVslider9, "unit", "Hz");
		ui_interface->addVerticalSlider("Eq2 Freq", &fVslider9, FAUSTFLOAT(1.5e+03f), FAUSTFLOAT(1.6e+02f), FAUSTFLOAT(1e+04f), FAUSTFLOAT(1.0f));
		ui_interface->declare(&fVslider10, "2", "");
		ui_interface->declare(&fVslider10, "style", "knob");
		ui_interface->declare(&fVslider10, "tooltip", "Peak level         in dB of second-order Regalia-Mitra peaking equalizer section 2");
		ui_interface->declare(&fVslider10, "unit", "dB");
		ui_interface->addVerticalSlider("Eq2 Level", &fVslider10, FAUSTFLOAT(0.0f), FAUSTFLOAT(-15.0f), FAUSTFLOAT(15.0f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
		ui_interface->declare(0, "5", "");
		ui_interface->openHorizontalBox("Output");
		ui_interface->declare(&fVslider1, "1", "");
		ui_interface->declare(&fVslider1, "style", "knob");
		ui_interface->declare(&fVslider1, "tooltip", "Ratio of dry and wet signal. -1 = fully wet, +1 = fully dry");
		ui_interface->addVerticalSlider("Wet/Dry Mix", &fVslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fVslider0, "2", "");
		ui_interface->declare(&fVslider0, "style", "knob");
		ui_interface->declare(&fVslider0, "tooltip", "Output scale         factor");
		ui_interface->declare(&fVslider0, "unit", "dB");
		ui_interface->addVerticalSlider("Level", &fVslider0, FAUSTFLOAT(-2e+01f), FAUSTFLOAT(-7e+01f), FAUSTFLOAT(4e+01f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst1 * std::pow(1e+01f, 0.05f * static_cast<float>(fVslider0));
		float fSlow1 = fConst1 * static_cast<float>(fVslider1);
		float fSlow2 = static_cast<float>(fVslider2);
		float fSlow3 = std::pow(1e+01f, 0.05f * static_cast<float>(fVslider3));
		float fSlow4 = fConst3 * (fSlow2 / std::sqrt(std::max<float>(0.0f, fSlow3)));
		float fSlow5 = (1.0f - fSlow4) / (fSlow4 + 1.0f);
		float fSlow6 = static_cast<float>(fVslider4);
		float fSlow7 = std::exp(-(fConst5 / fSlow6));
		float fSlow8 = std::cos(fConst3 * static_cast<float>(fVslider5));
		float fSlow9 = mydsp_faustpower2_f(fSlow7);
		float fSlow10 = 1.0f - fSlow8 * fSlow9;
		float fSlow11 = 1.0f - fSlow9;
		float fSlow12 = std::sqrt(std::max<float>(0.0f, mydsp_faustpower2_f(fSlow10) / mydsp_faustpower2_f(fSlow11) + -1.0f));
		float fSlow13 = fSlow10 / fSlow11;
		float fSlow14 = fSlow7 * (fSlow12 + (1.0f - fSlow13));
		float fSlow15 = static_cast<float>(fVslider6);
		float fSlow16 = std::exp(-(fConst5 / fSlow15)) / fSlow7 + -1.0f;
		float fSlow17 = 1.0f / std::tan(fConst6 * static_cast<float>(fVslider7));
		float fSlow18 = 1.0f / (fSlow17 + 1.0f);
		float fSlow19 = 1.0f - fSlow17;
		float fSlow20 = fSlow13 - fSlow12;
		int iSlow21 = static_cast<int>(std::min<float>(8192.0f, std::max<float>(0.0f, fConst9 * static_cast<float>(fVslider8))));
		float fSlow22 = std::exp(-(fConst12 / fSlow6));
		float fSlow23 = mydsp_faustpower2_f(fSlow22);
		float fSlow24 = 1.0f - fSlow8 * fSlow23;
		float fSlow25 = 1.0f - fSlow23;
		float fSlow26 = std::sqrt(std::max<float>(0.0f, mydsp_faustpower2_f(fSlow24) / mydsp_faustpower2_f(fSlow25) + -1.0f));
		float fSlow27 = fSlow24 / fSlow25;
		float fSlow28 = fSlow22 * (fSlow26 + (1.0f - fSlow27));
		float fSlow29 = std::exp(-(fConst12 / fSlow15)) / fSlow22 + -1.0f;
		float fSlow30 = fSlow27 - fSlow26;
		float fSlow31 = std::exp(-(fConst17 / fSlow6));
		float fSlow32 = mydsp_faustpower2_f(fSlow31);
		float fSlow33 = 1.0f - fSlow8 * fSlow32;
		float fSlow34 = 1.0f - fSlow32;
		float fSlow35 = std::sqrt(std::max<float>(0.0f, mydsp_faustpower2_f(fSlow33) / mydsp_faustpower2_f(fSlow34) + -1.0f));
		float fSlow36 = fSlow33 / fSlow34;
		float fSlow37 = fSlow31 * (fSlow35 + (1.0f - fSlow36));
		float fSlow38 = std::exp(-(fConst17 / fSlow15)) / fSlow31 + -1.0f;
		float fSlow39 = fSlow36 - fSlow35;
		float fSlow40 = std::exp(-(fConst22 / fSlow6));
		float fSlow41 = mydsp_faustpower2_f(fSlow40);
		float fSlow42 = 1.0f - fSlow8 * fSlow41;
		float fSlow43 = 1.0f - fSlow41;
		float fSlow44 = std::sqrt(std::max<float>(0.0f, mydsp_faustpower2_f(fSlow42) / mydsp_faustpower2_f(fSlow43) + -1.0f));
		float fSlow45 = fSlow42 / fSlow43;
		float fSlow46 = fSlow40 * (fSlow44 + (1.0f - fSlow45));
		float fSlow47 = std::exp(-(fConst22 / fSlow15)) / fSlow40 + -1.0f;
		float fSlow48 = fSlow45 - fSlow44;
		float fSlow49 = std::exp(-(fConst27 / fSlow6));
		float fSlow50 = mydsp_faustpower2_f(fSlow49);
		float fSlow51 = 1.0f - fSlow8 * fSlow50;
		float fSlow52 = 1.0f - fSlow50;
		float fSlow53 = std::sqrt(std::max<float>(0.0f, mydsp_faustpower2_f(fSlow51) / mydsp_faustpower2_f(fSlow52) + -1.0f));
		float fSlow54 = fSlow51 / fSlow52;
		float fSlow55 = fSlow49 * (fSlow53 + (1.0f - fSlow54));
		float fSlow56 = std::exp(-(fConst27 / fSlow15)) / fSlow49 + -1.0f;
		float fSlow57 = fSlow54 - fSlow53;
		float fSlow58 = std::exp(-(fConst32 / fSlow6));
		float fSlow59 = mydsp_faustpower2_f(fSlow58);
		float fSlow60 = 1.0f - fSlow8 * fSlow59;
		float fSlow61 = 1.0f - fSlow59;
		float fSlow62 = std::sqrt(std::max<float>(0.0f, mydsp_faustpower2_f(fSlow60) / mydsp_faustpower2_f(fSlow61) + -1.0f));
		float fSlow63 = fSlow60 / fSlow61;
		float fSlow64 = fSlow58 * (fSlow62 + (1.0f - fSlow63));
		float fSlow65 = std::exp(-(fConst32 / fSlow15)) / fSlow58 + -1.0f;
		float fSlow66 = fSlow63 - fSlow62;
		float fSlow67 = std::exp(-(fConst37 / fSlow6));
		float fSlow68 = mydsp_faustpower2_f(fSlow67);
		float fSlow69 = 1.0f - fSlow8 * fSlow68;
		float fSlow70 = 1.0f - fSlow68;
		float fSlow71 = std::sqrt(std::max<float>(0.0f, mydsp_faustpower2_f(fSlow69) / mydsp_faustpower2_f(fSlow70) + -1.0f));
		float fSlow72 = fSlow69 / fSlow70;
		float fSlow73 = fSlow67 * (fSlow71 + (1.0f - fSlow72));
		float fSlow74 = std::exp(-(fConst37 / fSlow15)) / fSlow67 + -1.0f;
		float fSlow75 = fSlow72 - fSlow71;
		float fSlow76 = std::exp(-(fConst42 / fSlow6));
		float fSlow77 = mydsp_faustpower2_f(fSlow76);
		float fSlow78 = 1.0f - fSlow77 * fSlow8;
		float fSlow79 = 1.0f - fSlow77;
		float fSlow80 = std::sqrt(std::max<float>(0.0f, mydsp_faustpower2_f(fSlow78) / mydsp_faustpower2_f(fSlow79) + -1.0f));
		float fSlow81 = fSlow78 / fSlow79;
		float fSlow82 = fSlow76 * (fSlow80 + (1.0f - fSlow81));
		float fSlow83 = std::exp(-(fConst42 / fSlow15)) / fSlow76 + -1.0f;
		float fSlow84 = fSlow81 - fSlow80;
		float fSlow85 = std::cos(fConst3 * fSlow2) * (fSlow5 + 1.0f);
		float fSlow86 = static_cast<float>(fVslider9);
		float fSlow87 = std::pow(1e+01f, 0.05f * static_cast<float>(fVslider10));
		float fSlow88 = fConst3 * (fSlow86 / std::sqrt(std::max<float>(0.0f, fSlow87)));
		float fSlow89 = (1.0f - fSlow88) / (fSlow88 + 1.0f);
		float fSlow90 = std::cos(fConst3 * fSlow86) * (fSlow89 + 1.0f);
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			fRec0[0] = fSlow0 + fConst2 * fRec0[1];
			float fTemp0 = static_cast<float>(input0[i0]);
			fVec0[IOTA0 & 16383] = fTemp0;
			fRec1[0] = fSlow1 + fConst2 * fRec1[1];
			float fTemp1 = fRec1[0] + 1.0f;
			float fTemp2 = 1.0f - 0.5f * fTemp1;
			fRec15[0] = -(fSlow18 * (fSlow19 * fRec15[1] - (fRec11[1] + fRec11[2])));
			fRec14[0] = fSlow14 * (fRec11[1] + fSlow16 * fRec15[0]) + fSlow20 * fRec14[1];
			fVec1[IOTA0 & 32767] = 0.35355338f * fRec14[0] + 1e-20f;
			float fTemp3 = 0.6f * fRec12[1] + fVec1[(IOTA0 - iConst8) & 32767];
			float fTemp4 = static_cast<float>(input1[i0]);
			fVec2[IOTA0 & 16383] = fTemp4;
			float fTemp5 = 0.3f * fVec2[(IOTA0 - iSlow21) & 16383];
			fVec3[IOTA0 & 2047] = fTemp3 - fTemp5;
			fRec12[0] = fVec3[(IOTA0 - iConst10) & 2047];
			float fRec13 = 0.6f * (fTemp5 - fTemp3);
			fRec19[0] = -(fSlow18 * (fSlow19 * fRec19[1] - (fRec7[1] + fRec7[2])));
			fRec18[0] = fSlow28 * (fRec7[1] + fSlow29 * fRec19[0]) + fSlow30 * fRec18[1];
			fVec4[IOTA0 & 32767] = 0.35355338f * fRec18[0] + 1e-20f;
			float fTemp6 = 0.6f * fRec16[1] + fVec4[(IOTA0 - iConst14) & 32767];
			fVec5[IOTA0 & 4095] = fTemp6 - fTemp5;
			fRec16[0] = fVec5[(IOTA0 - iConst15) & 4095];
			float fRec17 = 0.6f * (fTemp5 - fTemp6);
			fRec23[0] = -(fSlow18 * (fSlow19 * fRec23[1] - (fRec9[1] + fRec9[2])));
			fRec22[0] = fSlow37 * (fRec9[1] + fSlow38 * fRec23[0]) + fSlow39 * fRec22[1];
			fVec6[IOTA0 & 16383] = 0.35355338f * fRec22[0] + 1e-20f;
			float fTemp7 = fVec6[(IOTA0 - iConst19) & 16383] + fTemp5 + 0.6f * fRec20[1];
			fVec7[IOTA0 & 4095] = fTemp7;
			fRec20[0] = fVec7[(IOTA0 - iConst20) & 4095];
			float fRec21 = -(0.6f * fTemp7);
			fRec27[0] = -(fSlow18 * (fSlow19 * fRec27[1] - (fRec5[1] + fRec5[2])));
			fRec26[0] = fSlow46 * (fRec5[1] + fSlow47 * fRec27[0]) + fSlow48 * fRec26[1];
			fVec8[IOTA0 & 32767] = 0.35355338f * fRec26[0] + 1e-20f;
			float fTemp8 = fTemp5 + 0.6f * fRec24[1] + fVec8[(IOTA0 - iConst24) & 32767];
			fVec9[IOTA0 & 4095] = fTemp8;
			fRec24[0] = fVec9[(IOTA0 - iConst25) & 4095];
			float fRec25 = -(0.6f * fTemp8);
			fRec31[0] = -(fSlow18 * (fSlow19 * fRec31[1] - (fRec10[1] + fRec10[2])));
			fRec30[0] = fSlow55 * (fRec10[1] + fSlow56 * fRec31[0]) + fSlow57 * fRec30[1];
			fVec10[IOTA0 & 16383] = 0.35355338f * fRec30[0] + 1e-20f;
			float fTemp9 = 0.3f * fVec0[(IOTA0 - iSlow21) & 16383];
			float fTemp10 = fVec10[(IOTA0 - iConst29) & 16383] - (fTemp9 + 0.6f * fRec28[1]);
			fVec11[IOTA0 & 2047] = fTemp10;
			fRec28[0] = fVec11[(IOTA0 - iConst30) & 2047];
			float fRec29 = 0.6f * fTemp10;
			fRec35[0] = -(fSlow18 * (fSlow19 * fRec35[1] - (fRec6[1] + fRec6[2])));
			fRec34[0] = fSlow64 * (fRec6[1] + fSlow65 * fRec35[0]) + fSlow66 * fRec34[1];
			fVec12[IOTA0 & 16383] = 0.35355338f * fRec34[0] + 1e-20f;
			float fTemp11 = fVec12[(IOTA0 - iConst34) & 16383] - (fTemp9 + 0.6f * fRec32[1]);
			fVec13[IOTA0 & 4095] = fTemp11;
			fRec32[0] = fVec13[(IOTA0 - iConst35) & 4095];
			float fRec33 = 0.6f * fTemp11;
			fRec39[0] = -(fSlow18 * (fSlow19 * fRec39[1] - (fRec8[1] + fRec8[2])));
			fRec38[0] = fSlow73 * (fRec8[1] + fSlow74 * fRec39[0]) + fSlow75 * fRec38[1];
			fVec14[IOTA0 & 16383] = 0.35355338f * fRec38[0] + 1e-20f;
			float fTemp12 = fTemp9 + fVec14[(IOTA0 - iConst39) & 16383] - 0.6f * fRec36[1];
			fVec15[IOTA0 & 4095] = fTemp12;
			fRec36[0] = fVec15[(IOTA0 - iConst40) & 4095];
			float fRec37 = 0.6f * fTemp12;
			fRec43[0] = -(fSlow18 * (fSlow19 * fRec43[1] - (fRec4[1] + fRec4[2])));
			fRec42[0] = fSlow82 * (fRec4[1] + fSlow83 * fRec43[0]) + fSlow84 * fRec42[1];
			fVec16[IOTA0 & 16383] = 0.35355338f * fRec42[0] + 1e-20f;
			float fTemp13 = fVec16[(IOTA0 - iConst44) & 16383] + fTemp9 - 0.6f * fRec40[1];
			fVec17[IOTA0 & 2047] = fTemp13;
			fRec40[0] = fVec17[(IOTA0 - iConst45) & 2047];
			float fRec41 = 0.6f * fTemp13;
			float fTemp14 = fRec41 + fRec37;
			float fTemp15 = fRec29 + fRec33 + fTemp14;
			fRec4[0] = fRec12[1] + fRec16[1] + fRec20[1] + fRec24[1] + fRec28[1] + fRec32[1] + fRec36[1] + fRec40[1] + fRec13 + fRec17 + fRec21 + fRec25 + fTemp15;
			fRec5[0] = fRec28[1] + fRec32[1] + fRec36[1] + fRec40[1] + fTemp15 - (fRec12[1] + fRec16[1] + fRec20[1] + fRec24[1] + fRec13 + fRec17 + fRec25 + fRec21);
			float fTemp16 = fRec33 + fRec29;
			fRec6[0] = fRec20[1] + fRec24[1] + fRec36[1] + fRec40[1] + fRec21 + fRec25 + fTemp14 - (fRec12[1] + fRec16[1] + fRec28[1] + fRec32[1] + fRec13 + fRec17 + fTemp16);
			fRec7[0] = fRec12[1] + fRec16[1] + fRec36[1] + fRec40[1] + fRec13 + fRec17 + fTemp14 - (fRec20[1] + fRec24[1] + fRec28[1] + fRec32[1] + fRec21 + fRec25 + fTemp16);
			float fTemp17 = fRec41 + fRec33;
			float fTemp18 = fRec37 + fRec29;
			fRec8[0] = fRec16[1] + fRec24[1] + fRec32[1] + fRec40[1] + fRec17 + fRec25 + fTemp17 - (fRec12[1] + fRec20[1] + fRec28[1] + fRec36[1] + fRec13 + fRec21 + fTemp18);
			fRec9[0] = fRec12[1] + fRec20[1] + fRec32[1] + fRec40[1] + fRec13 + fRec21 + fTemp17 - (fRec16[1] + fRec24[1] + fRec28[1] + fRec36[1] + fRec17 + fRec25 + fTemp18);
			float fTemp19 = fRec41 + fRec29;
			float fTemp20 = fRec37 + fRec33;
			fRec10[0] = fRec12[1] + fRec24[1] + fRec28[1] + fRec40[1] + fRec13 + fRec25 + fTemp19 - (fRec16[1] + fRec20[1] + fRec32[1] + fRec36[1] + fRec17 + fRec21 + fTemp20);
			fRec11[0] = fRec16[1] + fRec20[1] + fRec28[1] + fRec40[1] + fRec17 + fRec21 + fTemp19 - (fRec12[1] + fRec24[1] + fRec32[1] + fRec36[1] + fRec13 + fRec25 + fTemp20);
			float fTemp21 = 0.37f * (fRec5[0] + fRec6[0]);
			float fTemp22 = fSlow85 * fRec3[1];
			float fTemp23 = fTemp21 + fTemp22;
			fRec3[0] = fTemp23 - fSlow5 * fRec3[2];
			float fTemp24 = fSlow5 * fRec3[0];
			float fTemp25 = fTemp24 + fTemp21 + fRec3[2];
			float fTemp26 = fSlow3 * (fRec3[2] + fTemp24 - fTemp23);
			float fTemp27 = fSlow90 * fRec2[1];
			fRec2[0] = 0.5f * (fTemp25 - fTemp22 + fTemp26) + fTemp27 - fSlow89 * fRec2[2];
			float fTemp28 = fRec2[2] + fSlow89 * fRec2[0];
			float fTemp29 = 0.5f * (fTemp25 + fTemp26 - fTemp22);
			output0[i0] = static_cast<FAUSTFLOAT>(0.5f * fRec0[0] * (fTemp0 * fTemp1 + fTemp2 * (fTemp28 + fTemp29 + fSlow87 * (fTemp28 - (fTemp27 + fTemp29)) - fTemp27)));
			float fTemp30 = 0.37f * (fRec5[0] - fRec6[0]);
			float fTemp31 = fSlow85 * fRec45[1];
			float fTemp32 = fTemp30 + fTemp31;
			fRec45[0] = fTemp32 - fSlow5 * fRec45[2];
			float fTemp33 = fSlow5 * fRec45[0];
			float fTemp34 = fTemp33 + fTemp30 + fRec45[2];
			float fTemp35 = fSlow3 * (fRec45[2] + fTemp33 - fTemp32);
			float fTemp36 = fSlow90 * fRec44[1];
			fRec44[0] = 0.5f * (fTemp34 - fTemp31 + fTemp35) + fTemp36 - fSlow89 * fRec44[2];
			float fTemp37 = fRec44[2] + fSlow89 * fRec44[0];
			float fTemp38 = 0.5f * (fTemp34 + fTemp35 - fTemp31);
			output1[i0] = static_cast<FAUSTFLOAT>(0.5f * fRec0[0] * (fTemp4 * fTemp1 + fTemp2 * (fTemp37 + fTemp38 + fSlow87 * (fTemp37 - (fTemp36 + fTemp38)) - fTemp36)));
			fRec0[1] = fRec0[0];
			IOTA0 = IOTA0 + 1;
			fRec1[1] = fRec1[0];
			fRec15[1] = fRec15[0];
			fRec14[1] = fRec14[0];
			fRec12[1] = fRec12[0];
			fRec19[1] = fRec19[0];
			fRec18[1] = fRec18[0];
			fRec16[1] = fRec16[0];
			fRec23[1] = fRec23[0];
			fRec22[1] = fRec22[0];
			fRec20[1] = fRec20[0];
			fRec27[1] = fRec27[0];
			fRec26[1] = fRec26[0];
			fRec24[1] = fRec24[0];
			fRec31[1] = fRec31[0];
			fRec30[1] = fRec30[0];
			fRec28[1] = fRec28[0];
			fRec35[1] = fRec35[0];
			fRec34[1] = fRec34[0];
			fRec32[1] = fRec32[0];
			fRec39[1] = fRec39[0];
			fRec38[1] = fRec38[0];
			fRec36[1] = fRec36[0];
			fRec43[1] = fRec43[0];
			fRec42[1] = fRec42[0];
			fRec40[1] = fRec40[0];
			fRec4[2] = fRec4[1];
			fRec4[1] = fRec4[0];
			fRec5[2] = fRec5[1];
			fRec5[1] = fRec5[0];
			fRec6[2] = fRec6[1];
			fRec6[1] = fRec6[0];
			fRec7[2] = fRec7[1];
			fRec7[1] = fRec7[0];
			fRec8[2] = fRec8[1];
			fRec8[1] = fRec8[0];
			fRec9[2] = fRec9[1];
			fRec9[1] = fRec9[0];
			fRec10[2] = fRec10[1];
			fRec10[1] = fRec10[0];
			fRec11[2] = fRec11[1];
			fRec11[1] = fRec11[0];
			fRec3[2] = fRec3[1];
			fRec3[1] = fRec3[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec45[2] = fRec45[1];
			fRec45[1] = fRec45[0];
			fRec44[2] = fRec44[1];
			fRec44[1] = fRec44[0];
		}
	}

};

#endif
} } // namespace spotykach::rv_zita
