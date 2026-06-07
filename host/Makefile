# Desktop host harness for the Spotykach DSP core.
#
# Builds the real src/engine/granular/ for the host (no libDaisy: the <daisy.h> shim under host/shim
# supplies the few symbols common.h needs). DaisySP is portable C++ and is compiled from
# source. Run from the repo root:  make -C host   then   ./host/build/host_spotykach

CXX      ?= clang++
ROOT      = ..
# reso's vendored Rings/stmlib DSP, colocated under the engine and trimmed to the used closure.
RESO_TP   = $(ROOT)/src/engine/reso/thirdparty
BUILD     = build
TARGET    = $(BUILD)/host_spotykach

DAISYSP   = $(ROOT)/lib/DaisySP/Source

# DaisySP .cpp files include sibling headers (e.g. "dsp.h") by bare name, so every Source subdirectory
# must be on the include path. src/transport/ headers (transport.h) likewise pull siblings
# (synclock.h/tempo.h) by bare name, so that dir is on the path too for TUs that include the transport.
DAISYSP_INC = $(addprefix -I,$(wildcard $(DAISYSP)/*/))
INCLUDES  = -Ishim -I$(ROOT)/src -I$(ROOT)/src/transport -I$(RESO_TP) -I. -I$(DAISYSP) $(DAISYSP_INC)
# -DTEST selects stmlib's portable C paths (the ARM ssat/usat asm is gated behind #ifndef TEST), so
# the Rings DSP (under $(RESO_TP)) compiles on the desktop. Unused elsewhere in this repo.
CXXFLAGS  = -std=c++17 -O2 -DTEST -Wall -Wno-unused-variable -Wno-unused-but-set-variable $(INCLUDES)

CORE_SRC    = $(wildcard $(ROOT)/src/engine/granular/*.cpp)
# Engine-agnostic shared primitives (biquad/follower/adenv/cpattern + header-only siblings) live in
# src/dsp/ and are consumed by the granular engine via "dsp/..." includes, so compile them here too.
DSP_SRC     = $(wildcard $(ROOT)/src/dsp/*.cpp)
# The platform transport service (the clock the engine observes via the injected ITransport). The host
# plays the platform's role: it constructs a real Transport and drives transport.tick() per block.
TRANSPORT_SRC = $(wildcard $(ROOT)/src/transport/*.cpp)
# src/engine/*.cpp now includes the hardware-free LEDRing + Color impls (moved out of src/ui/ in
# Phase 5 R2), which the DisplayModel needs; the rest of src/ui/ stays out (it needs the HAL).
ENGINE_SRC  = $(wildcard $(ROOT)/src/engine/*.cpp)
DAISYSP_SRC = $(wildcard $(DAISYSP)/*/*.cpp)
LIB_SRC     = $(CORE_SRC) $(DSP_SRC) $(TRANSPORT_SRC) $(ENGINE_SRC) $(DAISYSP_SRC)
# The edrums/reso engines live under src/engine/<name>/ (outside the wildcards above); add for tests.
EDRUMS_SRC  = $(ROOT)/src/engine/edrums/edrums_engine.cpp
RESO_SRC    = $(ROOT)/src/engine/reso/reso_engine.cpp
# reso's DSP is the Mutable Instruments Rings engine + its stmlib support library (under $(RESO_TP)).
RINGS_SRC   = $(RESO_TP)/rings/dsp/part.cc $(RESO_TP)/rings/dsp/string.cc \
              $(RESO_TP)/rings/dsp/resonator.cc $(RESO_TP)/rings/dsp/fm_voice.cc \
              $(RESO_TP)/rings/resources.cc \
              $(RESO_TP)/stmlib/dsp/units.cc $(RESO_TP)/stmlib/utils/random.cc \
              $(RESO_TP)/stmlib/dsp/atan.cc

SRC          = main_host.cpp $(LIB_SRC)
TEST         = $(BUILD)/test_engine_params
TEST_EDRUMS  = $(BUILD)/test_edrums_slots
TEST_RESO    = $(BUILD)/test_reso

.PHONY: all clean run test test-granular test-edrums test-reso
all: $(TARGET)

$(TARGET): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

$(TEST): test_engine_params.cpp $(LIB_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_engine_params.cpp $(LIB_SRC) -o $@

$(TEST_EDRUMS): test_edrums_slots.cpp $(LIB_SRC) $(EDRUMS_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_edrums_slots.cpp $(LIB_SRC) $(EDRUMS_SRC) -o $@

$(TEST_RESO): test_reso.cpp $(LIB_SRC) $(RESO_SRC) $(RINGS_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_reso.cpp $(LIB_SRC) $(RESO_SRC) $(RINGS_SRC) -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(TARGET)
	./$(TARGET)

# `test` runs both engine harnesses. Both inject the transport via EngineContext (granular drives the
# real platform Transport; edrums uses a small stub ITransport) - the post-transport-decoupling way.
test: test-granular test-edrums test-reso

test-granular: $(TEST)
	./$(TEST)

test-edrums: $(TEST_EDRUMS)
	./$(TEST_EDRUMS)

test-reso: $(TEST_RESO)
	./$(TEST_RESO)

clean:
	rm -rf $(BUILD)
