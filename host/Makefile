# Desktop host harness for the Spotykach DSP core.
#
# Builds the real src/core/ for the host (no libDaisy: the <daisy.h> shim under host/shim
# supplies the few symbols common.h needs). DaisySP is portable C++ and is compiled from
# source. Run from the repo root:  make -C host   then   ./host/build/host_spotykach

CXX      ?= clang++
ROOT      = ..
BUILD     = build
TARGET    = $(BUILD)/host_spotykach

DAISYSP   = $(ROOT)/lib/DaisySP/Source

# DaisySP .cpp files include sibling headers (e.g. "dsp.h") by bare name, so every
# Source subdirectory must be on the include path.
DAISYSP_INC = $(addprefix -I,$(wildcard $(DAISYSP)/*/))
INCLUDES  = -Ishim -I$(ROOT)/src -I. -I$(DAISYSP) $(DAISYSP_INC)
CXXFLAGS  = -std=c++17 -O2 -Wall -Wno-unused-variable -Wno-unused-but-set-variable $(INCLUDES)

CORE_SRC    = $(wildcard $(ROOT)/src/core/*.cpp)
ENGINE_SRC  = $(wildcard $(ROOT)/src/engine/*.cpp)
# The DisplayModel (engine/display_model.h) now holds hardware-free LEDRing canvases, so the
# host needs the ring + color implementations. These two UI TUs are hardware-free (color.h /
# expose.h only pull common.h via the shim); the rest of src/ui/ stays out (it needs the HAL).
UI_SRC      = $(ROOT)/src/ui/led.ring.cpp $(ROOT)/src/ui/color.cpp
DAISYSP_SRC = $(wildcard $(DAISYSP)/*/*.cpp)
LIB_SRC     = $(CORE_SRC) $(ENGINE_SRC) $(UI_SRC) $(DAISYSP_SRC)

SRC        = main_host.cpp $(LIB_SRC)
TEST       = $(BUILD)/test_engine_params

.PHONY: all clean run test
all: $(TARGET)

$(TARGET): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

$(TEST): test_engine_params.cpp $(LIB_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_engine_params.cpp $(LIB_SRC) -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(TARGET)
	./$(TARGET)

test: $(TEST)
	./$(TEST)

clean:
	rm -rf $(BUILD)
