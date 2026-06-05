# Desktop host harness for the Spotykach DSP core.
#
# Builds the real src/engine/granular/ for the host (no libDaisy: the <daisy.h> shim under host/shim
# supplies the few symbols common.h needs). DaisySP is portable C++ and is compiled from
# source. Run from the repo root:  make -C host   then   ./host/build/host_spotykach

CXX      ?= clang++
ROOT      = ..
BUILD     = build
TARGET    = $(BUILD)/host_spotykach

DAISYSP   = $(ROOT)/lib/DaisySP/Source

# DaisySP .cpp files include sibling headers (e.g. "dsp.h") by bare name, so every Source subdirectory
# must be on the include path. src/transport/ headers (transport.h) likewise pull siblings
# (synclock.h/tempo.h) by bare name, so that dir is on the path too for TUs that include the transport.
DAISYSP_INC = $(addprefix -I,$(wildcard $(DAISYSP)/*/))
INCLUDES  = -Ishim -I$(ROOT)/src -I$(ROOT)/src/transport -I. -I$(DAISYSP) $(DAISYSP_INC)
CXXFLAGS  = -std=c++17 -O2 -Wall -Wno-unused-variable -Wno-unused-but-set-variable $(INCLUDES)

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
# The edrums engine lives under src/engine/edrums/ (outside the wildcards above); add it for its test.
EDRUMS_SRC  = $(ROOT)/src/engine/edrums/edrums_engine.cpp

SRC          = main_host.cpp $(LIB_SRC)
TEST         = $(BUILD)/test_engine_params
TEST_EDRUMS  = $(BUILD)/test_edrums_slots

.PHONY: all clean run test test-granular test-edrums
all: $(TARGET)

$(TARGET): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

$(TEST): test_engine_params.cpp $(LIB_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_engine_params.cpp $(LIB_SRC) -o $@

$(TEST_EDRUMS): test_edrums_slots.cpp $(LIB_SRC) $(EDRUMS_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_edrums_slots.cpp $(LIB_SRC) $(EDRUMS_SRC) -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(TARGET)
	./$(TARGET)

# `test` runs both engine harnesses. Both inject the transport via EngineContext (granular drives the
# real platform Transport; edrums uses a small stub ITransport) - the post-transport-decoupling way.
test: test-granular test-edrums

test-granular: $(TEST)
	./$(TEST)

test-edrums: $(TEST_EDRUMS)
	./$(TEST_EDRUMS)

clean:
	rm -rf $(BUILD)
