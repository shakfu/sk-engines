// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// Orchestra selection for the Csound engine (roadmap #1 in docs/dev/csound.md): load a `.csd` patch
// from the SD card instead of the compiled-in string, with the built-in as a safety fallback.
//
// This is the host-testable half of the feature - it has no libcsound dependency, so
// host/test_csound_patch.cpp exercises the path/fallback/validation decisions against a fake
// IStreamDeck. The engine (csound_engine.cpp, which DOES link libcsound) calls select_orchestra(),
// then hands the returned text to csoundCompileCSD.
//
// Card layout: a single patch at `/csound/patch.csd` (a full CSD document, like the built-in
// kOrchestra). Multi-patch selection (a set of files + an Alt+PITCH selector) is roadmap #5; this
// keeps to one well-known path.

#include "engine/istreamdeck.h"

#include <cstring>

namespace spotykach {

// The well-known patch path on the card. A full CSD document; see kOrchestra for the control-channel
// vocabulary the engine drives (speedA/mixA/sizeA/...).
inline constexpr const char* kCsoundPatchPath = "/csound/patch.csd";

// Cheap guard: does `text` look like a Csound CSD document (has the root tag)? Lets a stray or
// corrupt text file on the card fall back to the working built-in orchestra instead of being handed
// to the compiler (where, at best, it fails to compile and the engine goes silent).
inline bool looks_like_csd(const char* text) {
    return text && std::strstr(text, "<CsoundSynthesizer") != nullptr;
}

// Choose the orchestra text to compile. Reads kCsoundPatchPath from the card via `stream` into `buf`
// (read_text NUL-terminates); if that yields a non-empty, CSD-looking document, returns `buf` and
// sets *from_sd = true. Otherwise returns `fallback` (the compiled-in orchestra) with
// *from_sd = false. Tolerates a null `stream` (no SD service) or a null/too-small `buf` - both fall
// back. `from_sd` may be null.
inline const char* select_orchestra(IStreamDeck* stream, char* buf, int max,
                                    const char* fallback, bool* from_sd) {
    if (from_sd) *from_sd = false;
    if (!stream || !buf || max <= 1) return fallback;

    const int n = stream->read_text(kCsoundPatchPath, buf, max);
    if (n <= 0)              return fallback;   // missing / empty card file
    if (!looks_like_csd(buf)) return fallback;  // present but not a CSD -> keep the built-in

    if (from_sd) *from_sd = true;
    return buf;
}

} // namespace spotykach
