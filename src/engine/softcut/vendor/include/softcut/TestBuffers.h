//
// Created by ezra on 11/16/18.
//
// sk-engines PORT NOTE: upstream TestBuffers is a desktop-only debug capture - it holds
// `float buf[6][131072]` (= 3 MB) per ReadWriteHead and writes a Matlab .m file via <fstream>.
// On the Daisy that is 3 MB of SRAM per voice (18 MB for 6 voices) plus an iostream dependency,
// for diagnostics that never run on-device. Stubbed to an empty, API-compatible no-op. The only
// live call site is ReadWriteHead::init()'s testBuf.init(); update()/print() are unused here.
//
#ifndef Softcut_TESTBUFFERS_H
#define Softcut_TESTBUFFERS_H

namespace softcut {
class TestBuffers {
public:
    typedef enum { Read, Write, Fade, State, Pre, Rec, numChannels } Channel;
    void init() {}
    void update(float, float, float, float, float, float) {}
    void print() {}
};
}

#endif //Softcut_TESTBUFFERS_H
