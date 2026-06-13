#include "version.h"

namespace infrasonic
{
// The build banner makes a downloaded binary self-identifying without a serial console or display:
// `arm-none-eabi-strings firmware.bin | grep '^spotykach '` reveals the exact version and engine.
// `used` keeps it in the object file; the linker still drops it under --gc-sections unless a
// reachable section references it, so app.cpp reads firmware_banner() at boot regardless of log level.
__attribute__((used)) static const char kBuildBanner[] =
    "spotykach " SPK_VERSION_STR " engine=" SPK_ENGINE_STR;

const char* firmware_version() { return SPK_VERSION_STR; }
const char* engine_name() { return SPK_ENGINE_STR; }
const char* firmware_banner() { return kBuildBanner; }
} // namespace infrasonic
