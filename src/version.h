#pragma once

// Firmware identity, injected at build time by the Makefile:
//   SPK_VERSION_STR -> `git describe --tags --always` of the source tree
//   SPK_ENGINE_STR  -> the ENGINE= variant this binary was built for
// Both carry safe fallbacks so host-side builds (host/, which don't set them) still compile.
#ifndef SPK_VERSION_STR
#define SPK_VERSION_STR "dev"
#endif
#ifndef SPK_ENGINE_STR
#define SPK_ENGINE_STR "unknown"
#endif

namespace infrasonic
{
// `git describe` of the tree this firmware was built from (e.g. "v1.2.0" or "v1.2.0-5-gabc1234").
const char* firmware_version();

// The engine variant compiled into this binary (e.g. "reverb", "delay").
const char* engine_name();

// The full self-identifying banner: "spotykach <version> engine=<engine>". Reference this once
// from always-reachable boot code (see app.cpp) so -Wl,--gc-sections retains it; that is what
// makes `arm-none-eabi-strings firmware.bin | grep '^spotykach '` work on a release build.
const char* firmware_banner();
} // namespace infrasonic
