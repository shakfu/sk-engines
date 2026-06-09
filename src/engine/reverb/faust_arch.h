// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// Minimal, MIT-clean Faust architecture shim (faust-spike, throwaway).
//
// cyfaust's cpp backend emits `class mydsp : public dsp`, whose metadata()/buildUserInterface()
// methods call Meta::declare and UI::add*/open*/close*. Upstream supplies those base types via
// faust/dsp/dsp.h + faust/gui/{UI,meta}.h, which carry a GPL-with-exception header. This project is
// MIT, so rather than vendor those wrappers we declare here the only base types the generated kernel
// needs. The generated compute() (the audio path) touches none of them - they exist purely so the
// kernel class and its compile-time-only metadata/UI methods compile.
//
// Control path: the kernel's params are PRIVATE members, exposed only through buildUserInterface,
// which hands out one FAUSTFLOAT* per control. CaptureUI (reverb_engine.cpp) captures those pointers by
// label once at init; the engine then writes params through them. This is the idiomatic Faust embed
// pattern and exactly the "treat the generated class as an opaque kernel + manual param wiring" seam.

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

// Metadata sink (kernel.metadata(Meta*)). Never called on the audio path.
struct Meta {
    virtual ~Meta() = default;
    virtual void declare(const char* /*key*/, const char* /*value*/) {}
};

// UI sink (kernel.buildUserInterface(UI*)). Default no-ops; CaptureUI overrides the add* it cares about
// to capture control zone pointers. Covers the widget set Faust can emit so a changed .dsp still
// compiles without editing this shim.
class UI {
 public:
    virtual ~UI() = default;
    // layout
    virtual void openTabBox(const char*) {}
    virtual void openHorizontalBox(const char*) {}
    virtual void openVerticalBox(const char*) {}
    virtual void closeBox() {}
    // active widgets
    virtual void addButton(const char*, FAUSTFLOAT*) {}
    virtual void addCheckButton(const char*, FAUSTFLOAT*) {}
    virtual void addVerticalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addHorizontalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addNumEntry(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    // passive widgets (bargraphs)
    virtual void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) {}
    virtual void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) {}
    // per-zone metadata
    virtual void declare(FAUSTFLOAT*, const char*, const char*) {}
};

// Base of the generated kernel. The kernel redeclares every method it needs as `virtual`; the base
// only has to exist with a virtual destructor.
class dsp {
 public:
    dsp() = default;
    virtual ~dsp() = default;
};
