// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

// Lock-free handoff of the Csound instance between the audio ISR and the main loop, for live patch
// switching (roadmap #5 in docs/dev/csound-impl.md). process() (the ISR) performs on the instance; a
// patch change destroys it and compiles a new one from the main loop (in prepare()). Those must not
// overlap: destroying the instance while the ISR is mid-csoundPerformKsmps is a use-after-free.
//
// ReloadGate sequences ownership with no lock, for exactly one consumer (the audio ISR:
// begin_use/end_use) and one controller (the main loop: take/publish):
//
//   ISR each block:   void* cs = gate.begin_use();  if (cs) { ...perform... }  gate.end_use();
//   main loop reload:  void* old = gate.take();      // ISR now sees null (silence)
//                      destroy(old); cs2 = build();
//                      gate.publish(cs2);            // ISR sees cs2 next block
//
// Correctness rests on begin_use() marking the gate busy BEFORE it reads the active pointer: if the
// ISR is about to use (or is using) a non-null instance, `busy` is already set, so take()'s spin
// waits it out; if `busy` is clear when take() checks it, the ISR is not in a use-section and the
// pointer it reads next is already null (taken). So the ISR never touches an instance take() has
// removed and is about to destroy. Holds void* to stay libcsound-free and host-testable.
//
// MEMORY ORDER: both sides do store-then-load of DIFFERENT atomics (begin_use stores _busy then
// loads _active; take stores _active via exchange then loads _busy). That is Dekker's pattern, and
// release/acquire does NOT forbid StoreLoad reordering - so the ISR could read the old _active while
// take() reads a stale _busy=false and destroys it out from under the ISR (a use-after-free this
// header's host test reproduces in ~10/100000 reloads under release/acquire). Sequential consistency
// (seq_cst) on the handshake ops establishes a single total order that rules it out. seq_cst here is
// a DMB per audio block / per reload - not per sample - so the cost is negligible.

#include <atomic>

namespace spotykach {

class ReloadGate {
public:
    // --- ISR side -------------------------------------------------------------------------------
    // Begin a use-section: returns the instance to use this block (null during a reload). MUST be
    // paired with end_use(), even when it returns null.
    void* begin_use() {
        _busy.store(true, std::memory_order_seq_cst);
        return _active.load(std::memory_order_seq_cst);
    }
    void end_use() { _busy.store(false, std::memory_order_seq_cst); }

    // --- main-loop side -------------------------------------------------------------------------
    // Take the live instance out of service and wait until the ISR is guaranteed not to be using it.
    // Returns the old instance (may be null) for the caller to destroy. After this, begin_use()
    // returns null until publish().
    void* take() {
        void* old = _active.exchange(nullptr, std::memory_order_seq_cst);
        while (_busy.load(std::memory_order_seq_cst)) { /* spin: at most one audio block */ }
        return old;
    }
    // Install a freshly built instance.
    void publish(void* inst) { _active.store(inst, std::memory_order_seq_cst); }

    // Current instance (null if none / mid-reload). Safe from the main loop (the ISR never writes
    // _active); the ISR should use begin_use()/end_use() instead.
    void* current() const { return _active.load(std::memory_order_seq_cst); }

private:
    std::atomic<void*> _active{nullptr};
    std::atomic<bool>  _busy{false};
};

} // namespace spotykach
