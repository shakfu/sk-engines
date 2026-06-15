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
# The edrums/reso/reverb engines live under src/engine/<name>/ (outside the wildcards above); add for tests.
DELAY_SRC   = $(ROOT)/src/engine/delay/delay_engine.cpp
EDRUMS_SRC  = $(ROOT)/src/engine/edrums/edrums_engine.cpp
RESO_SRC    = $(ROOT)/src/engine/reso/reso_engine.cpp
# The reverb engine's adapter; its Faust kernels are checked-in headers (engine/reverb/faust_kernel_*.h,
# portable C++) pulled by the .cpp, so no extra sources are needed.
REVERB_SRC  = $(ROOT)/src/engine/reverb/reverb_engine.cpp
# The tape engine + its Faust tape-FX kernel (engine/tape/faust_kernel_tapefx.h, header pulled by the .cpp).
TAPE_SRC    = $(ROOT)/src/engine/tape/tape_engine.cpp
# The buffer-based shuttle engine (4 in-RAM tape tracks; bipolar/reverse varispeed). Self-contained.
SHUTTLE_SRC = $(ROOT)/src/engine/shuttle/shuttle_engine.cpp
RADIO_SRC   = $(ROOT)/src/engine/radio/radio_engine.cpp
# The SPK_REVERB_GIGAVERB build folds the gen~ "gigaverb" export into ReverbEngine as a third voice.
# It needs the gen~ glue (the export wrapper + the arena-bound genlib runtime), its include dirs, and
# the same defines the firmware build sets. Mirrors the ENGINE=reverb REVERB_GIGAVERB=1 Makefile branch.
# GENLIB_USE_FLOAT32 makes gen~'s t_sample == float, matching what the firmware compiles (genlib_platform.h
# defines it for __arm__). The gen bridge marshals float** buffers and the engine sizes the gen~ arena
# slice for float; without this the host would default t_sample to double (8 B), doubling the footprint
# and breaking the float** marshalling. Set it so the host test exercises the shipped configuration.
GIGA_DEFS = -DSPK_REVERB_GIGAVERB -DGENLIB_NO_JSON -DGENLIB_USE_FLOAT32 -DDAISY_EXT_NAME=gigaverb \
            -DGEN_EXPORTED_NAME=gen_exported -DGEN_EXPORTED_HEADER='"gen_exported.h"' -DGEN_EXPORTED_CPP='"gen_exported.cpp"' \
            -Wno-unused-function -Wno-unused-parameter
GIGA_INC  = -I$(ROOT)/src/engine/gigaverb -I$(ROOT)/src/engine/gigaverb/gen -I$(ROOT)/src/engine/gigaverb/gen/gen_dsp
GIGA_SRC  = $(ROOT)/src/engine/gigaverb/_ext_daisy.cpp $(ROOT)/src/engine/gen/genlib_arena.cpp
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
TEST_REVERB  = $(BUILD)/test_reverb
TEST_TAPE    = $(BUILD)/test_tape
TEST_STREAM  = $(BUILD)/test_stream
TEST_SHUTTLE = $(BUILD)/test_shuttle
TEST_RADIO   = $(BUILD)/test_radio
TEST_CHORUS  = $(BUILD)/test_chorus
TEST_REVERB_GIGA = $(BUILD)/test_reverb_giga
TEST_DELAY   = $(BUILD)/test_delay
BENCH_REVERB     = $(BUILD)/bench_reverb

.PHONY: all clean run test test-granular test-edrums test-reso test-reverb test-reverb-giga test-delay test-tape test-stream test-shuttle test-radio test-chorus bench-reverb
all: $(TARGET)

$(TARGET): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

$(TEST): test_engine_params.cpp $(LIB_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_engine_params.cpp $(LIB_SRC) -o $@

$(TEST_EDRUMS): test_edrums_slots.cpp $(LIB_SRC) $(EDRUMS_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_edrums_slots.cpp $(LIB_SRC) $(EDRUMS_SRC) -o $@

$(TEST_RESO): test_reso.cpp $(LIB_SRC) $(RESO_SRC) $(RINGS_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_reso.cpp $(LIB_SRC) $(RESO_SRC) $(RINGS_SRC) -o $@

# The generated Faust kernels are headers (not in $(*_SRC)), so list them explicitly as prerequisites
# - else `make faust-gen` followed by `make test-*` would relink against a stale binary.
$(TEST_REVERB): test_reverb.cpp $(LIB_SRC) $(REVERB_SRC) $(wildcard $(ROOT)/src/engine/reverb/faust_kernel_*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) test_reverb.cpp $(LIB_SRC) $(REVERB_SRC) -o $@

$(TEST_TAPE): test_tape.cpp $(LIB_SRC) $(TAPE_SRC) $(ROOT)/src/engine/tape/faust_kernel_tapefx.h | $(BUILD)
	$(CXX) $(CXXFLAGS) test_tape.cpp $(LIB_SRC) $(TAPE_SRC) -o $@

$(TEST_DELAY): test_delay.cpp $(LIB_SRC) $(DELAY_SRC) $(ROOT)/src/engine/delay/delay_engine.h | $(BUILD)
	$(CXX) $(CXXFLAGS) test_delay.cpp $(LIB_SRC) $(DELAY_SRC) -o $@

$(TEST_SHUTTLE): test_shuttle.cpp $(LIB_SRC) $(SHUTTLE_SRC) $(ROOT)/src/engine/shuttle/shuttle_engine.h | $(BUILD)
	$(CXX) $(CXXFLAGS) test_shuttle.cpp $(LIB_SRC) $(SHUTTLE_SRC) -o $@

# Radio engine + the headerless raw-stream codec (raw_stream.h, header pulled by the test).
$(TEST_RADIO): test_radio.cpp $(LIB_SRC) $(RADIO_SRC) $(ROOT)/src/engine/radio/radio_engine.h \
               $(ROOT)/src/memory/raw_stream.h | $(BUILD)
	$(CXX) $(CXXFLAGS) test_radio.cpp $(LIB_SRC) $(RADIO_SRC) -o $@

# Generated Faust `chorus` engine - header-only (the cyfaust kernel + the shared FaustEngine wrapper),
# so it links against LIB_SRC with no extra engine .cpp.
$(TEST_CHORUS): test_chorus.cpp $(LIB_SRC) $(ROOT)/src/engine/chorus/chorus_engine.h \
                $(ROOT)/src/engine/chorus/faust_kernel_chorus.h | $(BUILD)
	$(CXX) $(CXXFLAGS) test_chorus.cpp $(LIB_SRC) -o $@

# Verifies the gigaverb-as-third-voice integration (SPK_REVERB_GIGAVERB). Not in the default `test`
# aggregate: it compiles the gen~ runtime, which the lean reverb build deliberately excludes.
$(TEST_REVERB_GIGA): test_reverb_giga.cpp $(LIB_SRC) $(REVERB_SRC) $(GIGA_SRC) \
                     $(wildcard $(ROOT)/src/engine/reverb/faust_kernel_*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(GIGA_DEFS) $(GIGA_INC) test_reverb_giga.cpp $(LIB_SRC) $(REVERB_SRC) $(GIGA_SRC) -o $@

# Reverb process() cost benchmark. One binary times both topologies (Stereo vs DoubleMono) by switching
# the Route at runtime. The ratio is the portable signal; absolute us is host time, not device load.
REVERB_KERNELS = $(wildcard $(ROOT)/src/engine/reverb/faust_kernel_*.h)
$(BENCH_REVERB): bench_reverb.cpp $(LIB_SRC) $(REVERB_SRC) $(REVERB_KERNELS) | $(BUILD)
	$(CXX) $(CXXFLAGS) bench_reverb.cpp $(LIB_SRC) $(REVERB_SRC) -o $@

bench-reverb: $(BENCH_REVERB)
	./$(BENCH_REVERB)

# The SD-streaming core (SpscRing + Play/RecordStream) is self-contained header-only logic with no
# engine/Daisy/FatFs deps, so its test compiles on its own.
$(TEST_STREAM): test_stream.cpp $(ROOT)/src/memory/spsc_ring.h $(ROOT)/src/memory/audio_stream.h \
                $(ROOT)/src/memory/byte_file.h $(ROOT)/src/memory/wav_stream.h $(ROOT)/src/memory/wav.h | $(BUILD)
	$(CXX) $(CXXFLAGS) test_stream.cpp -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(TARGET)
	./$(TARGET)

# `test` runs both engine harnesses. Both inject the transport via EngineContext (granular drives the
# real platform Transport; edrums uses a small stub ITransport) - the post-transport-decoupling way.
test: test-granular test-edrums test-reso test-reverb test-reverb-giga test-delay test-tape test-stream test-shuttle test-radio test-chorus

test-granular: $(TEST)
	./$(TEST)

test-edrums: $(TEST_EDRUMS)
	./$(TEST_EDRUMS)

test-reso: $(TEST_RESO)
	./$(TEST_RESO)

test-reverb: $(TEST_REVERB)
	./$(TEST_REVERB)

test-reverb-giga: $(TEST_REVERB_GIGA)
	./$(TEST_REVERB_GIGA)

test-delay: $(TEST_DELAY)
	./$(TEST_DELAY)

test-tape: $(TEST_TAPE)
	./$(TEST_TAPE)

test-stream: $(TEST_STREAM)
	./$(TEST_STREAM)

test-shuttle: $(TEST_SHUTTLE)
	./$(TEST_SHUTTLE)

test-radio: $(TEST_RADIO)
	./$(TEST_RADIO)

test-chorus: $(TEST_CHORUS)
	./$(TEST_CHORUS)

clean:
	rm -rf $(BUILD)
