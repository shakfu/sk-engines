// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// GENERATED FILE - do not edit by hand. Regenerate with `make faust-kernels` (cyfaust cpp backend).
// Source: src/engine/voice/osc.dsp. The generated `class mydsp` is wrapped in namespace spotykach::fx_voice_osc; its
// dsp/UI/Meta base types resolve to the shared arch shim (see engine/faust_arch.h).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#include "engine/faust_arch.h"

namespace spotykach { namespace fx_voice_osc {
/* ------------------------------------------------------------
name: "osc"
Code generated with Faust 2.85.5 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __fx_voice_osc_H__
#define  __fx_voice_osc_H__

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
	FAUSTFLOAT fHslider0;
	float fConst2;
	int iVec0[2];
	float fRec0[2];
	FAUSTFLOAT fHslider1;
	float fRec1[2];
	float fConst3;
	FAUSTFLOAT fHslider2;
	float fRec2[2];
	float fConst4;
	float fRec4[2];
	float fVec1[2];
	int IOTA0;
	float fVec2[4096];
	float fConst5;
	
 public:
	mydsp() {
	}
	
	mydsp(const mydsp&) = default;
	
	virtual ~mydsp() = default;
	
	mydsp& operator=(const mydsp&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "osc");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "osc");
		m->declare("oscillators.lib/lf_sawpos:author", "Bart Brouns, revised by Stéphane Letz");
		m->declare("oscillators.lib/lf_sawpos:licence", "STK-4.3");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/saw2ptr:author", "Julius O. Smith III");
		m->declare("oscillators.lib/saw2ptr:license", "STK-4.3");
		m->declare("oscillators.lib/sawN:author", "Julius O. Smith III");
		m->declare("oscillators.lib/sawN:license", "STK-4.3");
		m->declare("oscillators.lib/version", "1.7.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.6.0");
	}

	virtual int getNumInputs() {
		return 0;
	}
	virtual int getNumOutputs() {
		return 1;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 44.1f / fConst0;
		fConst2 = 1.0f - fConst1;
		fConst3 = 1.0f / fConst0;
		fConst4 = 0.25f * fConst0;
		fConst5 = 0.5f * fConst0;
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider1 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(0.3f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec0[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec1[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec2[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec4[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fVec1[l5] = 0.0f;
		}
		IOTA0 = 0;
		for (int l6 = 0; l6 < 4096; l6 = l6 + 1) {
			fVec2[l6] = 0.0f;
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
		ui_interface->openVerticalBox("osc");
		ui_interface->addHorizontalSlider("freq", &fHslider2, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("level", &fHslider0, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("shape", &fHslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* output0 = outputs[0];
		float fSlow0 = fConst1 * static_cast<float>(fHslider0);
		float fSlow1 = fConst1 * static_cast<float>(fHslider1);
		float fSlow2 = 4e+01f * std::pow(2.0f, 6.0f * static_cast<float>(fHslider2));
		float fSlow3 = std::max<float>(1.1920929e-07f, std::fabs(fSlow2));
		float fSlow4 = fConst3 * fSlow3;
		float fSlow5 = 1.0f - fConst0 / fSlow3;
		float fSlow6 = std::max<float>(fSlow2, 23.44895f);
		float fSlow7 = std::max<float>(2e+01f, std::fabs(fSlow6));
		float fSlow8 = fConst4 / fSlow7;
		float fSlow9 = fConst3 * fSlow7;
		float fSlow10 = std::max<float>(0.0f, std::min<float>(2047.0f, fConst5 / fSlow6));
		float fSlow11 = std::floor(fSlow10);
		float fSlow12 = fSlow11 + (1.0f - fSlow10);
		int iSlow13 = static_cast<int>(fSlow10);
		float fSlow14 = fSlow10 - fSlow11;
		int iSlow15 = iSlow13 + 1;
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			iVec0[0] = 1;
			fRec0[0] = fSlow0 + fConst2 * fRec0[1];
			fRec1[0] = fSlow1 + fConst2 * fRec1[1];
			float fTemp0 = fSlow4 + fRec2[1] + -1.0f;
			int iTemp1 = fTemp0 < 0.0f;
			float fTemp2 = fSlow4 + fRec2[1];
			fRec2[0] = ((iTemp1) ? fTemp2 : fTemp0);
			float fRec3 = ((iTemp1) ? fTemp2 : fSlow4 + fRec2[1] + fSlow5 * fTemp0);
			float fTemp3 = ((1 - iVec0[1]) ? 0.0f : fSlow9 + fRec4[1]);
			fRec4[0] = fTemp3 - std::floor(fTemp3);
			float fTemp4 = mydsp_faustpower2_f(2.0f * fRec4[0] + -1.0f);
			fVec1[0] = fTemp4;
			float fTemp5 = fSlow8 * static_cast<float>(iVec0[1]) * (fTemp4 - fVec1[1]);
			fVec2[IOTA0 & 4095] = fTemp5;
			output0[i0] = static_cast<FAUSTFLOAT>(fRec0[0] * ((1.0f - fRec1[0]) * (2.0f * fRec3 + -1.0f) + fRec1[0] * (fTemp5 - (fSlow12 * fVec2[(IOTA0 - iSlow13) & 4095] + fSlow14 * fVec2[(IOTA0 - iSlow15) & 4095]))));
			iVec0[1] = iVec0[0];
			fRec0[1] = fRec0[0];
			fRec1[1] = fRec1[0];
			fRec2[1] = fRec2[0];
			fRec4[1] = fRec4[0];
			fVec1[1] = fVec1[0];
			IOTA0 = IOTA0 + 1;
		}
	}

};

#endif
} } // namespace spotykach::fx_voice_osc
