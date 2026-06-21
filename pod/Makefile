# Standalone Csound-on-Daisy proof of life. Builds a BOOT_QSPI app (code in QSPI
# flash, heap in SDRAM) linked against the cross-compiled libcsound.a. This is
# deliberately separate from the sk-engines SRAM firmware/Makefile.
#
#   make            - build the harness (CsoundEngine behind IEngine)
#   make program-dfu- flash over USB DFU (catch the v5.4 bootloader's DFU window)
#
# NOTE: this uses the stock Daisy bootloader (dsy_bootloader_v5_4.bin), not the
# spotykach SRAM bootloader. Flashing it replaces the bootloader; reflash the
# spotykach bootloader to go back to the SRAM engines.

APP_TYPE = BOOT_QSPI
LDFLAGS += -u _printf_float

# The sk-engines contract headers (common.h: std::clamp) need C++17; libDaisy defaults to gnu++14.
CPP_STANDARD = -std=gnu++17

TARGET      = harness
CPP_SOURCES = harness_csound.cpp ../src/engine/csound/csound_engine.cpp

# Daisy libraries (vendored in the sk-engines repo)
LIBDAISY_DIR = ../lib/libDaisy
DAISYSP_DIR  = ../lib/DaisySP

# Cross-compiled Csound static library + headers (built via thirdparty/csound)
CSOUND_BASE        = ../thirdparty/csound/Daisy
CSOUND_INCLUDE_DIR = $(CSOUND_BASE)/include/csound
CSOUND_LIB_DIR     = $(CSOUND_BASE)/lib
CSOUND_STATIC_LIB  = $(CSOUND_LIB_DIR)/libcsound.a

C_INCLUDES += -I../src                 # sk-engines contract headers (engine/iengine.h etc.)
C_INCLUDES += -I$(CSOUND_INCLUDE_DIR)
LIBS       += $(CSOUND_STATIC_LIB)
LIBDIR     += -L$(CSOUND_LIB_DIR)

# Daisy v5.4 bootloader + the SDRAM-heap QSPI linker script from the Csound port
BOOT_BIN = $(CSOUND_BASE)/DaisyCsoundExamples/dsy_bootloader_v5_4.bin
LDSCRIPT = $(CSOUND_BASE)/DaisyCsoundExamples/STM32H750IB_qspi_custom.lds

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
