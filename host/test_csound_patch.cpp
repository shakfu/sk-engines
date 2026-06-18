// Host test for the Csound orchestra selector (select_orchestra, src/engine/csound/csound_patch.h).
// This is the host-testable half of roadmap #1 (SD-loaded .csd patches): the decision of whether to
// compile an SD card patch or fall back to the built-in orchestra. It has no libcsound dependency, so
// it runs off-target against a fake IStreamDeck. The actual csoundCompileCSD call lives in the engine
// (QSPI-only) and is not exercised here. Build: `make -C host test-csound-patch`.

#include "engine/csound/csound_patch.h"

#include <cstdio>
#include <cstring>

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}

// Minimal IStreamDeck whose read_text returns a configurable file body for one configured path,
// and records the path it was asked for. Everything else is a stub (the selector only calls
// read_text). file_path == nullptr models "no such file" (read_text returns 0).
struct FakeStream : IStreamDeck {
    const char* file_path = nullptr;     // the one path that "exists" on the card
    const char* file_body = nullptr;     // its contents
    mutable char last_requested[64] = {};
    mutable int  read_text_calls = 0;

    int read_text(const char* path, char* buf, int max) const override {
        read_text_calls++;
        std::strncpy(last_requested, path ? path : "", sizeof(last_requested) - 1);
        if (max <= 0) return 0;
        if (!file_path || !path || std::strcmp(path, file_path) != 0 || !file_body) {
            buf[0] = '\0';
            return 0;                    // missing file -> empty, matching the real StreamDeck
        }
        int i = 0;
        for (; file_body[i] && i < max - 1; i++) buf[i] = file_body[i];
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
    bool exists(const char*) const override { return false; }
};

const char* kFallback = "FALLBACK-ORCHESTRA";
const char* kValidCsd =
    "<CsoundSynthesizer>\n<CsInstruments>\ninstr 1\n  out oscili(0.2, 220)\nendin\n"
    "schedule(1,0,10)\n</CsInstruments>\n<CsScore>\n</CsScore>\n</CsoundSynthesizer>\n";

void test_null_stream_uses_fallback() {
    std::printf("null stream -> fallback\n");
    char buf[256];
    bool from_sd = true;                 // start true to prove it's cleared
    const char* orc = select_orchestra(nullptr, buf, sizeof(buf), kFallback, &from_sd);
    check(orc == kFallback, "null stream returns the built-in fallback");
    check(from_sd == false, "from_sd cleared for the fallback path");
}

void test_missing_file_uses_fallback() {
    std::printf("file missing -> fallback\n");
    FakeStream s;                        // no file configured
    char buf[256];
    bool from_sd = true;
    const char* orc = select_orchestra(&s, buf, sizeof(buf), kFallback, &from_sd);
    check(orc == kFallback, "missing card file returns the fallback");
    check(from_sd == false, "from_sd false when nothing was read");
    check(s.read_text_calls == 1, "the selector did attempt one read");
    check(std::strcmp(s.last_requested, kCsoundPatchPath) == 0, "it read the well-known patch path");
}

void test_valid_patch_is_used() {
    std::printf("valid CSD on card -> used\n");
    FakeStream s;
    s.file_path = kCsoundPatchPath;
    s.file_body = kValidCsd;
    char buf[256];
    bool from_sd = false;
    const char* orc = select_orchestra(&s, buf, sizeof(buf), kFallback, &from_sd);
    check(orc == buf, "returns the scratch buffer (the SD text), not the fallback");
    check(from_sd == true, "from_sd true for an SD patch");
    check(std::strcmp(buf, kValidCsd) == 0, "the buffer holds the file contents verbatim, NUL-terminated");
}

void test_non_csd_text_falls_back() {
    std::printf("present-but-not-a-CSD -> fallback (don't kill audio on a stray file)\n");
    FakeStream s;
    s.file_path = kCsoundPatchPath;
    s.file_body = "this is just a note to self, not an orchestra\n";
    char buf[256];
    bool from_sd = true;
    const char* orc = select_orchestra(&s, buf, sizeof(buf), kFallback, &from_sd);
    check(orc == kFallback, "a non-CSD text file falls back to the built-in");
    check(from_sd == false, "from_sd false for the non-CSD case");
}

void test_csd_tag_anywhere_accepted() {
    std::printf("CSD tag after a leading comment -> accepted\n");
    FakeStream s;
    s.file_path = kCsoundPatchPath;
    s.file_body = "; my patch\n; second comment line\n<CsoundSynthesizer>\n</CsoundSynthesizer>\n";
    char buf[256];
    bool from_sd = false;
    const char* orc = select_orchestra(&s, buf, sizeof(buf), kFallback, &from_sd);
    check(orc == buf && from_sd, "leading comments before the root tag are fine");
}

void test_null_or_tiny_buffer_falls_back() {
    std::printf("no scratch buffer -> fallback (no crash)\n");
    FakeStream s;
    s.file_path = kCsoundPatchPath;
    s.file_body = kValidCsd;
    bool from_sd = true;
    check(select_orchestra(&s, nullptr, 0, kFallback, &from_sd) == kFallback, "null buf -> fallback");
    check(from_sd == false, "from_sd false with null buf");
    char tiny[1];
    check(select_orchestra(&s, tiny, 1, kFallback, &from_sd) == kFallback, "max<=1 -> fallback");
}

void test_null_from_sd_out_is_tolerated() {
    std::printf("null from_sd out-param -> no crash\n");
    FakeStream s;
    s.file_path = kCsoundPatchPath;
    s.file_body = kValidCsd;
    char buf[256];
    const char* orc = select_orchestra(&s, buf, sizeof(buf), kFallback, nullptr);
    check(orc == buf, "works with a null from_sd pointer");
}

void test_looks_like_csd_unit() {
    std::printf("looks_like_csd unit\n");
    check(!looks_like_csd(nullptr), "null is not a CSD");
    check(!looks_like_csd(""), "empty is not a CSD");
    check(!looks_like_csd("instr 1\nendin\n"), "a bare orchestra (no root tag) is rejected");
    check(looks_like_csd("<CsoundSynthesizer></CsoundSynthesizer>"), "the root tag is detected");
}

} // namespace

int main() {
    std::printf("=== Csound orchestra selector (SD patch / fallback) ===\n");
    test_null_stream_uses_fallback();
    test_missing_file_uses_fallback();
    test_valid_patch_is_used();
    test_non_csd_text_falls_back();
    test_csd_tag_anywhere_accepted();
    test_null_or_tiny_buffer_falls_back();
    test_null_from_sd_out_is_tolerated();
    test_looks_like_csd_unit();

    if (g_failures) { std::printf("\nFAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("\nAll Csound selector tests passed.\n");
    return 0;
}
