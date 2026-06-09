# Config Options
# DEBUG=1
# LOFI_INT16=1   # store the loop buffer as 16-bit PCM (doubles record time to 84s)

ifeq ($(DEBUG), 1)
C_DEFS += -DINFS_LOG=1
endif

ifeq ($(LOFI_INT16), 1)
C_DEFS += -DLOFI_INT16=1
endif

# Swappable DSP engine selected at build time (item 3b). Default = the granular looper.
# `make ENGINE=passthrough` builds the minimal passthrough variant. The define drives
# src/engine/engine_select.h (-> ActiveEngine); ENGINE_SOURCES compiles only the chosen engine.
ENGINE ?= granular
ifeq ($(ENGINE), granular)
C_DEFS += -DSPK_ENGINE_GRANULAR
# Granular engine = its IEngine wrapper (granular_engine.cpp) + all the DSP, all under src/engine/granular/.
ENGINE_SOURCES = $(wildcard src/engine/granular/*.cpp)
else ifeq ($(ENGINE), passthrough)
C_DEFS += -DSPK_ENGINE_PASSTHROUGH
# Passthrough engine is header-only (src/engine/passthrough/); no engine .cpp to compile.
ENGINE_SOURCES =
else ifeq ($(ENGINE), delay)
C_DEFS += -DSPK_ENGINE_DELAY
ENGINE_SOURCES = src/engine/delay/delay_engine.cpp
else ifeq ($(ENGINE), edrums)
C_DEFS += -DSPK_ENGINE_EDRUMS
ENGINE_SOURCES = src/engine/edrums/edrums_engine.cpp
else ifeq ($(ENGINE), tape)
C_DEFS += -DSPK_ENGINE_TAPE
# Streaming tape engine = its IEngine wrapper (tape_engine.cpp). Its SD streaming service
# (src/hw/stream_deck.cpp + fat_file.cpp) compiles via the platform src/hw/ wildcard, with bodies
# guarded by SPK_ENGINE_TAPE so every other engine stays byte-identical.
ENGINE_SOURCES = src/engine/tape/tape_engine.cpp
else ifeq ($(ENGINE), faust)
C_DEFS += -DSPK_ENGINE_FAUST
# FAUST-SPIKE (throwaway): a cyfaust-generated DSP kernel wrapped behind IEngine, to measure
# generated-code SRAM_EXEC cost on the H7. The kernel header (faust_dsp.h) is generated from
# voice.dsp by `make faust-gen`; the arch shim (faust_arch.h) is hand-written + MIT. The core
# Makefile already links with -Wl,--print-memory-usage, so the build prints the SRAM_EXEC usage.
ENGINE_SOURCES = src/engine/faust/faust_engine.cpp
else ifeq ($(ENGINE), reso)
C_DEFS += -DSPK_ENGINE_RESO
# stmlib's filters use M_PI, which strict -std=c++17 does not expose from <cmath> on arm-none-eabi.
C_DEFS += -DM_PI=3.14159265358979323846
# The Rings DSP (~30K of code+tables) overflows the 186K execution SRAM at -O2. Build reso at -Os to
# fit; the M7 at 480 MHz has ample headroom (Rings shipped on a 168 MHz F4). Scoped to this engine.
OPT = -Os
# reso's DSP is the Mutable Instruments Rings engine + its stmlib support library, vendored (trimmed to
# the used closure) under src/engine/reso/thirdparty/. RESO_INC scopes that include to the reso build
# (referenced from C_INCLUDES below; empty for other engines). The .cc files compile on-target WITHOUT
# -DTEST, so stmlib uses its Cortex-M ssat/usat fast paths.
RESO_TP  = src/engine/reso/thirdparty
RESO_INC = -I$(RESO_TP)
ENGINE_SOURCES = src/engine/reso/reso_engine.cpp \
	$(RESO_TP)/rings/dsp/part.cc $(RESO_TP)/rings/dsp/string.cc \
	$(RESO_TP)/rings/dsp/resonator.cc $(RESO_TP)/rings/dsp/fm_voice.cc \
	$(RESO_TP)/rings/resources.cc \
	$(RESO_TP)/stmlib/dsp/units.cc $(RESO_TP)/stmlib/utils/random.cc $(RESO_TP)/stmlib/dsp/atan.cc
else
$(error Unknown ENGINE '$(ENGINE)' - use 'granular', 'passthrough', 'delay', 'edrums', 'reso', 'tape', or 'faust')
endif

USE_FATFS = 1

# Project Name
TARGET = spotykach

CPP_STANDARD = -std=c++17

LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR = lib/DaisySP
CMSIS_DSP_SRC_DIR = ${LIBDAISY_DIR}/Drivers/CMSIS-DSP/Source

# Daisy Bootloader - SRAM Linkage
APP_TYPE = BOOT_SRAM
LDSCRIPT = alt_sram.lds
BOOT_BIN = bootloader-spotykach-v2.bin

C_INCLUDES = -Isrc/ -Ilib/ $(RESO_INC)
# NOTE: there used to be `C_USR_FLAGS = -ffast-math -funroll-loops` here, but the core Makefile reads
# C_USER_FLAGS (with the E), so it was dead - those flags never reached the compiler and the shipping
# firmware was built without them. Removed to stop it reading as active. The device meets its CPU/SRAM
# budget without them, so do NOT just rename the var: -ffast-math changes FP semantics (implies
# -ffinite-math-only, dropping isnan/isinf guards) and -funroll-loops inflates .text (SRAM_EXEC is
# ~94% full). Enabling fast-math/FTZ is a deliberate, measured, hardware-flashed change - batch with P2.
C_DEFS += -DINFS_LOG_TARGET=daisy::LOGGER_EXTERNAL

CPP_SOURCES = \
	main.cpp \
	src/app.cpp \
	$(ENGINE_SOURCES) \
	src/engine/color.cpp \
	src/engine/led.ring.cpp \
	$(wildcard src/transport/*.cpp) \
	$(wildcard src/dsp/*.cpp) \
	$(wildcard src/hw/*.cpp) \
	$(wildcard src/ui/*.cpp) \
	$(wildcard src/memory/*.cpp)

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Rebuild the engine-dependent object when ENGINE changes. Objects are compiled with -DSPK_ENGINE_*,
# but make can't see flag changes, so `make ENGINE=passthrough` over a stale granular build would
# relink the wrong engine (undefined-reference to the other engine's vtable). app.cpp is the only TU
# that includes engine_select.h, so make it depend on a stamp whose content is rewritten only when
# ENGINE differs -> app.o rebuilds exactly on a switch, no manual `make clean` needed.
build/app.o: build/.engine-stamp
# The tape engine's SD-streaming platform TUs are also engine-flag-dependent: their bodies are guarded
# by SPK_ENGINE_TAPE (so other engines stay byte-identical), so they must rebuild on an engine switch
# too - otherwise `make ENGINE=tape` over a stale non-tape build relinks empty objects (undefined
# StreamDeck/FatFile/streamMem). Same stamp mechanism as app.o.
build/stream_deck.o build/fat_file.o build/buffer.sdram.o: build/.engine-stamp
build/.engine-stamp: FORCE
	@mkdir -p build
	@echo '$(ENGINE)' | cmp -s - $@ 2>/dev/null || echo '$(ENGINE)' > $@
.PHONY: FORCE
FORCE:

# Platform/engine boundary guard (Phase 5 R4b). The platform (hw/ui/memory) must reach the engine
# ONLY through the contract headers in src/engine/, never the granular DSP under src/engine/granular/.
# app.cpp is exempt: it is the composition root that instantiates the concrete ActiveEngine via
# engine_select.h. Wired as a prerequisite of `all`, so a `make` that reintroduces a granular include
# into hw/ui/memory fails - the boundary is enforced by the build, not by convention/review.
PLATFORM_DIRS = src/hw src/ui src/memory src/transport
.PHONY: check-boundary
check-boundary:
	@if grep -rn '#include "engine/granular/\|#include "\.\./engine/granular/' $(PLATFORM_DIRS) ; then \
		echo "*** BOUNDARY VIOLATION: a platform TU (hw/ui/memory) includes granular DSP (above)."; \
		echo "*** The platform must use only the contract headers in src/engine/. See docs/engine-layout.md."; \
		exit 1; \
	fi
	@echo "boundary OK: hw/ui/memory include no engine/granular/ headers"

all: check-boundary

# One-shot variant flash: clean -> build -> flash over DFU. Put the device in DFU mode first
# (hold Reset ~3s until the bottom pad LEDs breathe white), then `make granular` / `make passthrough`.
.PHONY: engine-granular engine-passthrough engine-delay engine-edrums engine-reso engine-tape engine-faust
engine-granular:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=granular
	$(MAKE) ENGINE=granular program-dfu

engine-passthrough:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=passthrough
	$(MAKE) ENGINE=passthrough program-dfu

engine-delay:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=delay
	$(MAKE) ENGINE=delay program-dfu

engine-edrums:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=edrums
	$(MAKE) ENGINE=edrums program-dfu

engine-reso:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=reso
	$(MAKE) ENGINE=reso program-dfu

engine-tape:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=tape
	$(MAKE) ENGINE=tape program-dfu

# FAUST-SPIKE (throwaway). Flash the cyfaust-generated voice engine.
engine-faust:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=faust
	$(MAKE) ENGINE=faust program-dfu

# Regenerate src/engine/faust/faust_dsp.h from voice.dsp via cyfaust's cpp backend, re-prepending the
# project preamble (the generated body is appended verbatim). cyfaust (the Cython libfaust wrapper, full
# cpp backend) lives in a repo-local .venv: `python3 -m venv .venv && .venv/bin/pip install cyfaust`.
# Override the interpreter with `make faust-gen CYFAUST_PY=/path/to/python-with-cyfaust` to pin a
# different libfaust version.
CYFAUST_PY ?= .venv/bin/python
FAUST_DIR   = src/engine/faust
.PHONY: faust-gen
faust-gen:
	$(CYFAUST_PY) -m cyfaust compile $(FAUST_DIR)/voice.dsp -b cpp -o $(FAUST_DIR)/.kernel.gen
	@{ \
	  echo '// SYNTHUX ACADEMY /////////////////////////////////////////'; \
	  echo '// SPOTYKACH ///////////////////////////////////////////////'; \
	  echo '#pragma once'; echo ''; \
	  echo '// GENERATED FILE - do not edit by hand. Regenerate with `make faust-gen` (cyfaust cpp backend).'; \
	  echo '// Source: src/engine/faust/voice.dsp. Class name is the Faust default `mydsp` (global namespace);'; \
	  echo '// FaustEngine refers to it as ::mydsp. The arch shim below provides the dsp/UI/Meta base types the'; \
	  echo '// generated class assumes (see faust_arch.h for why we declare them ourselves rather than vendor'; \
	  echo "// Faust's GPL-with-exception headers)."; echo ''; \
	  echo '#include "engine/faust/faust_arch.h"'; echo ''; \
	  cat $(FAUST_DIR)/.kernel.gen; \
	} > $(FAUST_DIR)/faust_dsp.h
	@rm -f $(FAUST_DIR)/.kernel.gen
	@echo "regenerated $(FAUST_DIR)/faust_dsp.h"

# Vendored Daisy archives. The core Makefile's link step (-ldaisy -ldaisysp) needs these built, but a
# fresh checkout has source-only submodules, so a bare `make` used to fail at link with "cannot find
# -ldaisy". Wire each archive as a file prerequisite of the elf with its own build rule: a plain `make`
# builds a missing archive on demand, but the rules have no prerequisites so the sub-make never re-runs
# on a normal rebuild once the .a exists. `make libs` still force-rebuilds; pair with `clean-libs`.
LIBDAISY_A = $(LIBDAISY_DIR)/build/libdaisy.a
DAISYSP_A = $(DAISYSP_DIR)/build/libdaisysp.a

$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_A) $(DAISYSP_A)

$(LIBDAISY_A):
	cd $(LIBDAISY_DIR) && $(MAKE)

$(DAISYSP_A):
	cd $(DAISYSP_DIR) && $(MAKE)

.PHONY: libs
libs:
	cd $(LIBDAISY_DIR) && $(MAKE)
	cd $(DAISYSP_DIR) && $(MAKE)

clean-libs:
	cd $(LIBDAISY_DIR) && $(MAKE) clean
	cd $(DAISYSP_DIR) && $(MAKE) clean
