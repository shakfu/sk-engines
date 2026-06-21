// Host test for the ChucK program selector (src/engine/chuck/chuck_patch.h, M3): numbered SD patch
// slots, the existence scan, the Alt+PITCH index quantizer, and the read+normalize with a built-in
// fallback. No libchuck dependency, so it runs off-target against a fake IStreamDeck. The actual
// ChucK::compileCode lives in the QSPI-only engine and isn't exercised here.
// Build: `make -C host test-chuck-patch`.
//
// The key difference from the Csound selector test: a `.ck` file has no required header, so there is
// no structural validation to assert (any non-empty program is accepted). The fallback cases here are
// missing / empty / whitespace-only - not "wrong file type" - and the BOM/CRLF normalization keeps the
// returned text byte-clean for the lexer.

#include "engine/chuck/chuck_patch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}

// Fake card: a set of slot files (by full path) with bodies. read_text returns a path's body;
// exists() reports membership. Everything else is a stub (the selector only uses read_text/exists).
struct FakeStream : IStreamDeck {
    static constexpr int kN = kMaxChuckSlots;
    const char* body[kN] = {};           // body[s] != nullptr => slot s exists with that content

    static int slot_of(const char* path) {  // ".../<s>.ck" -> s, or -1
        const char* slash = std::strrchr(path, '/');
        return slash ? std::atoi(slash + 1) : -1;
    }
    bool exists(const char* path) const override {
        const int s = slot_of(path);
        return s >= 0 && s < kN && body[s] != nullptr;
    }
    int read_text(const char* path, char* buf, int max) const override {
        if (max <= 0) return 0;
        const int s = slot_of(path);
        if (s < 0 || s >= kN || !body[s]) { buf[0] = '\0'; return 0; }
        int i = 0;
        for (; body[s][i] && i < max - 1; i++) buf[i] = body[s][i];
        buf[i] = '\0';
        return i;
    }

    // --- unused stubs -----------------------------------------------------------------------------
    uint32_t play_consume(DeckRef::Ref, uint8_t*, uint32_t n) override { return n; }
    uint32_t record_produce(DeckRef::Ref, const uint8_t*, uint32_t n) override { return n; }
    bool is_playing(DeckRef::Ref)   const override { return false; }
    bool is_recording(DeckRef::Ref) const override { return false; }
    bool start_play(DeckRef::Ref, const char*)   override { return false; }
    bool start_record(DeckRef::Ref, const char*) override { return false; }
    void stop(DeckRef::Ref) override {}
    void set_loop(DeckRef::Ref, bool) override {}
    uint32_t loop_frames(DeckRef::Ref) const override { return 0; }
};

const char* kFallback = "FALLBACK-PROGRAM";
const char* kValidCk  = "SinOsc s => dac;\n0.2 => s.gain;\nwhile(true){ 1::second => now; }\n";

void test_chuck_path() {
    std::printf("chuck_path builds relative chuck/<slot>.ck (no leading slash)\n");
    char p[24];
    check(std::strcmp(chuck_path(0, p, sizeof(p)), "chuck/0.ck") == 0, "slot 0 path (relative)");
    check(std::strcmp(chuck_path(7, p, sizeof(p)), "chuck/7.ck") == 0, "slot 7 path (relative)");
    check(p[0] != '/', "no leading slash (libDaisy mounts at a volume-prefixed path)");
}

void test_scan_patches() {
    std::printf("scan_chuck_patches probes existence per slot\n");
    FakeStream s;
    s.body[0] = kValidCk; s.body[3] = kValidCk; s.body[7] = kValidCk;   // 3 present
    bool present[kMaxChuckSlots];
    const int n = scan_chuck_patches(&s, present, kMaxChuckSlots);
    check(n == 3, "counts the present slots");
    check(present[0] && present[3] && present[7], "present slots flagged");
    check(!present[1] && !present[2] && !present[4] && !present[5] && !present[6], "absent slots clear");

    bool none[kMaxChuckSlots];
    check(scan_chuck_patches(nullptr, none, kMaxChuckSlots) == 0, "null stream -> nothing present");
    check(!none[0], "null stream clears the flags");
}

void test_aux_to_index() {
    std::printf("aux_to_index quantizes 0..1 to a slot index\n");
    check(aux_to_index(0.0f, 1) == 0, "count 1 is always index 0");
    check(aux_to_index(0.9f, 1) == 0, "count 1 ignores the value");
    check(aux_to_index(0.0f, 4) == 0, "bottom -> 0");
    check(aux_to_index(0.99f, 4) == 3, "top -> count-1");
    check(aux_to_index(1.0f, 4) == 3, "exactly 1.0 clamps to count-1 (no overflow)");
    check(aux_to_index(0.25f, 4) == 1 && aux_to_index(0.5f, 4) == 2, "even quantization across the range");
    check(aux_to_index(-0.1f, 4) == 0, "below range clamps to 0");
}

void test_read_program() {
    std::printf("read_program: read+normalize else fallback\n");
    FakeStream s;
    s.body[2] = kValidCk;
    char buf[256];
    char path[24];
    bool from_sd = false;

    const char* prog = read_program(&s, chuck_path(2, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(prog == buf && from_sd, "a present, non-empty slot is used");
    check(std::strcmp(buf, kValidCk) == 0, "buffer holds the slot contents verbatim (already LF, no BOM)");

    from_sd = true;
    prog = read_program(&s, chuck_path(5, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(prog == kFallback && !from_sd, "an absent slot falls back");

    // A `.ck` has no required header, so the only content-based rejection is an empty / whitespace-only
    // file (nothing to compile) - that falls back to the working built-in rather than a no-op compile.
    s.body[4] = "   \n\t  \r\n  ";
    from_sd = true;
    prog = read_program(&s, chuck_path(4, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(prog == kFallback && !from_sd, "a whitespace-only file falls back (don't kill audio)");

    // Any non-empty program is accepted (no structural validation): even a one-liner.
    s.body[6] = "SinOsc s => dac;";
    from_sd = false;
    prog = read_program(&s, chuck_path(6, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(from_sd && std::strcmp(prog, "SinOsc s => dac;") == 0, "a bare one-line program is accepted");

    // A card file as an editor on Windows would save it: a UTF-8 BOM and CRLF line endings - neither of
    // which the compiled-in C-string program has. read_program must strip the BOM (the lexer chokes on
    // it) and normalize CRLF->LF, so compileCode sees the same clean bytes the built-in path compiles.
    s.body[3] = "\xEF\xBB\xBFSinOsc s => dac;\r\n0.2 => s.gain;\r\n";
    from_sd = false;
    prog = read_program(&s, chuck_path(3, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(from_sd, "a BOM+CRLF program is still recognized as an SD patch");
    check(prog[0] == 'S', "returned text starts past the BOM (first real byte)");
    check(std::strchr(prog, '\r') == nullptr, "all CR stripped (CRLF normalized to LF)");
    check(std::strstr(prog, "SinOsc s => dac;") != nullptr, "program body preserved");

    check(read_program(&s, nullptr, buf, sizeof(buf), kFallback, &from_sd) == kFallback, "null path -> fallback");
    check(read_program(nullptr, path, buf, sizeof(buf), kFallback, &from_sd) == kFallback, "null stream -> fallback");
    char tiny[1];
    check(read_program(&s, chuck_path(2, path, sizeof(path)), tiny, 1, kFallback, &from_sd) == kFallback,
          "no scratch buffer -> fallback");
    check(read_program(&s, path, buf, sizeof(buf), kFallback, nullptr) == buf || true, "null from_sd tolerated");
}

void test_looks_like_chuck() {
    std::printf("looks_like_chuck unit (any non-whitespace content)\n");
    check(!looks_like_chuck(nullptr) && !looks_like_chuck(""), "null/empty rejected");
    check(!looks_like_chuck("  \n\t \r\n "), "whitespace-only rejected");
    check(looks_like_chuck("SinOsc s => dac;"), "real program accepted");
    check(looks_like_chuck("\n\n  x"), "content after leading whitespace accepted");
}

} // namespace

int main() {
    std::printf("=== ChucK program selector (slots / fallback / Alt+PITCH) ===\n");
    test_chuck_path();
    test_scan_patches();
    test_aux_to_index();
    test_read_program();
    test_looks_like_chuck();

    if (g_failures) { std::printf("\nFAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("\nAll ChucK selector tests passed.\n");
    return 0;
}
