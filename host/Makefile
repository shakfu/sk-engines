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

CORE_SRC   = $(wildcard $(ROOT)/src/core/*.cpp)
DAISYSP_SRC = $(wildcard $(DAISYSP)/*/*.cpp)
SRC        = main_host.cpp $(CORE_SRC) $(DAISYSP_SRC)

.PHONY: all clean run
all: $(TARGET)

$(TARGET): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD)
