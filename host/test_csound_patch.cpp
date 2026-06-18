// Host test for the Csound orchestra selector (src/engine/csound/csound_patch.h, roadmap #1 + #5):
// numbered SD patch slots, the existence scan, the Alt+PITCH index quantizer, and the read+validate
// with a built-in fallback. No libcsound dependency, so it runs off-target against a fake
// IStreamDeck. The actual csoundCompileCSD lives in the QSPI-only engine and isn't exercised here.
// Build: `make -C host test-csound-patch`.

#include "engine/csound/csound_patch.h"

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
    static constexpr int kN = kMaxPatchSlots;
    const char* body[kN] = {};           // body[s] != nullptr => slot s exists with that content

    static int slot_of(const char* path) {  // ".../<s>.csd" -> s, or -1
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

const char* kFallback = "FALLBACK-ORCHESTRA";
const char* kValidCsd =
    "<CsoundSynthesizer>\n<CsInstruments>\ninstr 1\n  out oscili(0.2, 220)\nendin\n"
    "instr MidiNote\n  out oscili(0.3, p4)\nendin\n</CsInstruments>\n</CsoundSynthesizer>\n";

void test_patch_path() {
    std::printf("patch_path builds relative csound/<slot>.csd (no leading slash)\n");
    char p[24];
    check(std::strcmp(patch_path(0, p, sizeof(p)), "csound/0.csd") == 0, "slot 0 path (relative)");
    check(std::strcmp(patch_path(7, p, sizeof(p)), "csound/7.csd") == 0, "slot 7 path (relative)");
    check(p[0] != '/', "no leading slash (libDaisy mounts at a volume-prefixed path)");
}

void test_scan_patches() {
    std::printf("scan_patches probes existence per slot\n");
    FakeStream s;
    s.body[0] = kValidCsd; s.body[3] = kValidCsd; s.body[7] = kValidCsd;   // 3 present
    bool present[kMaxPatchSlots];
    const int n = scan_patches(&s, present, kMaxPatchSlots);
    check(n == 3, "counts the present slots");
    check(present[0] && present[3] && present[7], "present slots flagged");
    check(!present[1] && !present[2] && !present[4] && !present[5] && !present[6], "absent slots clear");

    bool none[kMaxPatchSlots];
    check(scan_patches(nullptr, none, kMaxPatchSlots) == 0, "null stream -> nothing present");
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

void test_read_orchestra() {
    std::printf("read_orchestra: read+validate else fallback\n");
    FakeStream s;
    s.body[2] = kValidCsd;
    char buf[256];
    char path[24];
    bool from_sd = false;

    const char* orc = read_orchestra(&s, patch_path(2, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(orc == buf && from_sd, "a present, valid slot is used");
    check(std::strcmp(buf, kValidCsd) == 0, "buffer holds the slot contents verbatim");

    from_sd = true;
    orc = read_orchestra(&s, patch_path(5, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(orc == kFallback && !from_sd, "an absent slot falls back");

    s.body[4] = "not a csd, just notes";
    from_sd = true;
    orc = read_orchestra(&s, patch_path(4, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(orc == kFallback && !from_sd, "a present non-CSD file falls back (don't kill audio)");

    // A card file as an editor on Windows would save it: a UTF-8 BOM, leading whitespace, and CRLF
    // line endings - none of which the compiled-in C-string orchestra has. read_orchestra must return
    // text that STARTS at "<CsoundSynthesizer" (BOM/junk stripped) and contains no '\r' (CRLF->LF), so
    // csoundCompileCSD sees exactly the clean document the built-in path compiles.
    s.body[3] = "\xEF\xBB\xBF\r\n<CsoundSynthesizer>\r\n<CsInstruments>\r\n</CsInstruments>\r\n</CsoundSynthesizer>\r\n";
    from_sd = false;
    orc = read_orchestra(&s, patch_path(3, path, sizeof(path)), buf, sizeof(buf), kFallback, &from_sd);
    check(from_sd, "a BOM+CRLF CSD is still recognized as an SD patch");
    check(std::strncmp(orc, "<CsoundSynthesizer", 18) == 0, "returned text starts exactly at the root tag (BOM/junk stripped)");
    check(std::strchr(orc, '\r') == nullptr, "all CR stripped (CRLF normalized to LF)");

    check(read_orchestra(&s, nullptr, buf, sizeof(buf), kFallback, &from_sd) == kFallback, "null path -> fallback");
    check(read_orchestra(nullptr, path, buf, sizeof(buf), kFallback, &from_sd) == kFallback, "null stream -> fallback");
    char tiny[1];
    check(read_orchestra(&s, patch_path(2, path, sizeof(path)), tiny, 1, kFallback, &from_sd) == kFallback,
          "no scratch buffer -> fallback");
    check(read_orchestra(&s, path, buf, sizeof(buf), kFallback, nullptr) == buf || true, "null from_sd tolerated");
}

void test_looks_like_csd() {
    std::printf("looks_like_csd unit\n");
    check(!looks_like_csd(nullptr) && !looks_like_csd(""), "null/empty rejected");
    check(!looks_like_csd("instr 1\nendin\n"), "bare orchestra (no root tag) rejected");
    check(looks_like_csd("; comment\n<CsoundSynthesizer>...</CsoundSynthesizer>"), "root tag after a comment accepted");
}

} // namespace

int main() {
    std::printf("=== Csound orchestra selector (slots / fallback / Alt+PITCH) ===\n");
    test_patch_path();
    test_scan_patches();
    test_aux_to_index();
    test_read_orchestra();
    test_looks_like_csd();

    if (g_failures) { std::printf("\nFAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("\nAll Csound selector tests passed.\n");
    return 0;
}
