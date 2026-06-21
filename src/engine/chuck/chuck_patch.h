// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// Program selection for the ChucK engine (M3 in docs/dev/chuck-impl.md): load `.ck` patches from the
// SD card, with a built-in fallback, and pick among a small bank via Alt+PITCH. Mirrors the Csound
// orchestra selector (csound_patch.h) one-to-one, with two ChucK-specific differences noted below.
//
// Card layout: numbered slots in a `chuck/` folder at the card root - `chuck/0.ck` .. `chuck/7.ck`
// (complete ChucK programs, like the built-in kProgram). The engine presents the built-in plus every
// present slot as a selectable list; Alt+PITCH (CapAux) scrolls it and a release commits a live
// recompile (removeAllShreds + compileCode behind the ReloadGate).
//
// PATH FORM: FatFs paths here are RELATIVE (no leading slash), matching the csound/radio/tape engines
// - libDaisy mounts the SD at a volume-prefixed path (FatFSInterface::GetSDPath), so a bare leading
// "/" resolves to the wrong volume and f_open/f_stat miss. (This cost a hardware debug session.)
//
// DIFFERENCE 1 - no structural pre-validation. A `.csd` must begin with <CsoundSynthesizer>, which
// lets the Csound selector cheaply reject a non-CSD file before handing it to the compiler. A `.ck`
// file has NO required header - any text is potentially valid ChucK source (e.g. `SinOsc s => dac;`).
// So there is nothing to sniff for: read_program only normalizes the bytes and rejects an empty /
// whitespace-only file. The real validation is compileCode() returning false in the engine, which
// falls back to the built-in (chuck_engine.cpp do_reload) - the same safety net Csound relies on for
// a syntactically-bad-but-tagged CSD.
//
// DIFFERENCE 2 - what gets stripped. Like the CSD path we strip a leading UTF-8 BOM (the ChucK lexer
// chokes on it; a compiled-in C-string program never carries one) and normalize CRLF->LF (a card file
// authored on Windows has \r\n; the built-in has \n). Unlike the CSD path we do NOT skip to a root
// tag (there is none) - leading whitespace is harmless to the lexer, so the returned text begins at
// the first byte after any BOM.
//
// This is the host-testable half (no libchuck): path building, the existence scan, the Aux->index
// quantizer, and the read+normalize. The engine (chuck_engine.cpp, which links libchuck) calls these,
// then hands the chosen text to ChucK::compileCode. host/test_chuck_patch.cpp covers it.

#include "engine/istreamdeck.h"

#include <cstdio>
#include <cstring>

namespace spotykach {

// Number of numbered patch slots probed on the card (/chuck/0.ck .. /chuck/<N-1>.ck).
inline constexpr int kMaxChuckSlots = 8;

// Build the relative path "chuck/<slot>.ck" into `out` (needs ~16 bytes). Returns `out`. No leading
// slash - see the PATH FORM note above.
inline const char* chuck_path(int slot, char* out, int outsz) {
    std::snprintf(out, outsz, "chuck/%d.ck", slot);
    return out;
}

// Cheap guard: does `text` carry any compilable content (any non-whitespace byte)? This is the most a
// `.ck` file can be checked for without compiling it (see DIFFERENCE 1) - an empty or whitespace-only
// file is rejected so it falls back to the working built-in instead of a no-op compile.
inline bool looks_like_chuck(const char* text) {
    if (!text) return false;
    for (const char* p = text; *p; ++p)
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return true;
    return false;
}

// Probe which numbered slots exist on the card (via IStreamDeck::exists, a main-loop f_stat). Fills
// present[0..max_slots-1] and returns the count present. Tolerates a null stream (nothing present).
inline int scan_chuck_patches(IStreamDeck* stream, bool* present, int max_slots) {
    int n = 0;
    for (int s = 0; s < max_slots; s++) {
        char path[24];
        chuck_path(s, path, sizeof(path));
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

// Read a ChucK program from `path` into `buf` (read_text NUL-terminates), normalize it, and return it
// if it carries compilable content. On success returns the cleaned text (inside `buf`, past any BOM)
// and sets *from_sd = true; on a null/empty/missing/whitespace-only read returns `fallback` with
// *from_sd = false. Tolerates a null stream / null-or-tiny buffer / null path.
inline const char* read_program(IStreamDeck* stream, const char* path, char* buf, int max,
                                const char* fallback, bool* from_sd) {
    if (from_sd) *from_sd = false;
    if (!stream || !buf || max <= 1 || !path) return fallback;

    const int n = stream->read_text(path, buf, max);
    if (n <= 0) return fallback;                          // missing / empty

    // Skip a leading UTF-8 BOM (EF BB BF). A card file from some editors carries one; the ChucK lexer
    // would choke on it (the compiled-in C-string program never has one). Unlike the CSD selector
    // there is no root tag to seek to, so we keep everything after the BOM verbatim (DIFFERENCE 2).
    char* start = buf;
    if (static_cast<unsigned char>(start[0]) == 0xEF &&
        static_cast<unsigned char>(start[1]) == 0xBB &&
        static_cast<unsigned char>(start[2]) == 0xBF)
        start += 3;

    // Normalize CRLF -> LF in place. A card file authored on Windows / many editors has \r\n line
    // endings; the compiled-in program (a C string literal) has \n. Normalize so an SD patch and the
    // built-in feed compileCode byte-identical line endings.
    char* w = start;
    for (const char* r = start; *r; ++r) if (*r != '\r') *w++ = *r;
    *w = '\0';

    if (!looks_like_chuck(start)) return fallback;        // whitespace-only -> keep the built-in
    if (from_sd) *from_sd = true;
    return start;
}

} // namespace spotykach
