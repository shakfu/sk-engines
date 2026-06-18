// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// Orchestra selection for the Csound engine (roadmap #1 + #5 in docs/dev/csound-impl.md): load `.csd`
// patches from the SD card, with a built-in fallback, and pick among a small bank via Alt+PITCH.
//
// Card layout: numbered slots in a `csound/` folder at the card root - `csound/0.csd` ..
// `csound/7.csd` (full CSD documents, like the built-in kOrchestra). The engine presents the built-in
// plus every present slot as a selectable list; Alt+PITCH (CapAux) scrolls it and a release commits
// a live recompile.
//
// PATH FORM: FatFs paths here are RELATIVE (no leading slash), matching the radio/tape/shuttle engines
// - libDaisy mounts the SD at a volume-prefixed path (FatFSInterface::GetSDPath), so a bare leading
// "/" resolves to the wrong volume and f_open/f_stat miss. (This cost a hardware debug session.)
//
// This is the host-testable half (no libcsound): path building, the existence scan, the Aux->index
// quantizer, and the read+validate. The engine (csound_engine.cpp, which links libcsound) calls
// these, then hands the chosen text to csoundCompileCSD. host/test_csound_patch.cpp covers it.

#include "engine/istreamdeck.h"

#include <cstdio>
#include <cstring>

namespace spotykach {

// Number of numbered patch slots probed on the card (/csound/0.csd .. /csound/<N-1>.csd).
inline constexpr int kMaxPatchSlots = 8;

// Build the relative path "csound/<slot>.csd" into `out` (needs ~16 bytes). Returns `out`. No leading
// slash - see the PATH FORM note above.
inline const char* patch_path(int slot, char* out, int outsz) {
    std::snprintf(out, outsz, "csound/%d.csd", slot);
    return out;
}

// Cheap guard: does `text` look like a Csound CSD document (has the root tag)? Lets a stray or
// corrupt file fall back to the working built-in instead of being handed to the compiler.
inline bool looks_like_csd(const char* text) {
    return text && std::strstr(text, "<CsoundSynthesizer") != nullptr;
}

// Probe which numbered slots exist on the card (via IStreamDeck::exists, a main-loop f_stat). Fills
// present[0..max_slots-1] and returns the count present. Tolerates a null stream (nothing present).
inline int scan_patches(IStreamDeck* stream, bool* present, int max_slots) {
    int n = 0;
    for (int s = 0; s < max_slots; s++) {
        char path[24];
        patch_path(s, path, sizeof(path));
        present[s] = stream && stream->exists(path);
        if (present[s]) n++;
    }
    return n;
}

// Quantize a 0..1 Aux (Alt+PITCH) value to an index in [0, count). count must be >= 1.
inline int aux_to_index(float v, int count) {
    if (count <= 1) return 0;
    int i = static_cast<int>(v * static_cast<float>(count));
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;
    return i;
}

// Read an orchestra from `path` into `buf` (read_text NUL-terminates) and validate it as a CSD. On
// success returns `buf` and sets *from_sd = true; on a null/empty/missing/non-CSD read returns
// `fallback` with *from_sd = false. Tolerates a null stream / null-or-tiny buffer / null path.
inline const char* read_orchestra(IStreamDeck* stream, const char* path, char* buf, int max,
                                  const char* fallback, bool* from_sd) {
    if (from_sd) *from_sd = false;
    if (!stream || !buf || max <= 1 || !path) return fallback;

    const int n = stream->read_text(path, buf, max);
    if (n <= 0) return fallback;                          // missing / empty

    // Start the document exactly at the root tag, skipping any leading bytes a card file may carry
    // that a compiled-in string never does - a UTF-8 BOM (EF BB BF) or stray whitespace/newlines.
    // csoundCompileCSD wants the text to BEGIN with <CsoundSynthesizer>; a BOM in front makes it fail
    // to find the tag (while strstr still would), which presents as "valid CSD but compile failed".
    char* tag = std::strstr(buf, "<CsoundSynthesizer");
    if (!tag) return fallback;                            // present but not a CSD -> keep the built-in

    // Normalize CRLF -> LF in place. A card file authored on Windows / many editors has \r\n line
    // endings; the compiled-in orchestra (a C string literal) has \n. Csound's line-oriented CSD
    // block parser chokes on the stray \r, which presents as "valid CSD text but compile failed".
    char* w = tag;
    for (const char* r = tag; *r; ++r) if (*r != '\r') *w++ = *r;
    *w = '\0';

    if (from_sd) *from_sd = true;
    return tag;
}

} // namespace spotykach
