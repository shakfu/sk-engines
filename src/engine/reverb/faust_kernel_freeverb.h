// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// GENERATED FILE - do not edit by hand. Regenerate with `make faust-kernels` (cyfaust cpp backend).
// Source: src/engine/reverb/freeverb.dsp. The generated `class mydsp` is wrapped in namespace spotykach::rv_freeverb; its
// dsp/UI/Meta base types resolve to the shared arch shim (see engine/faust_arch.h).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#include "engine/faust_arch.h"

namespace spotykach { namespace rv_freeverb {
/* ------------------------------------------------------------
name: "freeverb"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __rv_freeverb_H__
#define  __rv_freeverb_H__

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


class mydsp : public dsp {
	
 private:
	
	int IOTA0;
	float fVec0[8192];
	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider0;
	float fConst2;
	float fRec0[2];
	float fConst3;
	FAUSTFLOAT fHslider1;
	float fRec2[2];
	float fConst4;
	FAUSTFLOAT fHslider2;
	float fRec12[2];
	float fVec1[8192];
	FAUSTFLOAT fHslider3;
	float fRec14[2];
	float fRec13[2];
	FAUSTFLOAT fHslider4;
	float fRec15[2];
	float fVec2[8192];
	int iConst5;
	int iConst6;
	float fRec11[2];
	float fRec17[2];
	float fVec3[8192];
	int iConst7;
	int iConst8;
	float fRec16[2];
	float fRec19[2];
	float fVec4[8192];
	int iConst9;
	int iConst10;
	float fRec18[2];
	float fRec21[2];
	float fVec5[8192];
	int iConst11;
	int iConst12;
	float fRec20[2];
	float fRec23[2];
	float fVec6[8192];
	int iConst13;
	int iConst14;
	float fRec22[2];
	float fRec25[2];
	float fVec7[8192];
	int iConst15;
	int iConst16;
	float fRec24[2];
	float fRec27[2];
	float fVec8[8192];
	int iConst17;
	int iConst18;
	float fRec26[2];
	float fRec29[2];
	float fVec9[8192];
	int iConst19;
	int iConst20;
	float fRec28[2];
	float fVec10[2048];
	int iConst21;
	int iConst22;
	float fRec9[2];
	float fVec11[2048];
	int iConst23;
	int iConst24;
	float fRec7[2];
	float fVec12[2048];
	int iConst25;
	int iConst26;
	float fRec5[2];
	float fVec13[1024];
	int iConst27;
	int iConst28;
	float fRec3[2];
	float fVec14[2];
	float fRec1[2];
	float fRec40[2];
	float fVec15[8192];
	float fConst29;
	FAUSTFLOAT fHslider5;
	float fRec39[2];
	float fRec42[2];
	float fVec16[8192];
	float fConst30;
	float fRec41[2];
	float fRec44[2];
	float fVec17[8192];
	float fConst31;
	float fRec43[2];
	float fRec46[2];
	float fVec18[8192];
	float fConst32;
	float fRec45[2];
	float fRec48[2];
	float fVec19[8192];
	float fConst33;
	float fRec47[2];
	float fRec50[2];
	float fVec20[8192];
	float fConst34;
	float fRec49[2];
	float fRec52[2];
	float fVec21[8192];
	float fConst35;
	float fRec51[2];
	float fRec54[2];
	float fVec22[8192];
	float fConst36;
	float fRec53[2];
	float fVec23[2048];
	float fConst37;
	float fRec37[2];
	float fVec24[2048];
	float fConst38;
	float fRec35[2];
	float fVec25[2048];
	float fConst39;
	float fRec33[2];
	float fVec26[1024];
	float fConst40;
	float fRec31[2];
	float fVec27[2];
	float fRec30[2];
	
 public:
	mydsp() {
	}
	
	mydsp(const mydsp&) = default;
	
	virtual ~mydsp() = default;
	
	mydsp& operator=(const mydsp&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.2.0");
		m->declare("description", "Freeverb (Schroeder-Moorer) reverb (sk-engines faust engine).");
		m->declare("filename", "freeverb");
		m->declare("filters.lib/allpass_comb:author", "Julius O. Smith III");
		m->declare("filters.lib/allpass_comb:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/allpass_comb:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/tf1:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf1s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1s:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "freeverb");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("reverbs.lib/mono_freeverb:author", "Romain Michon");
		m->declare("reverbs.lib/name", "Faust Reverb Library");
		m->declare("reverbs.lib/stereo_freeverb:author", "Romain Michon");
		m->declare("reverbs.lib/version", "1.5.1");
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
		fConst3 = 1256.6371f / fConst0;
		fConst4 = 0.05f * fConst0;
		iConst5 = static_cast<int>(0.036666665f * fConst0);
		iConst6 = std::max<int>(0, iConst5 + -1);
		iConst7 = static_cast<int>(0.035306122f * fConst0);
		iConst8 = std::max<int>(0, iConst7 + -1);
		iConst9 = static_cast<int>(0.033809524f * fConst0);
		iConst10 = std::max<int>(0, iConst9 + -1);
		iConst11 = static_cast<int>(0.0322449f * fConst0);
		iConst12 = std::max<int>(0, iConst11 + -1);
		iConst13 = static_cast<int>(0.030748298f * fConst0);
		iConst14 = std::max<int>(0, iConst13 + -1);
		iConst15 = static_cast<int>(0.028956916f * fConst0);
		iConst16 = std::max<int>(0, iConst15 + -1);
		iConst17 = static_cast<int>(0.026938776f * fConst0);
		iConst18 = std::max<int>(0, iConst17 + -1);
		iConst19 = static_cast<int>(0.025306122f * fConst0);
		iConst20 = std::max<int>(0, iConst19 + -1);
		iConst21 = static_cast<int>(0.0126077095f * fConst0);
		iConst22 = std::min<int>(1024, std::max<int>(0, iConst21 + -1));
		iConst23 = static_cast<int>(0.01f * fConst0);
		iConst24 = std::min<int>(1024, std::max<int>(0, iConst23 + -1));
		iConst25 = static_cast<int>(0.0077324263f * fConst0);
		iConst26 = std::min<int>(1024, std::max<int>(0, iConst25 + -1));
		iConst27 = static_cast<int>(0.0051020407f * fConst0);
		iConst28 = std::min<int>(1024, std::max<int>(0, iConst27 + -1));
		fConst29 = static_cast<float>(iConst5);
		fConst30 = static_cast<float>(iConst7);
		fConst31 = static_cast<float>(iConst9);
		fConst32 = static_cast<float>(iConst11);
		fConst33 = static_cast<float>(iConst13);
		fConst34 = static_cast<float>(iConst15);
		fConst35 = static_cast<float>(iConst17);
		fConst36 = static_cast<float>(iConst19);
		fConst37 = static_cast<float>(iConst21);
		fConst38 = static_cast<float>(iConst23);
		fConst39 = static_cast<float>(iConst25);
		fConst40 = static_cast<float>(iConst27);
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.33f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.85f);
		fHslider2 = static_cast<FAUSTFLOAT>(0.1f);
		fHslider3 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider4 = static_cast<FAUSTFLOAT>(0.7f);
		fHslider5 = static_cast<FAUSTFLOAT>(0.5f);
	}
	
	virtual void instanceClear() {
		IOTA0 = 0;
		for (int l0 = 0; l0 < 8192; l0 = l0 + 1) {
			fVec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec0[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec2[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec12[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 8192; l4 = l4 + 1) {
			fVec1[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec14[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec13[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec15[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 8192; l8 = l8 + 1) {
			fVec2[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec11[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fRec17[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 8192; l11 = l11 + 1) {
			fVec3[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec16[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			fRec19[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 8192; l14 = l14 + 1) {
			fVec4[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fRec18[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = l16 + 1) {
			fRec21[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 8192; l17 = l17 + 1) {
			fVec5[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = l18 + 1) {
			fRec20[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = l19 + 1) {
			fRec23[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 8192; l20 = l20 + 1) {
			fVec6[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = l21 + 1) {
			fRec22[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = l22 + 1) {
			fRec25[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 8192; l23 = l23 + 1) {
			fVec7[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = l24 + 1) {
			fRec24[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = l25 + 1) {
			fRec27[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 8192; l26 = l26 + 1) {
			fVec8[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2; l27 = l27 + 1) {
			fRec26[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2; l28 = l28 + 1) {
			fRec29[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 8192; l29 = l29 + 1) {
			fVec9[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2; l30 = l30 + 1) {
			fRec28[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2048; l31 = l31 + 1) {
			fVec10[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2; l32 = l32 + 1) {
			fRec9[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2048; l33 = l33 + 1) {
			fVec11[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 2; l34 = l34 + 1) {
			fRec7[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2048; l35 = l35 + 1) {
			fVec12[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = l36 + 1) {
			fRec5[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 1024; l37 = l37 + 1) {
			fVec13[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = l38 + 1) {
			fRec3[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = l39 + 1) {
			fVec14[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2; l40 = l40 + 1) {
			fRec1[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = l41 + 1) {
			fRec40[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 8192; l42 = l42 + 1) {
			fVec15[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = l43 + 1) {
			fRec39[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 2; l44 = l44 + 1) {
			fRec42[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 8192; l45 = l45 + 1) {
			fVec16[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 2; l46 = l46 + 1) {
			fRec41[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 2; l47 = l47 + 1) {
			fRec44[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 8192; l48 = l48 + 1) {
			fVec17[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 2; l49 = l49 + 1) {
			fRec43[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 2; l50 = l50 + 1) {
			fRec46[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 8192; l51 = l51 + 1) {
			fVec18[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 2; l52 = l52 + 1) {
			fRec45[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 2; l53 = l53 + 1) {
			fRec48[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 8192; l54 = l54 + 1) {
			fVec19[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 2; l55 = l55 + 1) {
			fRec47[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 2; l56 = l56 + 1) {
			fRec50[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 8192; l57 = l57 + 1) {
			fVec20[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 2; l58 = l58 + 1) {
			fRec49[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = l59 + 1) {
			fRec52[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 8192; l60 = l60 + 1) {
			fVec21[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = l61 + 1) {
			fRec51[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 2; l62 = l62 + 1) {
			fRec54[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 8192; l63 = l63 + 1) {
			fVec22[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 2; l64 = l64 + 1) {
			fRec53[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 2048; l65 = l65 + 1) {
			fVec23[l65] = 0.0f;
		}
		for (int l66 = 0; l66 < 2; l66 = l66 + 1) {
			fRec37[l66] = 0.0f;
		}
		for (int l67 = 0; l67 < 2048; l67 = l67 + 1) {
			fVec24[l67] = 0.0f;
		}
		for (int l68 = 0; l68 < 2; l68 = l68 + 1) {
			fRec35[l68] = 0.0f;
		}
		for (int l69 = 0; l69 < 2048; l69 = l69 + 1) {
			fVec25[l69] = 0.0f;
		}
		for (int l70 = 0; l70 < 2; l70 = l70 + 1) {
			fRec33[l70] = 0.0f;
		}
		for (int l71 = 0; l71 < 1024; l71 = l71 + 1) {
			fVec26[l71] = 0.0f;
		}
		for (int l72 = 0; l72 < 2; l72 = l72 + 1) {
			fRec31[l72] = 0.0f;
		}
		for (int l73 = 0; l73 < 2; l73 = l73 + 1) {
			fVec27[l73] = 0.0f;
		}
		for (int l74 = 0; l74 < 2; l74 = l74 + 1) {
			fRec30[l74] = 0.0f;
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
		ui_interface->openVerticalBox("freeverb");
		ui_interface->addHorizontalSlider("Damp", &fHslider3, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("Mix", &fHslider0, FAUSTFLOAT(0.33f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("PreDelay", &fHslider2, FAUSTFLOAT(0.1f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("RoomSize", &fHslider4, FAUSTFLOAT(0.7f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("Spread", &fHslider5, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("Tone", &fHslider1, FAUSTFLOAT(0.85f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst1 * static_cast<float>(fHslider0);
		float fSlow1 = fConst1 * static_cast<float>(fHslider1);
		float fSlow2 = fConst1 * static_cast<float>(fHslider2);
		float fSlow3 = fConst1 * static_cast<float>(fHslider3);
		float fSlow4 = fConst1 * static_cast<float>(fHslider4);
		float fSlow5 = 23.0f * static_cast<float>(fHslider5) + -1.0f;
		int iSlow6 = static_cast<int>(std::max<float>(0.0f, fConst29 + fSlow5));
		int iSlow7 = static_cast<int>(std::max<float>(0.0f, fConst30 + fSlow5));
		int iSlow8 = static_cast<int>(std::max<float>(0.0f, fConst31 + fSlow5));
		int iSlow9 = static_cast<int>(std::max<float>(0.0f, fConst32 + fSlow5));
		int iSlow10 = static_cast<int>(std::max<float>(0.0f, fConst33 + fSlow5));
		int iSlow11 = static_cast<int>(std::max<float>(0.0f, fConst34 + fSlow5));
		int iSlow12 = static_cast<int>(std::max<float>(0.0f, fConst35 + fSlow5));
		int iSlow13 = static_cast<int>(std::max<float>(0.0f, fConst36 + fSlow5));
		int iSlow14 = static_cast<int>(std::min<float>(1024.0f, std::max<float>(0.0f, fConst37 + fSlow5)));
		int iSlow15 = static_cast<int>(std::min<float>(1024.0f, std::max<float>(0.0f, fConst38 + fSlow5)));
		int iSlow16 = static_cast<int>(std::min<float>(1024.0f, std::max<float>(0.0f, fConst39 + fSlow5)));
		int iSlow17 = static_cast<int>(std::min<float>(1024.0f, std::max<float>(0.0f, fConst40 + fSlow5)));
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			float fTemp0 = static_cast<float>(input0[i0]);
			fVec0[IOTA0 & 8191] = fTemp0;
			fRec0[0] = fSlow0 + fConst2 * fRec0[1];
			float fTemp1 = 1.0f - fRec0[0];
			fRec2[0] = fSlow1 + fConst2 * fRec2[1];
			float fTemp2 = 1.0f / std::tan(fConst3 * std::pow(2.0f, 5.3f * fRec2[0]));
			float fTemp3 = 1.0f - fTemp2;
			fRec12[0] = fSlow2 + fConst2 * fRec12[1];
			float fTemp4 = fConst4 * fRec12[0];
			float fTemp5 = std::floor(fTemp4);
			int iTemp6 = static_cast<int>(fTemp4);
			int iTemp7 = std::min<int>(4097, std::max<int>(0, iTemp6));
			float fTemp8 = static_cast<float>(input1[i0]);
			fVec1[IOTA0 & 8191] = fTemp8;
			float fTemp9 = (fTemp5 + (1.0f - fTemp4)) * (fVec0[(IOTA0 - iTemp7) & 8191] + fVec1[(IOTA0 - iTemp7) & 8191]);
			int iTemp10 = std::min<int>(4097, std::max<int>(0, iTemp6 + 1));
			float fTemp11 = (fTemp4 - fTemp5) * (fVec0[(IOTA0 - iTemp10) & 8191] + fVec1[(IOTA0 - iTemp10) & 8191]);
			float fTemp12 = fTemp9 + fTemp11;
			fRec14[0] = fSlow3 + fConst2 * fRec14[1];
			float fTemp13 = 1.0f - fRec14[0];
			fRec13[0] = fRec14[0] * fRec13[1] + fTemp13 * fRec11[1];
			fRec15[0] = fSlow4 + fConst2 * fRec15[1];
			float fTemp14 = 0.28f * fRec15[0] + 0.7f;
			fVec2[IOTA0 & 8191] = fTemp12 + fRec13[0] * fTemp14;
			fRec11[0] = fVec2[(IOTA0 - iConst6) & 8191];
			fRec17[0] = fRec14[0] * fRec17[1] + fTemp13 * fRec16[1];
			fVec3[IOTA0 & 8191] = fTemp12 + fRec17[0] * fTemp14;
			fRec16[0] = fVec3[(IOTA0 - iConst8) & 8191];
			fRec19[0] = fRec14[0] * fRec19[1] + fTemp13 * fRec18[1];
			fVec4[IOTA0 & 8191] = fTemp12 + fRec19[0] * fTemp14;
			fRec18[0] = fVec4[(IOTA0 - iConst10) & 8191];
			fRec21[0] = fRec14[0] * fRec21[1] + fTemp13 * fRec20[1];
			fVec5[IOTA0 & 8191] = fTemp12 + fRec21[0] * fTemp14;
			fRec20[0] = fVec5[(IOTA0 - iConst12) & 8191];
			fRec23[0] = fRec14[0] * fRec23[1] + fTemp13 * fRec22[1];
			fVec6[IOTA0 & 8191] = fTemp12 + fRec23[0] * fTemp14;
			fRec22[0] = fVec6[(IOTA0 - iConst14) & 8191];
			fRec25[0] = fRec14[0] * fRec25[1] + fTemp13 * fRec24[1];
			fVec7[IOTA0 & 8191] = fTemp12 + fRec25[0] * fTemp14;
			fRec24[0] = fVec7[(IOTA0 - iConst16) & 8191];
			fRec27[0] = fRec14[0] * fRec27[1] + fTemp13 * fRec26[1];
			fVec8[IOTA0 & 8191] = fTemp12 + fRec27[0] * fTemp14;
			fRec26[0] = fVec8[(IOTA0 - iConst18) & 8191];
			fRec29[0] = fRec14[0] * fRec29[1] + fTemp13 * fRec28[1];
			fVec9[IOTA0 & 8191] = fTemp11 + fRec29[0] * fTemp14 + fTemp9;
			fRec28[0] = fVec9[(IOTA0 - iConst20) & 8191];
			float fTemp15 = fRec11[1] + fRec16[1] + fRec18[1] + fRec20[1] + fRec22[1] + fRec24[1] + fRec26[1] + 0.5f * fRec9[1] + fRec28[1];
			fVec10[IOTA0 & 2047] = fTemp15;
			fRec9[0] = fVec10[(IOTA0 - iConst22) & 2047];
			float fRec10 = -(0.5f * fTemp15);
			float fTemp16 = fRec9[1] + fRec10 + 0.5f * fRec7[1];
			fVec11[IOTA0 & 2047] = fTemp16;
			fRec7[0] = fVec11[(IOTA0 - iConst24) & 2047];
			float fRec8 = -(0.5f * fTemp16);
			float fTemp17 = fRec7[1] + fRec8 + 0.5f * fRec5[1];
			fVec12[IOTA0 & 2047] = fTemp17;
			fRec5[0] = fVec12[(IOTA0 - iConst26) & 2047];
			float fRec6 = -(0.5f * fTemp17);
			float fTemp18 = fRec5[1] + fRec6 + 0.5f * fRec3[1];
			fVec13[IOTA0 & 1023] = fTemp18;
			fRec3[0] = fVec13[(IOTA0 - iConst28) & 1023];
			float fRec4 = -(0.5f * fTemp18);
			float fTemp19 = fRec4 + fRec3[1];
			fVec14[0] = fTemp19;
			float fTemp20 = fTemp2 + 1.0f;
			fRec1[0] = -((fRec1[1] * fTemp3 - (fTemp19 + fVec14[1])) / fTemp20);
			output0[i0] = static_cast<FAUSTFLOAT>(fTemp0 * fTemp1 + fRec0[0] * fRec1[0]);
			fRec40[0] = fRec14[0] * fRec40[1] + fTemp13 * fRec39[1];
			fVec15[IOTA0 & 8191] = fTemp12 + fRec40[0] * fTemp14;
			fRec39[0] = fVec15[(IOTA0 - iSlow6) & 8191];
			fRec42[0] = fRec14[0] * fRec42[1] + fTemp13 * fRec41[1];
			fVec16[IOTA0 & 8191] = fTemp12 + fRec42[0] * fTemp14;
			fRec41[0] = fVec16[(IOTA0 - iSlow7) & 8191];
			fRec44[0] = fRec14[0] * fRec44[1] + fTemp13 * fRec43[1];
			fVec17[IOTA0 & 8191] = fTemp12 + fRec44[0] * fTemp14;
			fRec43[0] = fVec17[(IOTA0 - iSlow8) & 8191];
			fRec46[0] = fRec14[0] * fRec46[1] + fTemp13 * fRec45[1];
			fVec18[IOTA0 & 8191] = fTemp12 + fRec46[0] * fTemp14;
			fRec45[0] = fVec18[(IOTA0 - iSlow9) & 8191];
			fRec48[0] = fRec14[0] * fRec48[1] + fTemp13 * fRec47[1];
			fVec19[IOTA0 & 8191] = fTemp12 + fRec48[0] * fTemp14;
			fRec47[0] = fVec19[(IOTA0 - iSlow10) & 8191];
			fRec50[0] = fRec14[0] * fRec50[1] + fTemp13 * fRec49[1];
			fVec20[IOTA0 & 8191] = fTemp12 + fRec50[0] * fTemp14;
			fRec49[0] = fVec20[(IOTA0 - iSlow11) & 8191];
			fRec52[0] = fRec14[0] * fRec52[1] + fTemp13 * fRec51[1];
			fVec21[IOTA0 & 8191] = fTemp12 + fRec52[0] * fTemp14;
			fRec51[0] = fVec21[(IOTA0 - iSlow12) & 8191];
			fRec54[0] = fRec14[0] * fRec54[1] + fTemp13 * fRec53[1];
			fVec22[IOTA0 & 8191] = fTemp12 + fRec54[0] * fTemp14;
			fRec53[0] = fVec22[(IOTA0 - iSlow13) & 8191];
			float fTemp21 = fRec39[1] + fRec41[1] + fRec43[1] + fRec45[1] + fRec47[1] + fRec49[1] + fRec51[1] + 0.5f * fRec37[1] + fRec53[1];
			fVec23[IOTA0 & 2047] = fTemp21;
			fRec37[0] = fVec23[(IOTA0 - iSlow14) & 2047];
			float fRec38 = -(0.5f * fTemp21);
			float fTemp22 = fRec37[1] + fRec38 + 0.5f * fRec35[1];
			fVec24[IOTA0 & 2047] = fTemp22;
			fRec35[0] = fVec24[(IOTA0 - iSlow15) & 2047];
			float fRec36 = -(0.5f * fTemp22);
			float fTemp23 = fRec35[1] + fRec36 + 0.5f * fRec33[1];
			fVec25[IOTA0 & 2047] = fTemp23;
			fRec33[0] = fVec25[(IOTA0 - iSlow16) & 2047];
			float fRec34 = -(0.5f * fTemp23);
			float fTemp24 = fRec33[1] + fRec34 + 0.5f * fRec31[1];
			fVec26[IOTA0 & 1023] = fTemp24;
			fRec31[0] = fVec26[(IOTA0 - iSlow17) & 1023];
			float fRec32 = -(0.5f * fTemp24);
			float fTemp25 = fRec32 + fRec31[1];
			fVec27[0] = fTemp25;
			fRec30[0] = -((fTemp3 * fRec30[1] - (fTemp25 + fVec27[1])) / fTemp20);
			output1[i0] = static_cast<FAUSTFLOAT>(fTemp8 * fTemp1 + fRec0[0] * fRec30[0]);
			IOTA0 = IOTA0 + 1;
			fRec0[1] = fRec0[0];
			fRec2[1] = fRec2[0];
			fRec12[1] = fRec12[0];
			fRec14[1] = fRec14[0];
			fRec13[1] = fRec13[0];
			fRec15[1] = fRec15[0];
			fRec11[1] = fRec11[0];
			fRec17[1] = fRec17[0];
			fRec16[1] = fRec16[0];
			fRec19[1] = fRec19[0];
			fRec18[1] = fRec18[0];
			fRec21[1] = fRec21[0];
			fRec20[1] = fRec20[0];
			fRec23[1] = fRec23[0];
			fRec22[1] = fRec22[0];
			fRec25[1] = fRec25[0];
			fRec24[1] = fRec24[0];
			fRec27[1] = fRec27[0];
			fRec26[1] = fRec26[0];
			fRec29[1] = fRec29[0];
			fRec28[1] = fRec28[0];
			fRec9[1] = fRec9[0];
			fRec7[1] = fRec7[0];
			fRec5[1] = fRec5[0];
			fRec3[1] = fRec3[0];
			fVec14[1] = fVec14[0];
			fRec1[1] = fRec1[0];
			fRec40[1] = fRec40[0];
			fRec39[1] = fRec39[0];
			fRec42[1] = fRec42[0];
			fRec41[1] = fRec41[0];
			fRec44[1] = fRec44[0];
			fRec43[1] = fRec43[0];
			fRec46[1] = fRec46[0];
			fRec45[1] = fRec45[0];
			fRec48[1] = fRec48[0];
			fRec47[1] = fRec47[0];
			fRec50[1] = fRec50[0];
			fRec49[1] = fRec49[0];
			fRec52[1] = fRec52[0];
			fRec51[1] = fRec51[0];
			fRec54[1] = fRec54[0];
			fRec53[1] = fRec53[0];
			fRec37[1] = fRec37[0];
			fRec35[1] = fRec35[0];
			fRec33[1] = fRec33[0];
			fRec31[1] = fRec31[0];
			fVec27[1] = fVec27[0];
			fRec30[1] = fRec30[0];
		}
	}

};

#endif
} } // namespace spotykach::rv_freeverb
