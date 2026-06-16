# SPIKE (TODO P3): evaluate converting the firmware build to CMake.
# Goal is parity with the proven Make build, NOT idiomatic-CMake defaults.
# The Make build compiles app TUs at -O2/no-LTO/c++17, defines BOOT_APP, and links
# the custom alt_sram.lds for the SRAM bootloader. Each of those is reproduced below;
# the comments flag every place where DaisyProject.cmake's defaults would diverge.

cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0135 NEW)

# Autodetect the arm-none-eabi toolchain before project() (mirrors libDaisy's own CMakeLists).
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    include(${CMAKE_CURRENT_LIST_DIR}/lib/libDaisy/cmake/toolchains/autodetect.cmake)
endif()

project(SK_ENGINES VERSION 0.3.2 LANGUAGES C CXX ASM)

# PARITY (opt levels): the Make build is deliberately split -- libDaisy and DaisySP compile at -O3,
# the app at -O2 -- and uses no LTO. DaisyProject forces -O3+LTO on Release and -Og on Debug; neither
# matches. Debug is the only config that does NOT add -flto, so we build Debug and steer opt levels by
# hand below. NDEBUG is also defined only on Release/MinSizeRel and Make never defines it, so Debug
# matches there too.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# PARITY (libs at -O3): set -O3 as a directory option BEFORE add_subdirectory so every libDaisy and
# DaisySP target inherits it at creation (directory COMPILE_OPTIONS propagate to subdirs added after
# the call). The app target, created later by DaisyProject, also inherits this -O3 but is overridden
# back to -O2 after include(DaisyProject) -- a later target-level -O2 wins on the command line.
add_compile_options($<$<CONFIG:Debug>:-O3>)
# Match DaisyProject's own daisy_targets() opt push (applied to the daisy lib) to -O3 as well, so it
# does not fight the directory -O3 with a -O2.
set(FIRMWARE_DEBUG_OPT_LEVEL -O3)

# libDaisy + DaisySP as subprojects. add_subdirectory(libDaisy) also pushes the cmake/ dir
# onto CMAKE_MODULE_PATH so `include(DaisyProject)` resolves below.
add_subdirectory(lib/libDaisy)
add_subdirectory(lib/DaisySP)
# PARITY: Make builds DaisySP at -std=gnu++14; its CMake target otherwise picks up gnu++17.
set_target_properties(DaisySP PROPERTIES CXX_STANDARD 14)

# --- Engine selection (mirrors the ENGINE switch in the Makefile; default = granular) ---
if(NOT DEFINED ENGINE)
    set(ENGINE granular)
endif()
# Per-engine extras (defaults; the reso branch overrides them for its vendored Rings/stmlib DSP).
set(ENGINE_INCLUDES "")            # extra -I roots for the firmware target
set(ENGINE_DEFINES  "")            # extra compile definitions
set(ENGINE_OPTIONS  "")            # extra compile options (gigaverb suppresses gen~ unused-* warnings)
set(APP_OPT -O2)                   # app-TU opt level (overrides the inherited -O3); reso drops to -Os
if(ENGINE STREQUAL granular)
    set(ENGINE_DEFINE SPK_ENGINE_GRANULAR)
    file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/granular/*.cpp)
elseif(ENGINE STREQUAL passthrough)
    set(ENGINE_DEFINE SPK_ENGINE_PASSTHROUGH)
    set(ENGINE_SOURCES "")
elseif(ENGINE STREQUAL delay)
    set(ENGINE_DEFINE SPK_ENGINE_DELAY)
    set(ENGINE_SOURCES src/engine/delay/delay_engine.cpp)
elseif(ENGINE STREQUAL edrums)
    set(ENGINE_DEFINE SPK_ENGINE_EDRUMS)
    set(ENGINE_SOURCES src/engine/edrums/edrums_engine.cpp)
elseif(ENGINE STREQUAL reso)
    set(ENGINE_DEFINE SPK_ENGINE_RESO)
    # reso's DSP = Mutable Instruments Rings + its stmlib support library, vendored (trimmed to the used
    # closure) under src/engine/reso/thirdparty/ and compiled into the firmware with the engine wrapper.
    set(RESO_TP ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/reso/thirdparty)
    set(ENGINE_SOURCES
        src/engine/reso/reso_engine.cpp
        ${RESO_TP}/rings/dsp/part.cc
        ${RESO_TP}/rings/dsp/string.cc
        ${RESO_TP}/rings/dsp/resonator.cc
        ${RESO_TP}/rings/dsp/fm_voice.cc
        ${RESO_TP}/rings/resources.cc
        ${RESO_TP}/stmlib/dsp/units.cc
        ${RESO_TP}/stmlib/utils/random.cc
        ${RESO_TP}/stmlib/dsp/atan.cc
    )
    set(ENGINE_INCLUDES ${RESO_TP})
    # stmlib's filters use M_PI, which strict -std=c++17 does not expose from <cmath> on arm-none-eabi.
    set(ENGINE_DEFINES M_PI=3.14159265358979323846)
    # Rings (~30 KB of code+tables) overflows the 186 KB execution SRAM at -O2; build reso at -Os to fit.
    set(APP_OPT -Os)
elseif(ENGINE STREQUAL graincloud)
    set(ENGINE_DEFINE SPK_ENGINE_GRAINCLOUD)
    # graincloud's DSP is a de-STL'd port of GrainflowLib (header-only, no heap on the audio path),
    # vendored under src/engine/graincloud/thirdparty/grainflow/. Self-contained; one engine .cpp.
    set(GRAINCLOUD_TP ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/graincloud/thirdparty)
    set(ENGINE_SOURCES src/engine/graincloud/graincloud_engine.cpp)
    set(ENGINE_INCLUDES ${GRAINCLOUD_TP})
    # gfSyn pulls M_PI via <cmath>, which strict -std=c++17 does not expose on arm-none-eabi.
    set(ENGINE_DEFINES M_PI=3.14159265358979323846)
    # Fits SRAM_EXEC at -O2 (~87%), so no -Os override needed.
elseif(ENGINE STREQUAL tape)
    set(ENGINE_DEFINE SPK_ENGINE_TAPE)
    # Engine wrapper (tape_engine.cpp); its SD streaming service lives in src/hw/ (stream_deck.cpp,
    # fat_file.cpp, buffer.sdram.cpp -- picked up by the HW_SOURCES glob) with bodies guarded by
    # SPK_USE_STREAM, the feature gate every streaming engine opts into. Without that define those TUs
    # compile to empty objects and the link fails with undefined StreamDeck/FatFile symbols (the Makefile
    # sets it for tape too; this define was the parity gap that left the CMake tape build unlinkable).
    set(ENGINE_DEFINES SPK_USE_STREAM)
    set(ENGINE_SOURCES src/engine/tape/tape_engine.cpp)
elseif(ENGINE STREQUAL radio)
    set(ENGINE_DEFINE SPK_ENGINE_RADIO)
    # Dual virtual RadioMusic. Streams raw 16-bit-mono ".raw" stations from SD via the shared streaming
    # service in src/hw/ (stream_deck.cpp, fat_file.cpp, buffer.sdram.cpp -- HW_SOURCES glob), bodies
    # guarded by SPK_USE_STREAM (same gate as tape/shuttle); without it the link fails on undefined
    # StreamDeck/FatFile symbols.
    set(ENGINE_DEFINES SPK_USE_STREAM)
    set(ENGINE_SOURCES src/engine/radio/radio_engine.cpp)
elseif(ENGINE STREQUAL reverb)
    set(ENGINE_DEFINE SPK_ENGINE_REVERB)
    # Stereo reverb (Dattorro plate / Zita hall). Its DSP is the cyfaust-generated kernels
    # (faust_kernel_<name>.h, regenerated by `make faust-gen`) plus the hand-written arch shim -- both
    # header-only, so the engine wrapper is the only TU to compile.
    set(ENGINE_SOURCES src/engine/reverb/reverb_engine.cpp)
elseif(ENGINE STREQUAL shuttle)
    set(ENGINE_DEFINE SPK_ENGINE_SHUTTLE)
    # Buffer-based bipolar/reverse "varispeed shuttle" tape: 4 in-SDRAM mono buffers. LOAD (slot from SD
    # into RAM) opts into the shared streaming service via SPK_USE_STREAM -- the same gate the tape engine
    # sets, so stream_deck/fat_file/buffer.sdram compile their bodies and the platform pumps the StreamDeck.
    set(ENGINE_DEFINES SPK_USE_STREAM)
    set(ENGINE_SOURCES src/engine/shuttle/shuttle_engine.cpp)
elseif(ENGINE STREQUAL gigaverb)
    # gen~ engine: a Cycling '74 gen~ export bridged through gen-dsp's genlib-isolation layer plus the
    # shared src/engine/gen/ runtime (GenEngine + the arena-bound genlib). Mirrors the marker-managed
    # gigaverb block that scripts/gen_engine.py maintains in the Makefile. gen_exported.cpp is NOT a
    # source: it is #included via GEN_EXPORTED_CPP inside the bridge, so listing it would double-define.
    set(ENGINE_DEFINE SPK_ENGINE_GIGAVERB)
    set(GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/gigaverb)
    set(ENGINE_DEFINES
        GENLIB_NO_JSON
        DAISY_EXT_NAME=gigaverb
        GEN_EXPORTED_NAME=gen_exported
        GEN_EXPORTED_HEADER="gen_exported.h"
        GEN_EXPORTED_CPP="gen_exported.cpp"
    )
    # gen~ codegen trips a raft of unused-function/variable/parameter warnings; the Make build silences
    # them for this engine, so match that to keep the build warning-clean.
    set(ENGINE_OPTIONS -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter)
    set(ENGINE_INCLUDES ${GEN_DIR} ${GEN_DIR}/gen ${GEN_DIR}/gen/gen_dsp)
    set(ENGINE_SOURCES ${GEN_DIR}/_ext_daisy.cpp src/engine/gen/genlib_arena.cpp)
# >>> faust:chorus >>>
elseif(ENGINE STREQUAL chorus)
    set(ENGINE_DEFINE SPK_ENGINE_CHORUS)
    set(ENGINE_SOURCES)
# <<< faust:chorus <<<
# >>> faust:dfilter >>>
elseif(ENGINE STREQUAL dfilter)
    set(ENGINE_DEFINE SPK_ENGINE_DFILTER)
    set(ENGINE_SOURCES)
# <<< faust:dfilter <<<
# >>> faust:voice >>>
elseif(ENGINE STREQUAL voice)
    set(ENGINE_DEFINE SPK_ENGINE_VOICE)
    set(ENGINE_SOURCES)
# <<< faust:voice <<<
else()
    message(FATAL_ERROR "Unknown ENGINE '${ENGINE}' - use granular, passthrough, delay, edrums, reso, graincloud, tape, reverb, shuttle, radio, or gigaverb")
endif()

# Source list = the Makefile's CPP_SOURCES, with the wildcard dirs expanded the same way.
file(GLOB TRANSPORT_SOURCES CONFIGURE_DEPENDS src/transport/*.cpp)
file(GLOB DSP_SOURCES       CONFIGURE_DEPENDS src/dsp/*.cpp)
file(GLOB HW_SOURCES        CONFIGURE_DEPENDS src/hw/*.cpp)
file(GLOB UI_SOURCES        CONFIGURE_DEPENDS src/ui/*.cpp)
file(GLOB MEMORY_SOURCES    CONFIGURE_DEPENDS src/memory/*.cpp)

set(FIRMWARE_NAME spotykach)
set(FIRMWARE_SOURCES
    main.cpp
    src/app.cpp
    ${ENGINE_SOURCES}
    src/engine/color.cpp
    src/engine/led.ring.cpp
    ${TRANSPORT_SOURCES}
    ${DSP_SOURCES}
    ${HW_SOURCES}
    ${UI_SOURCES}
    ${MEMORY_SOURCES}
)
set(DAISYSP_LIB DaisySP)

# PARITY (boot path): use the project's custom SRAM linker script, not the stock
# STM32H750IB_sram.lds. DaisyProject reads CUSTOM_LINKER_SCRIPT at include() time.
set(CUSTOM_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/alt_sram.lds)

include(DaisyProject)

# PARITY (boot path, critical): the Make build defines BOOT_APP for the startup file, which
# makes Reset_Handler SKIP SystemInit() (the SRAM bootloader already ran it). DaisyProject does
# NOT set BOOT_APP when CUSTOM_LINKER_SCRIPT is used, so without this line the bootloaded app
# would re-run SystemInit() and likely fail to boot. BOOT_APP appears ONLY in startup_stm32h750xx.c,
# so defining it lib-wide changes nothing else.
target_compile_definitions(daisy PRIVATE BOOT_APP)

# FORK GAP: the bleeptools fork added hid/midi_util.cpp (daisy::MidiTxMessage) to libDaisy's *Make*
# module list (core Makefile) but never to its CMakeLists.txt add_library(daisy ...). The Make build
# compiles it; the CMake build does not, so the app fails to link with undefined MidiTxMessage symbols.
# Patch it onto the target here rather than editing the vendored submodule. (per/pwm.cpp is also
# Make-only but the app uses the HAL TIM API directly, not daisy::Pwm, so it is unused and omitted.)
target_sources(daisy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/libDaisy/src/hid/midi_util.cpp)

# PARITY: app-level defines and the C++17 standard (DaisyProject sets the firmware target to C++14).
# USE_HAL_DRIVER / USE_FULL_LL_DRIVER are global C_DEFS in the Make build but only PRIVATE defines
# on the daisy CMake lib, so they do NOT reach app TUs here. They must: without USE_HAL_DRIVER the
# force-included stm32h7xx.h skips the HAL chain that drops <stddef.h> (::size_t) into global scope,
# and headers like detector.h that use bare `size_t` fail to compile.
target_compile_definitions(${FIRMWARE_NAME} PRIVATE
    ${ENGINE_DEFINE}
    ${ENGINE_DEFINES}
    INFS_LOG_TARGET=daisy::LOGGER_EXTERNAL
    USE_HAL_DRIVER
    USE_FULL_LL_DRIVER
)

# PARITY: the old Makefile's config toggles (DEBUG=1 -> -DINFS_LOG=1, LOFI_INT16=1 -> -DLOFI_INT16=1).
# Passed straight through by the thin Make frontend (Makefile.cmake) as -DDEBUG=1 / -DLOFI_INT16=1.
if(DEBUG)
    target_compile_definitions(${FIRMWARE_NAME} PRIVATE INFS_LOG=1)
endif()
if(LOFI_INT16)
    target_compile_definitions(${FIRMWARE_NAME} PRIVATE LOFI_INT16=1)
endif()
# PARITY: Make uses strict -std=c++17 for the app (not gnu++17), so disable GNU extensions. The app
# does use the `asm` keyword, which strict c++17 rejects without -fasm -- Make supplies -fasm, added below.
set_target_properties(${FIRMWARE_NAME} PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)

# PARITY (app codegen flags): replicate the Make app TU flag set exactly. The trailing ${APP_OPT}
# (-O2 default, -Os for reso -- whose Rings DSP overflows SRAM at -O2) overrides the inherited directory
# -O3 for app TUs only (last -O on the line wins). DaisyProject already supplies
# -fno-exceptions/-fno-rtti/-fdata-sections/-ffunction-sections.
#   NOTE on -fshort-enums: in the Make build this is an APP-ONLY flag (libDaisy and DaisySP do NOT use
#   it), so app and library disagree on enum width. That asymmetry ships today and is hardware-proven,
#   so reproducing it here is the parity-correct choice -- it is no riskier than the current build.
target_compile_options(${FIRMWARE_NAME} PRIVATE
    -fasm
    -finline
    -finline-functions-called-once
    -fno-move-loop-invariants
    -fno-unwind-tables
    -fshort-enums
    -Wno-register
    ${APP_OPT}
    ${ENGINE_OPTIONS}
)

# PARITY: the Make build forces `-include stm32h7xx.h` globally and adds -Isrc/ -Ilib/.
target_include_directories(${FIRMWARE_NAME} PRIVATE src lib ${ENGINE_INCLUDES})
target_compile_options(${FIRMWARE_NAME} PRIVATE -include stm32h7xx.h)

# DFU plumbing (TODO flagged this as missing from the CMake path). Recreate the Makefile's
# program-dfu / program-boot as custom targets. BOOT_SRAM stores the app in QSPI at 0x90040000;
# the SRAM bootloader copies it to SRAM at boot. PID df11 = the Daisy bootloader's DFU id.
set(SPK_QSPI_ADDRESS 0x90040000)
set(SPK_INTERNAL_ADDRESS 0x08000000)
set(SPK_DAISY_PID df11)
add_custom_command(TARGET ${FIRMWARE_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${FIRMWARE_NAME}> $<TARGET_FILE_DIR:${FIRMWARE_NAME}>/${FIRMWARE_NAME}.bin
    COMMENT "Generating ${FIRMWARE_NAME}.bin (DFU payload)")
add_custom_target(program-dfu
    COMMAND dfu-util -a 0 -s ${SPK_QSPI_ADDRESS}:leave -D $<TARGET_FILE_DIR:${FIRMWARE_NAME}>/${FIRMWARE_NAME}.bin -d ,0483:${SPK_DAISY_PID}
    DEPENDS ${FIRMWARE_NAME} USES_TERMINAL)
add_custom_target(program-boot
    COMMAND dfu-util -a 0 -s ${SPK_INTERNAL_ADDRESS}:leave -D ${CMAKE_CURRENT_SOURCE_DIR}/bootloader-spotykach-v2.bin -d ,0483:df11
    USES_TERMINAL)
