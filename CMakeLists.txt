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

project(spotykach VERSION 1.0.0 LANGUAGES C CXX ASM)

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
else()
    message(FATAL_ERROR "Unknown ENGINE '${ENGINE}' - use granular, passthrough, delay, or edrums")
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

# PARITY (app codegen flags): replicate the Make app TU flag set exactly. The trailing -O2 overrides the
# inherited directory -O3 for app TUs only (last -O on the line wins). DaisyProject already supplies
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
    -O2
)

# PARITY: the Make build forces `-include stm32h7xx.h` globally and adds -Isrc/ -Ilib/.
target_include_directories(${FIRMWARE_NAME} PRIVATE src lib)
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
