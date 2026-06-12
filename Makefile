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
# guarded by SPK_USE_STREAM (the platform stream capability) so every non-streaming engine stays
# byte-identical. SPK_USE_STREAM is the feature flag any engine needing SD streaming opts into.
C_DEFS += -DSPK_USE_STREAM
ENGINE_SOURCES = src/engine/tape/tape_engine.cpp
else ifeq ($(ENGINE), reverb)
C_DEFS += -DSPK_ENGINE_REVERB
# Stereo reverb (Dattorro plate / Zita hall) whose DSP is Faust-generated. The cyfaust-generated kernels
# (faust_kernel_<name>.h) are produced from the .dsp sources by `make faust-gen`; the arch shim
# (faust_arch.h) is hand-written + MIT. The kernels' delay-line state is placement-new'd into the SDRAM
# arena, so SRAM stays flat; the link's -Wl,--print-memory-usage shows SRAM_EXEC (the binding region).
ENGINE_SOURCES = src/engine/reverb/reverb_engine.cpp
# Opt-in (make ENGINE=reverb REVERB_GIGAVERB=1): fold the gen~ "gigaverb" export in as a third reverb
# voice selectable via Alt+PITCH. Pulls in the same gen~ glue the standalone gigaverb build uses (the
# arena-bound genlib runtime + the export's _ext_daisy wrapper) and its required defines/includes. Plain
# `make ENGINE=reverb` stays a lean two-voice build with no gen~ toolchain dependency.
ifeq ($(REVERB_GIGAVERB), 1)
C_DEFS += -DSPK_REVERB_GIGAVERB
C_DEFS += -DGENLIB_NO_JSON
C_DEFS += -DDAISY_EXT_NAME=gigaverb
C_DEFS += -DGEN_EXPORTED_NAME=gen_exported
C_DEFS += -DGEN_EXPORTED_HEADER=\"gen_exported.h\"
C_DEFS += -DGEN_EXPORTED_CPP=\"gen_exported.cpp\"
C_DEFS += -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter
GEN_DIR = src/engine/gigaverb
GEN_INC = -I$(GEN_DIR) -I$(GEN_DIR)/gen -I$(GEN_DIR)/gen/gen_dsp
ENGINE_SOURCES += $(GEN_DIR)/_ext_daisy.cpp src/engine/gen/genlib_arena.cpp
endif
# The reverb is route-aware: ONE stereo voice in Stereo/GenerativeStereo, two independent MONO reverbs
# (one per deck) in DoubleMono - chosen at runtime by the Route switch, no build flag. Pair with METER=1
# to read the two-voice (DoubleMono) CPU load on device.
else ifeq ($(ENGINE), shuttle)
C_DEFS += -DSPK_ENGINE_SHUTTLE
# Buffer-based bipolar/reverse "varispeed shuttle" tape: 4 in-SDRAM mono tape buffers (2 decks x 2
# tracks). RECORD needs only the arena; LOAD (load a tape slot from SD into RAM) opts into the shared
# platform streaming service via SPK_USE_STREAM (same flag the tape engine sets), so the platform
# constructs/pumps/injects the StreamDeck and ctx.stream is live on target.
C_DEFS += -DSPK_USE_STREAM
ENGINE_SOURCES = src/engine/shuttle/shuttle_engine.cpp
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
# gen~ engines (ENGINE=gen_<name>) are appended below by scripts/gen_engine.py, one marker-delimited
# `else ifeq` block per export. They use the genlib-isolation bridge from gen-dsp + the shared
# src/engine/gen/ family (GenEngine<W> + the arena-bound genlib runtime). See `make gen-engines`.
# >>> gen:gigaverb >>> (managed by scripts/gen_engine.py)
else ifeq ($(ENGINE), gigaverb)
C_DEFS += -DSPK_ENGINE_GIGAVERB
C_DEFS += -DGENLIB_NO_JSON
C_DEFS += -DDAISY_EXT_NAME=gigaverb
C_DEFS += -DGEN_EXPORTED_NAME=gen_exported
C_DEFS += -DGEN_EXPORTED_HEADER=\"gen_exported.h\"
C_DEFS += -DGEN_EXPORTED_CPP=\"gen_exported.cpp\"
C_DEFS += -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter
GEN_DIR = src/engine/gigaverb
GEN_INC = -I$(GEN_DIR) -I$(GEN_DIR)/gen -I$(GEN_DIR)/gen/gen_dsp
ENGINE_SOURCES = $(GEN_DIR)/_ext_daisy.cpp src/engine/gen/genlib_arena.cpp
# <<< gen:gigaverb <<<
else
$(error Unknown ENGINE '$(ENGINE)' - use 'granular', 'passthrough', 'delay', 'edrums', 'reso', 'tape', 'reverb', or 'shuttle')
endif

# Opt-in (make ... METER=1): enable the on-device CPU load meter (app.cpp's CpuLoadMeter). It prints
# Max/Avg/Min processing load % over the serial log each housekeeping pass - read it over USB serial.
ifeq ($(METER), 1)
C_DEFS += -DMETER
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

C_INCLUDES = -Isrc/ -Ilib/ $(RESO_INC) $(GEN_INC)
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
# The SD-streaming platform TUs are also engine-flag-dependent: their bodies are guarded by
# SPK_USE_STREAM (set by the streaming engines tape/shuttle, so non-streaming engines stay
# byte-identical), so they must rebuild on an engine switch too - otherwise `make ENGINE=tape` over a
# stale non-stream build relinks empty objects (undefined StreamDeck/FatFile/streamMem). Same stamp as app.o.
build/stream_deck.o build/fat_file.o build/buffer.sdram.o: build/.engine-stamp
# gen~ engines share the wrapper object basenames _ext_daisy.o / genlib_arena.o across exports (gen-dsp
# fixes the filenames), so a gen_X -> gen_Y switch must recompile them or the link pulls the previous
# engine's wrapper (undefined-reference to the other's <name>_daisy namespace). Same stamp as app.o.
# NOTE: after `scripts/gen_engine.py --remove`, run `make clean` once - the removed engine's stale
# build/_ext_daisy.d still names its now-deleted source, which make rejects before any recipe runs.
build/_ext_daisy.o build/genlib_arena.o: build/.engine-stamp
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
.PHONY: engine-granular engine-passthrough engine-delay engine-edrums engine-reso engine-tape engine-shuttle engine-reverb
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

engine-shuttle:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=shuttle
	$(MAKE) ENGINE=shuttle program-dfu

# Flash the Faust-generated reverb engine (Dattorro plate / Zita hall).
engine-reverb:
	$(MAKE) clean
	$(MAKE) -j8 ENGINE=reverb
	$(MAKE) ENGINE=reverb program-dfu

# Regenerate every engine's Faust kernel via cyfaust's cpp backend. FAUST_KERNELS lists one
# "<dir>:<namespace-prefix>:<name>" spec per kernel: it compiles <dir>/<name>.dsp -> <dir>/faust_kernel_<name>.h
# with the generated `class mydsp` wrapped in namespace spotykach::<prefix><name> so multiple kernels coexist
# in one build (cyfaust's cpp backend has no class-rename flag, so every kernel is `mydsp` - the namespace
# disambiguates them, and the generated `__mydsp_H__` include guard is renamed to match). The kernel's own
# `#include`s are hoisted to global scope (a namespaced #include would pull <cmath> etc. into the namespace);
# the generated class's unqualified `dsp`/`UI`/`Meta` resolve to the shared arch shim src/engine/faust_arch.h.
# cyfaust (the Cython libfaust wrapper, full cpp backend) lives in a repo-local .venv: `python3 -m venv .venv
# && .venv/bin/pip install cyfaust`. Override the interpreter with `CYFAUST_PY=/path/to/python` to pin a
# different libfaust version. Add a kernel: drop <name>.dsp in its engine dir, add a spec here, and bind it.
CYFAUST_PY ?= .venv/bin/python
FAUST_KERNELS ?= src/engine/reverb:rv_:dattorro src/engine/reverb:rv_:zita src/engine/tape:tfx_:tapefx
.PHONY: faust-gen
faust-gen:
	@for spec in $(FAUST_KERNELS); do \
	  dir=$${spec%%:*}; rest=$${spec#*:}; pfx=$${rest%%:*}; nm=$${rest##*:}; ns=$$pfx$$nm; \
	  src=$$dir/$$nm.dsp; out=$$dir/faust_kernel_$$nm.h; \
	  echo "compiling $$src -> $$out (namespace spotykach::$$ns)"; \
	  $(CYFAUST_PY) -m cyfaust compile $$src -b cpp -o $$dir/.kernel.gen || exit 1; \
	  { \
	    echo '// SYNTHUX ACADEMY /////////////////////////////////////////'; \
	    echo '// SPOTYKACH ///////////////////////////////////////////////'; \
	    echo '#pragma once'; echo ''; \
	    echo "// GENERATED FILE - do not edit by hand. Regenerate with \`make faust-gen\` (cyfaust cpp backend)."; \
	    echo "// Source: $$src. The generated \`class mydsp\` is wrapped in namespace spotykach::$$ns; its"; \
	    echo "// dsp/UI/Meta base types resolve to the shared arch shim (see engine/faust_arch.h)."; echo ''; \
	    grep '^#include' $$dir/.kernel.gen; \
	    echo '#include "engine/faust_arch.h"'; echo ''; \
	    echo "namespace spotykach { namespace $$ns {"; \
	    grep -v '^#include' $$dir/.kernel.gen | sed "s/__mydsp_H__/__$${ns}_H__/g"; \
	    echo "} } // namespace spotykach::$$ns"; \
	  } > $$out; \
	  rm -f $$dir/.kernel.gen; \
	done
	@echo "regenerated faust kernels"

# Regenerate every gen~ engine via gen-dsp's Daisy backend (the gen~ analogue of faust-gen).
# GEN_EXPORTS lists one "<gen~-export-dir>:<name>" spec per engine: scripts/gen_engine.py runs gen-dsp
# into src/engine/gen_<name>/, keeps only the genlib-isolation bridge (drops gen-dsp's board main and
# private allocator), emits <name>_engine.h (a ParamId map you retune by hand -- preserved across
# re-runs unless --force-glue), and wires the ENGINE switch + engine_select.h in marker-delimited
# blocks. gen-dsp lives in the repo-local .venv alongside cyfaust (`.venv/bin/pip install -e <gen-dsp>`).
# Add an engine: drop its spec here (or run the script directly), then `make ENGINE=<name>`.
GEN_PY ?= .venv/bin/python
# One "<gen~-export-dir>:<name>" spec per engine. gigaverb points at its own vendored export (the copy
# gen-dsp dropped under the engine dir is itself a valid gen-dsp input), so a regen is reproducible on
# any checkout without the external gen-dsp source tree. For a new engine, point at your gen~ export dir.
GEN_EXPORTS ?= src/engine/gigaverb/gen:gigaverb
.PHONY: gen-engines
gen-engines:
	@test -n "$(GEN_EXPORTS)" || { echo "set GEN_EXPORTS='<export-dir>:<name> ...' (or run scripts/gen_engine.py directly)"; exit 1; }
	@for spec in $(GEN_EXPORTS); do \
	  export=$${spec%:*}; name=$${spec##*:}; \
	  $(GEN_PY) scripts/gen_engine.py $$export $$name || exit 1; \
	done
	@echo "regenerated gen~ engines"

# Run the Python script test suites (scripts/test_*.py). These cover host-side utilities
# like convert_tape_audio.py and need neither hardware nor a firmware build. pytest is part
# of the `dev` dependency group declared in pyproject.toml ([dependency-groups], PEP 735),
# installed into the repo-local .venv. Override the interpreter with `TEST_PY=/path/to/python`.
# `make test-scripts` installs the dev group on first use (when pytest is missing);
# `make test-scripts-deps` (re)installs it on demand. Needs pip >= 25.1 for `--group`.
TEST_PY ?= .venv/bin/python
.PHONY: test-scripts test-scripts-deps
test-scripts-deps:
	$(TEST_PY) -m pip install -q --group dev
test-scripts:
	@$(TEST_PY) -c 'import pytest' 2>/dev/null || $(TEST_PY) -m pip install -q --group dev
	$(TEST_PY) -m pytest scripts/

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
