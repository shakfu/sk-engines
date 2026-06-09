// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// GENERATED FILE - do not edit by hand. Regenerate with `make faust-gen` (cyfaust cpp backend).
// Source: src/engine/faust/voice.dsp. Class name is the Faust default `mydsp` (global namespace);
// FaustEngine refers to it as ::mydsp. The arch shim below provides the dsp/UI/Meta base types the
// generated class assumes (see faust_arch.h for why we declare them ourselves rather than vendor
// Faust's GPL-with-exception headers).

#include "engine/faust/faust_arch.h"

/* ------------------------------------------------------------
name: "voice"
Code generated with Faust 2.83.1 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __mydsp_H__
#define  __mydsp_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

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

static float mydsp_faustpower4_f(float value) {
	return value * value * value * value;
}

class mydsp : public dsp {
	
 private:
	
	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider0;
	float fConst2;
	FAUSTFLOAT fHslider1;
	float fRec5[2];
	float fConst3;
	float fConst4;
	FAUSTFLOAT fButton0;
	float fVec0[2];
	float fRec7[2];
	float fConst5;
	float fConst6;
	int iRec8[2];
	FAUSTFLOAT fHslider2;
	float fRec4[2];
	float fRec3[2];
	float fRec2[2];
	float fRec1[2];
	float fRec0[2];
	
 public:
	mydsp() {
	}
	
	mydsp(const mydsp&) = default;
	
	virtual ~mydsp() = default;
	
	mydsp& operator=(const mydsp&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("envelopes.lib/adsr:author", "Yann Orlarey and Andrey Bundin");
		m->declare("envelopes.lib/author", "GRAME");
		m->declare("envelopes.lib/copyright", "GRAME");
		m->declare("envelopes.lib/license", "LGPL with exception");
		m->declare("envelopes.lib/name", "Faust Envelope Library");
		m->declare("envelopes.lib/version", "1.3.0");
		m->declare("filename", "voice");
		m->declare("filters.lib/lowpass0_highpass1", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/pole:author", "Julius O. Smith III");
		m->declare("filters.lib/pole:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/pole:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.3.0");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.7.0");
		m->declare("name", "voice");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/saw2ptr:author", "Julius O. Smith III");
		m->declare("oscillators.lib/saw2ptr:license", "STK-4.3");
		m->declare("oscillators.lib/version", "1.4.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("vaeffects.lib/moog_vcf:author", "Julius O. Smith III");
		m->declare("vaeffects.lib/moog_vcf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("vaeffects.lib/moog_vcf:license", "MIT-style STK-4.3 license");
		m->declare("vaeffects.lib/name", "Faust Virtual Analog Filter Effect Library");
		m->declare("vaeffects.lib/version", "1.2.0");
	}

	virtual int getNumInputs() {
		return 0;
	}
	virtual int getNumOutputs() {
		return 2;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 75398.23f / fConst0;
		fConst2 = 1.0f / fConst0;
		fConst3 = std::max<float>(1.0f, 0.005f * fConst0);
		fConst4 = 1.0f / fConst3;
		fConst5 = 0.3f / std::max<float>(1.0f, 0.12f * fConst0);
		fConst6 = 1.0f / std::max<float>(1.0f, 0.25f * fConst0);
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.5f);
		fHslider1 = static_cast<FAUSTFLOAT>(2.2e+02f);
		fButton0 = static_cast<FAUSTFLOAT>(0.0f);
		fHslider2 = static_cast<FAUSTFLOAT>(0.3f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			fRec5[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fVec0[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec7[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			iRec8[l3] = 0;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec4[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec3[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec2[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec1[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec0[l8] = 0.0f;
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
		ui_interface->openVerticalBox("voice");
		ui_interface->addHorizontalSlider("cutoff", &fHslider0, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("freq", &fHslider1, FAUSTFLOAT(2.2e+02f), FAUSTFLOAT(4e+01f), FAUSTFLOAT(4e+03f), FAUSTFLOAT(0.01f));
		ui_interface->addButton("gate", &fButton0);
		ui_interface->addHorizontalSlider("res", &fHslider2, FAUSTFLOAT(0.3f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.01f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst1 * static_cast<float>(fHslider0);
		float fSlow1 = mydsp_faustpower4_f(fSlow0);
		float fSlow2 = 1.0f - fSlow0;
		float fSlow3 = std::max<float>(1.1920929e-07f, std::fabs(static_cast<float>(fHslider1)));
		float fSlow4 = fConst2 * fSlow3;
		float fSlow5 = 1.0f - fConst0 / fSlow3;
		float fSlow6 = static_cast<float>(fButton0);
		int iSlow7 = fSlow6 == 0.0f;
		float fSlow8 = 4.0f * std::max<float>(0.0f, std::min<float>(static_cast<float>(fHslider2), 0.999999f));
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			float fTemp0 = fSlow4 + fRec5[1] + -1.0f;
			int iTemp1 = fTemp0 < 0.0f;
			float fTemp2 = fSlow4 + fRec5[1];
			fRec5[0] = ((iTemp1) ? fTemp2 : fTemp0);
			float fRec6 = ((iTemp1) ? fTemp2 : fSlow4 + fRec5[1] + fSlow5 * fTemp0);
			fVec0[0] = fSlow6;
			fRec7[0] = fSlow6 + fRec7[1] * static_cast<float>(fVec0[1] >= fSlow6);
			iRec8[0] = iSlow7 * (iRec8[1] + 1);
			fRec4[0] = fSlow2 * fRec4[1] + (2.0f * fRec6 + -1.0f) * std::max<float>(0.0f, std::min<float>(fConst4 * fRec7[0], std::max<float>(fConst5 * (fConst3 - fRec7[0]) + 1.0f, 0.7f)) * (1.0f - fConst6 * static_cast<float>(iRec8[0]))) - fSlow8 * fRec0[1];
			fRec3[0] = fRec4[0] + fSlow2 * fRec3[1];
			fRec2[0] = fRec3[0] + fSlow2 * fRec2[1];
			fRec1[0] = fRec2[0] + fSlow2 * fRec1[1];
			fRec0[0] = fSlow1 * fRec1[0];
			output0[i0] = static_cast<FAUSTFLOAT>(fRec0[0]);
			output1[i0] = static_cast<FAUSTFLOAT>(fRec0[0]);
			fRec5[1] = fRec5[0];
			fVec0[1] = fVec0[0];
			fRec7[1] = fRec7[0];
			iRec8[1] = iRec8[0];
			fRec4[1] = fRec4[0];
			fRec3[1] = fRec3[0];
			fRec2[1] = fRec2[0];
			fRec1[1] = fRec1[0];
			fRec0[1] = fRec0[0];
		}
	}

};

#endif
