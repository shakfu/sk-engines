// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// GENERATED FILE - do not edit by hand. Regenerate with `make faust-gen` (cyfaust cpp backend).
// Source: src/engine/reverb/dattorro.dsp. The generated `class mydsp` is wrapped in namespace spotykach::rv_dattorro; its
// dsp/UI/Meta base types resolve to the global arch shim (see faust_arch.h).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#include "engine/reverb/faust_arch.h"

namespace spotykach { namespace rv_dattorro {
/* ------------------------------------------------------------
name: "dattorro"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __rv_dattorro_H__
#define  __rv_dattorro_H__

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
	
	int fSampleRate;
	float fConst0;
	FAUSTFLOAT fVslider0;
	float fConst1;
	float fRec0[2];
	FAUSTFLOAT fVslider1;
	float fRec1[2];
	FAUSTFLOAT fVslider2;
	float fRec13[2];
	float fRec12[3];
	FAUSTFLOAT fVslider3;
	float fRec14[2];
	int IOTA0;
	float fVec0[256];
	float fRec10[2];
	float fVec1[128];
	float fRec8[2];
	FAUSTFLOAT fVslider4;
	float fRec15[2];
	float fVec2[512];
	float fRec6[2];
	float fVec3[512];
	float fRec4[2];
	FAUSTFLOAT fVslider5;
	float fRec16[2];
	FAUSTFLOAT fVslider6;
	float fRec20[2];
	FAUSTFLOAT fVslider7;
	float fRec23[2];
	float fVec4[1024];
	float fRec21[2];
	float fVec5[8192];
	float fRec19[2];
	FAUSTFLOAT fVslider8;
	float fRec24[2];
	float fVec6[4096];
	float fRec17[2];
	float fVec7[4096];
	float fRec2[2];
	float fVec8[1024];
	float fRec28[2];
	float fVec9[8192];
	float fRec27[2];
	float fVec10[2048];
	float fRec25[2];
	float fVec11[2048];
	float fRec3[2];
	
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
		m->declare("demos.lib/dattorro_rev_demo:author", "Jakob Zerbian");
		m->declare("demos.lib/dattorro_rev_demo:license", "MIT-style STK-4.3 license");
		m->declare("demos.lib/name", "Faust Demos Library");
		m->declare("demos.lib/version", "1.4.0");
		m->declare("description", "Dattorro plate reverb (sk-engines faust engine).");
		m->declare("filename", "dattorro");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "dattorro");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("reverbs.lib/dattorro_rev:author", "Jakob Zerbian");
		m->declare("reverbs.lib/dattorro_rev:licence", "MIT-style STK-4.3 license");
		m->declare("reverbs.lib/name", "Faust Reverb Library");
		m->declare("reverbs.lib/version", "1.5.1");
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
		fConst0 = 44.1f / std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 1.0f - fConst0;
	}
	
	virtual void instanceResetUserInterface() {
		fVslider0 = static_cast<FAUSTFLOAT>(-6.0f);
		fVslider1 = static_cast<FAUSTFLOAT>(0.0f);
		fVslider2 = static_cast<FAUSTFLOAT>(0.7f);
		fVslider3 = static_cast<FAUSTFLOAT>(0.625f);
		fVslider4 = static_cast<FAUSTFLOAT>(0.625f);
		fVslider5 = static_cast<FAUSTFLOAT>(0.7f);
		fVslider6 = static_cast<FAUSTFLOAT>(0.625f);
		fVslider7 = static_cast<FAUSTFLOAT>(0.625f);
		fVslider8 = static_cast<FAUSTFLOAT>(0.625f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			fRec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec1[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec13[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 3; l3 = l3 + 1) {
			fRec12[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec14[l4] = 0.0f;
		}
		IOTA0 = 0;
		for (int l5 = 0; l5 < 256; l5 = l5 + 1) {
			fVec0[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec10[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 128; l7 = l7 + 1) {
			fVec1[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec8[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec15[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 512; l10 = l10 + 1) {
			fVec2[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = l11 + 1) {
			fRec6[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 512; l12 = l12 + 1) {
			fVec3[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			fRec4[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = l14 + 1) {
			fRec16[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fRec20[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = l16 + 1) {
			fRec23[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 1024; l17 = l17 + 1) {
			fVec4[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = l18 + 1) {
			fRec21[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 8192; l19 = l19 + 1) {
			fVec5[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = l20 + 1) {
			fRec19[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = l21 + 1) {
			fRec24[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 4096; l22 = l22 + 1) {
			fVec6[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = l23 + 1) {
			fRec17[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 4096; l24 = l24 + 1) {
			fVec7[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = l25 + 1) {
			fRec2[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 1024; l26 = l26 + 1) {
			fVec8[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2; l27 = l27 + 1) {
			fRec28[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 8192; l28 = l28 + 1) {
			fVec9[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = l29 + 1) {
			fRec27[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 2048; l30 = l30 + 1) {
			fVec10[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = l31 + 1) {
			fRec25[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2048; l32 = l32 + 1) {
			fVec11[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = l33 + 1) {
			fRec3[l33] = 0.0f;
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
		ui_interface->openHorizontalBox("Dattorro Reverb");
		ui_interface->declare(0, "0", "");
		ui_interface->openHorizontalBox("Input");
		ui_interface->declare(&fVslider2, "1", "");
		ui_interface->declare(&fVslider2, "style", "knob");
		ui_interface->declare(&fVslider2, "tooltip", "lowpass-like filter, 0 = no signal, 1 = no filtering");
		ui_interface->addVerticalSlider("Prefilter", &fVslider2, FAUSTFLOAT(0.7f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->declare(&fVslider3, "2", "");
		ui_interface->declare(&fVslider3, "style", "knob");
		ui_interface->declare(&fVslider3, "tooltip", "diffusion factor, influences reverb color and density");
		ui_interface->addVerticalSlider("Diffusion 1", &fVslider3, FAUSTFLOAT(0.625f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->declare(&fVslider4, "3", "");
		ui_interface->declare(&fVslider4, "style", "knob");
		ui_interface->declare(&fVslider4, "tooltip", "diffusion factor, influences reverb color and density");
		ui_interface->addVerticalSlider("Diffusion 2", &fVslider4, FAUSTFLOAT(0.625f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
		ui_interface->declare(0, "1", "");
		ui_interface->openHorizontalBox("Feedback");
		ui_interface->declare(&fVslider7, "1", "");
		ui_interface->declare(&fVslider7, "style", "knob");
		ui_interface->declare(&fVslider7, "tooltip", "diffusion factor, influences reverb color and density");
		ui_interface->addVerticalSlider("Diffusion 1", &fVslider7, FAUSTFLOAT(0.625f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->declare(&fVslider8, "2", "");
		ui_interface->declare(&fVslider8, "style", "knob");
		ui_interface->declare(&fVslider8, "tooltip", "diffusion factor, influences reverb color and density");
		ui_interface->addVerticalSlider("Diffusion 2", &fVslider8, FAUSTFLOAT(0.625f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->declare(&fVslider5, "3", "");
		ui_interface->declare(&fVslider5, "style", "knob");
		ui_interface->declare(&fVslider5, "tooltip", "decay length, 1 = infinite");
		ui_interface->addVerticalSlider("Decay Rate", &fVslider5, FAUSTFLOAT(0.7f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->declare(&fVslider6, "4", "");
		ui_interface->declare(&fVslider6, "style", "knob");
		ui_interface->declare(&fVslider6, "tooltip", "dampening in feedback network");
		ui_interface->addVerticalSlider("Damping", &fVslider6, FAUSTFLOAT(0.625f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
		ui_interface->declare(0, "2", "");
		ui_interface->openHorizontalBox("Output");
		ui_interface->declare(&fVslider1, "1", "");
		ui_interface->declare(&fVslider1, "style", "knob");
		ui_interface->declare(&fVslider1, "tooltip", "-1 = dry, 1 = wet");
		ui_interface->addVerticalSlider("Dry/Wet Mix", &fVslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(-1.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.01f));
		ui_interface->declare(&fVslider0, "2", "");
		ui_interface->declare(&fVslider0, "style", "knob");
		ui_interface->declare(&fVslider0, "tooltip", "Output Gain");
		ui_interface->declare(&fVslider0, "unit", "dB");
		ui_interface->addVerticalSlider("Level", &fVslider0, FAUSTFLOAT(-6.0f), FAUSTFLOAT(-7e+01f), FAUSTFLOAT(4e+01f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst0 * std::pow(1e+01f, 0.05f * static_cast<float>(fVslider0));
		float fSlow1 = fConst0 * static_cast<float>(fVslider1);
		float fSlow2 = fConst0 * static_cast<float>(fVslider2);
		float fSlow3 = fConst0 * static_cast<float>(fVslider3);
		float fSlow4 = fConst0 * static_cast<float>(fVslider4);
		float fSlow5 = fConst0 * static_cast<float>(fVslider5);
		float fSlow6 = fConst0 * static_cast<float>(fVslider6);
		float fSlow7 = fConst0 * static_cast<float>(fVslider7);
		float fSlow8 = fConst0 * static_cast<float>(fVslider8);
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			fRec0[0] = fSlow0 + fConst1 * fRec0[1];
			float fTemp0 = static_cast<float>(input0[i0]);
			fRec1[0] = fSlow1 + fConst1 * fRec1[1];
			float fTemp1 = fRec1[0] + 1.0f;
			float fTemp2 = 1.0f - 0.5f * fTemp1;
			fRec13[0] = fSlow2 + fConst1 * fRec13[1];
			float fTemp3 = static_cast<float>(input1[i0]);
			fRec12[0] = (1.0f - fRec13[0]) * fRec12[2] + 0.5f * (fTemp0 + fTemp3) * fRec13[0];
			fRec14[0] = fSlow3 + fConst1 * fRec14[1];
			float fTemp4 = fRec12[0] - fRec14[0] * fRec10[1];
			fVec0[IOTA0 & 255] = fTemp4;
			fRec10[0] = fVec0[(IOTA0 - 142) & 255];
			float fRec11 = fRec14[0] * fTemp4;
			float fTemp5 = fRec11 + fRec10[1] - fRec14[0] * fRec8[1];
			fVec1[IOTA0 & 127] = fTemp5;
			fRec8[0] = fVec1[(IOTA0 - 107) & 127];
			float fRec9 = fRec14[0] * fTemp5;
			fRec15[0] = fSlow4 + fConst1 * fRec15[1];
			float fTemp6 = fRec9 + fRec8[1] - fRec15[0] * fRec6[1];
			fVec2[IOTA0 & 511] = fTemp6;
			fRec6[0] = fVec2[(IOTA0 - 379) & 511];
			float fRec7 = fRec15[0] * fTemp6;
			float fTemp7 = fRec7 + fRec6[1] - fRec15[0] * fRec4[1];
			fVec3[IOTA0 & 511] = fTemp7;
			fRec4[0] = fVec3[(IOTA0 - 277) & 511];
			float fRec5 = fRec15[0] * fTemp7;
			fRec16[0] = fSlow5 + fConst1 * fRec16[1];
			fRec20[0] = fSlow6 + fConst1 * fRec20[1];
			float fTemp8 = 1.0f - fRec20[0];
			fRec23[0] = fSlow7 + fConst1 * fRec23[1];
			float fTemp9 = fRec23[0] * fRec21[1] + fRec3[1];
			fVec4[IOTA0 & 1023] = fTemp9;
			fRec21[0] = fVec4[(IOTA0 - 908) & 1023];
			float fRec22 = -(fRec23[0] * fTemp9);
			fVec5[IOTA0 & 8191] = fRec22 + fRec21[1];
			fRec19[0] = fRec20[0] * fRec19[1] + fTemp8 * fVec5[(IOTA0 - 4217) & 8191];
			fRec24[0] = fSlow8 + fConst1 * fRec24[1];
			float fTemp10 = fRec19[0] * fRec16[0] - fRec24[0] * fRec17[1];
			fVec6[IOTA0 & 4095] = fTemp10;
			fRec17[0] = fVec6[(IOTA0 - 2656) & 4095];
			float fRec18 = fRec24[0] * fTemp10;
			fVec7[IOTA0 & 4095] = fRec18 + fRec17[1];
			fRec2[0] = fRec4[1] + fRec5 + fRec16[0] * fVec7[(IOTA0 - 2656) & 4095];
			float fTemp11 = fRec23[0] * fRec28[1] + fRec2[1];
			fVec8[IOTA0 & 1023] = fTemp11;
			fRec28[0] = fVec8[(IOTA0 - 672) & 1023];
			float fRec29 = -(fRec23[0] * fTemp11);
			fVec9[IOTA0 & 8191] = fRec29 + fRec28[1];
			fRec27[0] = fRec20[0] * fRec27[1] + fTemp8 * fVec9[(IOTA0 - 4453) & 8191];
			float fTemp12 = fRec16[0] * fRec27[0] - fRec24[0] * fRec25[1];
			fVec10[IOTA0 & 2047] = fTemp12;
			fRec25[0] = fVec10[(IOTA0 - 1800) & 2047];
			float fRec26 = fRec24[0] * fTemp12;
			fVec11[IOTA0 & 2047] = fRec26 + fRec25[1];
			fRec3[0] = fRec5 + fRec4[1] + fRec16[0] * fVec11[(IOTA0 - 1800) & 2047];
			output0[i0] = static_cast<FAUSTFLOAT>(fRec0[0] * (fTemp0 * fTemp2 + 0.5f * fTemp1 * fRec2[0]));
			output1[i0] = static_cast<FAUSTFLOAT>(fRec0[0] * (fTemp3 * fTemp2 + 0.5f * fTemp1 * fRec3[0]));
			fRec0[1] = fRec0[0];
			fRec1[1] = fRec1[0];
			fRec13[1] = fRec13[0];
			fRec12[2] = fRec12[1];
			fRec12[1] = fRec12[0];
			fRec14[1] = fRec14[0];
			IOTA0 = IOTA0 + 1;
			fRec10[1] = fRec10[0];
			fRec8[1] = fRec8[0];
			fRec15[1] = fRec15[0];
			fRec6[1] = fRec6[0];
			fRec4[1] = fRec4[0];
			fRec16[1] = fRec16[0];
			fRec20[1] = fRec20[0];
			fRec23[1] = fRec23[0];
			fRec21[1] = fRec21[0];
			fRec19[1] = fRec19[0];
			fRec24[1] = fRec24[0];
			fRec17[1] = fRec17[0];
			fRec2[1] = fRec2[0];
			fRec28[1] = fRec28[0];
			fRec27[1] = fRec27[0];
			fRec25[1] = fRec25[0];
			fRec3[1] = fRec3[0];
		}
	}

};

#endif
} } // namespace spotykach::rv_dattorro
