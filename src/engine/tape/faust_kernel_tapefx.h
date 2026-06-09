// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// GENERATED FILE - do not edit by hand. Regenerate with `make faust-gen` (cyfaust cpp backend).
// Source: src/engine/tape/tapefx.dsp. The generated `class mydsp` is wrapped in namespace spotykach::tfx_tapefx; its
// dsp/UI/Meta base types resolve to the shared arch shim (see engine/faust_arch.h).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#include "engine/faust_arch.h"

namespace spotykach { namespace tfx_tapefx {
/* ------------------------------------------------------------
name: "tapefx"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __tfx_tapefx_H__
#define  __tfx_tapefx_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

/* link with : "" */

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
	
	FAUSTFLOAT fHslider0;
	int iVec0[2];
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	float fConst3;
	float fConst4;
	float fConst5;
	FAUSTFLOAT fHslider1;
	int IOTA0;
	float fVec1[4096];
	FAUSTFLOAT fHslider2;
	float fConst6;
	FAUSTFLOAT fHslider3;
	float fRec4[2];
	float fRec5[2];
	float fConst7;
	float fRec6[2];
	float fRec7[2];
	float fConst8;
	float fRec8[2];
	float fRec9[2];
	float fConst9;
	float fRec10[2];
	float fRec11[2];
	float fVec2[3];
	float fRec3[2];
	float fRec0[2];
	float fConst10;
	float fRec1[2];
	float fConst11;
	
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
		m->declare("description", "Tape wow/flutter + Jiles-Atherton hysteresis (sk-engines tape engine).");
		m->declare("filename", "tapefx");
		m->declare("filters.lib/SVFTPT:author", "Dario Sanfilippo");
		m->declare("filters.lib/SVFTPT:copyright", "Copyright (C) 2024 Dario Sanfilippo <sanfilippo.dario@gmail.com>");
		m->declare("filters.lib/SVFTPT:license", "MIT License");
		m->declare("filters.lib/lowpass0_highpass1", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/nlf2:author", "Julius O. Smith III");
		m->declare("filters.lib/nlf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/nlf2:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("hysteresis.lib/author", "Thomas Mandolini");
		m->declare("hysteresis.lib/contributor", "Bart Brouns a.k.a magnetophon");
		m->declare("hysteresis.lib/ja_hysteresis:author", "Thomas Mandolini");
		m->declare("hysteresis.lib/ja_hysteresis:license", "LGPLv2.1");
		m->declare("hysteresis.lib/ja_processor:author", "Thomas Mandolini");
		m->declare("hysteresis.lib/ja_processor:license", "LGPLv2.1");
		m->declare("hysteresis.lib/name", "Faust Hysteresis Library");
		m->declare("hysteresis.lib/version", "1.0.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "tapefx");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.7.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.6.0");
	}

	virtual int getNumInputs() {
		return 1;
	}
	virtual int getNumOutputs() {
		return 1;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = std::tan(21.991148f / fConst0);
		fConst2 = fConst1 + 1.4142271f;
		fConst3 = fConst1 * fConst2 + 1.0f;
		fConst4 = fConst1 / fConst3;
		fConst5 = 2.0f * fConst4;
		fConst6 = 6.2831855f / fConst0;
		fConst7 = 37.699112f / fConst0;
		fConst8 = 75.398224f / fConst0;
		fConst9 = 113.097336f / fConst0;
		fConst10 = 2.0f * fConst1;
		fConst11 = 1.0f / fConst3;
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider2 = static_cast<FAUSTFLOAT>(0.3f);
		fHslider3 = static_cast<FAUSTFLOAT>(0.4f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		IOTA0 = 0;
		for (int l1 = 0; l1 < 4096; l1 = l1 + 1) {
			fVec1[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec4[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec5[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec6[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec7[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec8[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec9[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec10[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec11[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 3; l10 = l10 + 1) {
			fVec2[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = l11 + 1) {
			fRec3[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec0[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			fRec1[l13] = 0.0f;
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
		ui_interface->openVerticalBox("tapefx");
		ui_interface->addHorizontalSlider("char", &fHslider1, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("drive", &fHslider0, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("rate", &fHslider3, FAUSTFLOAT(0.4f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("wow", &fHslider2, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		float fSlow0 = std::pow(1e+01f, 2.7f * static_cast<float>(fHslider0));
		float fSlow1 = 316.22775f / std::max<float>(0.001f, fSlow0);
		float fSlow2 = 0.65f * static_cast<float>(fHslider1) + 0.25f;
		float fSlow3 = 7.2e+02f / std::max<float>(0.01f, 3.8e+02f * fSlow2);
		float fSlow4 = 0.0031622776f * fSlow0;
		float fSlow5 = 6e+02f * static_cast<float>(fHslider2);
		float fSlow6 = static_cast<float>(fHslider3);
		float fSlow7 = fConst6 * (2.0f * fSlow6 + 0.5f);
		float fSlow8 = std::sin(fSlow7);
		float fSlow9 = std::cos(fSlow7);
		float fSlow10 = fSlow6 + 1.0f;
		float fSlow11 = fConst7 * fSlow10;
		float fSlow12 = std::sin(fSlow11);
		float fSlow13 = std::cos(fSlow11);
		float fSlow14 = fConst8 * fSlow10;
		float fSlow15 = std::sin(fSlow14);
		float fSlow16 = std::cos(fSlow14);
		float fSlow17 = fConst9 * fSlow10;
		float fSlow18 = std::sin(fSlow17);
		float fSlow19 = std::cos(fSlow17);
		float fSlow20 = 0.00049410586f * fSlow0;
		float fSlow21 = 0.5277778f * fSlow2;
		float fSlow22 = 0.007916667f * fSlow2;
		float fSlow23 = 0.001087033f * fSlow0;
		float fSlow24 = 0.0015811388f * fSlow0;
		float fSlow25 = 0.001087033f * fSlow0;
		float fSlow26 = 0.002668172f * fSlow0;
		float fSlow27 = 0.00049410586f * fSlow0;
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			iVec0[0] = 1;
			float fTemp0 = static_cast<float>(input0[i0]);
			fVec1[IOTA0 & 4095] = fTemp0;
			fRec4[0] = fSlow8 * fRec5[1] + fSlow9 * fRec4[1];
			float fTemp1 = static_cast<float>(1 - iVec0[1]);
			fRec5[0] = fTemp1 + fSlow9 * fRec5[1] - fSlow8 * fRec4[1];
			fRec6[0] = fSlow12 * fRec7[1] + fSlow13 * fRec6[1];
			fRec7[0] = fTemp1 + fSlow13 * fRec7[1] - fSlow12 * fRec6[1];
			fRec8[0] = fSlow15 * fRec9[1] + fSlow16 * fRec8[1];
			fRec9[0] = fTemp1 + fSlow16 * fRec9[1] - fSlow15 * fRec8[1];
			fRec10[0] = fSlow18 * fRec11[1] + fSlow19 * fRec10[1];
			fRec11[0] = fTemp1 + fSlow19 * fRec11[1] - fSlow18 * fRec10[1];
			float fTemp2 = fSlow5 * (0.7f * fRec4[0] + 0.3f * (0.6f * fRec6[0] + 0.25f * fRec8[0] + 0.15f * fRec10[0]));
			float fTemp3 = fTemp2 + 1.2e+03f;
			int iTemp4 = static_cast<int>(fTemp3);
			float fTemp5 = std::floor(fTemp3);
			float fTemp6 = fVec1[(IOTA0 - std::min<int>(2401, std::max<int>(0, iTemp4))) & 4095] * (fTemp5 + (-1199.0f - fTemp2)) + (fTemp2 + (1.2e+03f - fTemp5)) * fVec1[(IOTA0 - std::min<int>(2401, std::max<int>(0, iTemp4 + 1))) & 4095];
			float fTemp7 = fSlow4 * fTemp6;
			fVec2[0] = fTemp7;
			float fTemp8 = fVec2[1] - fVec2[2];
			float fTemp9 = 0.140625f * fTemp8;
			float fTemp10 = fSlow20 * fTemp6;
			float fTemp11 = fTemp7 - fVec2[1];
			float fTemp12 = 0.046875f * fTemp11;
			float fTemp13 = 0.15625f * fVec2[1];
			float fTemp14 = fTemp9 + fTemp10 - (fTemp12 + fTemp13);
			float fTemp15 = tanhf(0.5277778f * (fTemp9 + 0.84375f * fVec2[1] + fTemp10 + 0.015f * fRec3[1] - fTemp12));
			float fTemp16 = 1.0f - mydsp_faustpower2_f(fTemp15);
			float fTemp17 = fTemp15 - fRec3[1];
			float fTemp18 = 3.0f * std::fabs(fTemp17) + 1.0f;
			float fTemp19 = std::max<float>(-1.0f, std::min<float>(1.0f, fRec3[1] + fTemp14 * (fSlow21 * fTemp16 + fTemp17 / (fTemp18 * (((fTemp14 >= 0.0f) ? 1.0f : -1.0f) + (0.001f - 0.015f * (fTemp17 / fTemp18))))) / (1.0f - fSlow22 * fTemp16)));
			float fTemp20 = 0.125f * (fTemp8 - fTemp11);
			float fTemp21 = 0.34375f * fVec2[1];
			float fTemp22 = fTemp20 + fTemp12 + fSlow23 * fTemp6 - (fTemp9 + fTemp21);
			float fTemp23 = tanhf(0.5277778f * (fTemp20 + 0.5f * fVec2[1] + fSlow24 * fTemp6 + 0.015f * fTemp19));
			float fTemp24 = 1.0f - mydsp_faustpower2_f(fTemp23);
			float fTemp25 = fTemp23 - fTemp19;
			float fTemp26 = 3.0f * std::fabs(fTemp25) + 1.0f;
			float fTemp27 = std::max<float>(-1.0f, std::min<float>(1.0f, fTemp19 + fTemp22 * (fSlow21 * fTemp24 + fTemp25 / (fTemp26 * (((fTemp22 >= 0.0f) ? 1.0f : -1.0f) + (0.001f - 0.015f * (fTemp25 / fTemp26))))) / (1.0f - fSlow22 * fTemp24)));
			float fTemp28 = 0.046875f * fTemp8;
			float fTemp29 = 0.140625f * fTemp11;
			float fTemp30 = fTemp28 + fSlow25 * fTemp6 - (fTemp20 + fTemp21 + fTemp29);
			float fTemp31 = tanhf(0.5277778f * (fTemp28 + fTemp13 + fSlow26 * fTemp6 + 0.015f * fTemp27 - fTemp29));
			float fTemp32 = 1.0f - mydsp_faustpower2_f(fTemp31);
			float fTemp33 = fTemp31 - fTemp27;
			float fTemp34 = 3.0f * std::fabs(fTemp33) + 1.0f;
			float fTemp35 = std::max<float>(-1.0f, std::min<float>(1.0f, fTemp27 + fTemp30 * (fSlow21 * fTemp32 + fTemp33 / (fTemp34 * (((fTemp30 >= 0.0f) ? 1.0f : -1.0f) + (0.001f - 0.015f * (fTemp33 / fTemp34))))) / (1.0f - fSlow22 * fTemp32)));
			float fTemp36 = fTemp29 + fSlow27 * fTemp6 - (fTemp13 + fTemp28);
			float fTemp37 = tanhf(0.5277778f * (fTemp7 + 0.015f * fTemp35));
			float fTemp38 = 1.0f - mydsp_faustpower2_f(fTemp37);
			float fTemp39 = fTemp37 - fTemp35;
			float fTemp40 = 3.0f * std::fabs(fTemp39) + 1.0f;
			fRec3[0] = std::max<float>(-1.0f, std::min<float>(1.0f, fTemp35 + fTemp36 * (fSlow21 * fTemp38 + fTemp39 / (fTemp40 * (((fTemp36 >= 0.0f) ? 1.0f : -1.0f) + (0.001f - 0.015f * (fTemp39 / fTemp40))))) / (1.0f - fSlow22 * fTemp38)));
			float fTemp41 = fSlow3 * fRec3[0];
			float fTemp42 = fTemp41 - (fConst2 * fRec0[1] + fRec1[1]);
			fRec0[0] = fRec0[1] + fConst5 * fTemp42;
			float fTemp43 = fRec0[1] + fConst4 * fTemp42;
			fRec1[0] = fRec1[1] + fConst10 * fTemp43;
			float fTemp44 = fConst11 * fTemp42;
			float fRec2 = fTemp44;
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow1 * fRec2);
			iVec0[1] = iVec0[0];
			IOTA0 = IOTA0 + 1;
			fRec4[1] = fRec4[0];
			fRec5[1] = fRec5[0];
			fRec6[1] = fRec6[0];
			fRec7[1] = fRec7[0];
			fRec8[1] = fRec8[0];
			fRec9[1] = fRec9[0];
			fRec10[1] = fRec10[0];
			fRec11[1] = fRec11[0];
			fVec2[2] = fVec2[1];
			fVec2[1] = fVec2[0];
			fRec3[1] = fRec3[0];
			fRec0[1] = fRec0[0];
			fRec1[1] = fRec1[0];
		}
	}

};

#endif
} } // namespace spotykach::tfx_tapefx
