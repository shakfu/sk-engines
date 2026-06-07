# Thin Make frontend over the CMake build (WIP).
#
# Preserves the muscle-memory commands while CMake does the real work:
#   make -f Makefile.cmake                  # configure + build the default (granular) engine
#   make -f Makefile.cmake ENGINE=passthrough
#   make -f Makefile.cmake program-dfu      # build (if stale) + flash over DFU
#   make -f Makefile.cmake engine-edrums    # build + flash a variant in one shot
#   make -f Makefile.cmake clean
#   make -f Makefile.cmake DEBUG=1          # config toggles pass straight through
#
# Both build systems coexist: the root `Makefile` stays the canonical, hardware-proven build (output in
# `build/`); this CMake path is an opt-in alternative (output in `build-cmake/<engine>/`). Renaming this
# file to `Makefile` to make CMake the default is a *deferred* option, not done - it would only be worth
# it once the CMake build has been flashed and trusted as much as the Make one. The recursive calls below
# use $(THIS) so they keep working under either name if that rename ever happens.
#
# Each engine gets its own cached build dir (build-cmake/<engine>), so switching engines never forces
# a reconfigure-in-place or a clean -- this is what retires the old `.engine-stamp` clean/rebuild hack.

THIS   := $(lastword $(MAKEFILE_LIST))
ENGINE ?= granular
JOBS   ?= 8
BUILD  := build-cmake/$(ENGINE)

# Config toggles -> CMake -D flags (mirror the old Makefile's DEBUG / LOFI_INT16).
CMAKE_FLAGS :=
ifeq ($(DEBUG),1)
CMAKE_FLAGS += -DDEBUG=1
endif
ifeq ($(LOFI_INT16),1)
CMAKE_FLAGS += -DLOFI_INT16=1
endif

.PHONY: all build configure clean check-boundary program-dfu program-boot \
        engine-granular engine-passthrough engine-delay engine-edrums engine-reso

all: build

# Configure once per engine dir. `cmake --build` below auto-re-runs cmake if CMakeLists.txt changes,
# so this rule only fires for a fresh dir.
$(BUILD)/CMakeCache.txt:
	cmake -S . -B $(BUILD) -G "Unix Makefiles" -DENGINE=$(ENGINE) \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_FLAGS)

configure: $(BUILD)/CMakeCache.txt

build: check-boundary configure
	cmake --build $(BUILD) -j$(JOBS)

# Boundary guard carried over verbatim from the old Makefile (grep-enforced platform/engine split).
# The compiler-enforced version (per-target include roots) is a follow-on, not done in this spike.
PLATFORM_DIRS := src/hw src/ui src/memory src/transport
check-boundary:
	@if grep -rn '#include "engine/granular/\|#include "\.\./engine/granular/' $(PLATFORM_DIRS) ; then \
		echo "*** BOUNDARY VIOLATION: a platform TU (hw/ui/memory) includes granular DSP (above)."; \
		exit 1; \
	fi
	@echo "boundary OK: hw/ui/memory include no engine/granular/ headers"

program-dfu: configure
	cmake --build $(BUILD) --target program-dfu

program-boot: configure
	cmake --build $(BUILD) --target program-boot

clean:
	rm -rf build-cmake

# One-shot variant flash (parity with the old engine-* targets). No `clean` needed: each engine has its
# own cached dir, so there is no stale-engine contamination to wipe.
engine-granular:
	$(MAKE) -f $(THIS) ENGINE=granular build program-dfu
engine-passthrough:
	$(MAKE) -f $(THIS) ENGINE=passthrough build program-dfu
engine-delay:
	$(MAKE) -f $(THIS) ENGINE=delay build program-dfu
engine-edrums:
	$(MAKE) -f $(THIS) ENGINE=edrums build program-dfu
engine-reso:
	$(MAKE) -f $(THIS) ENGINE=reso build program-dfu
