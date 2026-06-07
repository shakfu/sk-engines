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
else ifeq ($(ENGINE), karp)
C_DEFS += -DSPK_ENGINE_KARP
# stmlib's filters use M_PI, which strict -std=c++17 does not expose from <cmath> on arm-none-eabi.
C_DEFS += -DM_PI=3.14159265358979323846
# The Rings DSP (~30K of code+tables) overflows the 186K execution SRAM at -O2. Build karp at -Os to
# fit; the M7 at 480 MHz has ample headroom (Rings shipped on a 168 MHz F4). Scoped to this engine.
OPT = -Os
# karp's DSP is the Mutable Instruments Rings engine + its stmlib support library, vendored (trimmed to
# the used closure) under src/engine/karp/thirdparty/. KARP_INC scopes that include to the karp build
# (referenced from C_INCLUDES below; empty for other engines). The .cc files compile on-target WITHOUT
# -DTEST, so stmlib uses its Cortex-M ssat/usat fast paths.
KARP_TP  = src/engine/karp/thirdparty
KARP_INC = -I$(KARP_TP)
ENGINE_SOURCES = src/engine/karp/karp_engine.cpp \
	$(KARP_TP)/rings/dsp/part.cc $(KARP_TP)/rings/dsp/string.cc \
	$(KARP_TP)/rings/dsp/resonator.cc $(KARP_TP)/rings/dsp/fm_voice.cc \
	$(KARP_TP)/rings/resources.cc \
	$(KARP_TP)/stmlib/dsp/units.cc $(KARP_TP)/stmlib/utils/random.cc $(KARP_TP)/stmlib/dsp/atan.cc
else
$(error Unknown ENGINE '$(ENGINE)' - use 'granular', 'passthrough', 'delay', 'edrums', or 'karp')
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

C_INCLUDES = -Isrc/ -Ilib/ $(KARP_INC)
C_USR_FLAGS = -ffast-math -funroll-loops
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
.PHONY: engine-granular engine-passthrough engine-delay engine-edrums engine-karp
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

engine-karp:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=karp
	$(MAKE) ENGINE=karp program-dfu

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
