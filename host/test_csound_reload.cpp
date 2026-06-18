// Host test for the live-patch-switch handoff (src/engine/csound/csound_reload.h, roadmap #5). The
// ReloadGate sequences the Csound instance between the audio ISR (begin_use/end_use) and the main
// loop (take/publish) with no lock. The property that matters and is easy to get wrong: the ISR must
// NEVER use an instance the main loop has taken out of service and is destroying (use-after-free).
//
// We model "destroyed" with an `alive` flag instead of actually freeing, so a bug is reported (the
// ISR sees a retired instance) rather than crashing the test. Two threads stand in for the audio ISR
// and the main loop. Build: `make -C host test-csound-reload`.

#include "engine/csound/csound_reload.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace spotykach;

namespace {

int g_failures = 0;
void check(bool cond, const char* msg) {
    if (!cond) { std::printf("  FAIL: %s\n", msg); g_failures++; }
}

struct Instance { std::atomic<bool> alive{true}; };

void test_single_threaded_sequencing() {
    std::printf("single-threaded take/publish sequencing\n");
    ReloadGate gate;
    check(gate.begin_use() == nullptr, "empty gate yields null"); gate.end_use();

    Instance a;
    gate.publish(&a);
    check(gate.current() == &a, "published instance is current");
    void* got = gate.begin_use(); gate.end_use();
    check(got == &a, "begin_use returns the live instance");

    void* old = gate.take();
    check(old == &a, "take returns the instance to destroy");
    check(gate.current() == nullptr, "after take, current is null");
    void* during = gate.begin_use(); gate.end_use();
    check(during == nullptr, "between take and publish the ISR sees null (silence)");

    Instance b;
    gate.publish(&b);
    check(gate.begin_use() == &b, "after publish the ISR sees the new instance"); gate.end_use();
}

// The core stress: an ISR thread continuously uses whatever begin_use() hands it, asserting it is
// alive; a main-loop thread continuously retires the live instance and publishes a fresh one. A
// correct gate guarantees the ISR only ever sees null or a still-alive instance.
void test_concurrent_no_use_after_free() {
    std::printf("concurrent ISR/main: no use of a retired instance\n");
    ReloadGate gate;
    std::atomic<bool> stop{false};
    std::atomic<long> uses{0}, reloads{0}, violations{0};
    std::vector<Instance*> retired;   // kept (not freed) so a buggy read is detected, not a crash

    Instance* first = new Instance();
    gate.publish(first);

    std::thread isr([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            void* p = gate.begin_use();
            if (p) {
                Instance* inst = static_cast<Instance*>(p);
                // While inside the use-section, the instance MUST still be alive.
                if (!inst->alive.load(std::memory_order_acquire)) violations.fetch_add(1);
                // simulate a little work inside the section to widen the race window
                for (volatile int k = 0; k < 50; k++) {}
                if (!inst->alive.load(std::memory_order_acquire)) violations.fetch_add(1);
                uses.fetch_add(1);
            }
            gate.end_use();
        }
    });

    std::thread mainloop([&] {
        for (int i = 0; i < 100000; i++) {
            void* old = gate.take();                 // ISR can no longer be using `old` after this
            if (old) {
                Instance* inst = static_cast<Instance*>(old);
                inst->alive.store(false, std::memory_order_release);   // "destroy"
                retired.push_back(inst);
            }
            Instance* fresh = new Instance();
            gate.publish(fresh);
            reloads.fetch_add(1);
        }
    });

    mainloop.join();
    stop.store(true, std::memory_order_relaxed);
    isr.join();

    // drain the final live instance
    if (void* last = gate.take()) { static_cast<Instance*>(last)->alive.store(false); retired.push_back(static_cast<Instance*>(last)); }

    std::printf("  (uses=%ld reloads=%ld violations=%ld)\n", uses.load(), reloads.load(), violations.load());
    check(violations.load() == 0, "the ISR never used a retired instance");
    check(reloads.load() == 100000, "all reloads completed");
    check(uses.load() > 0, "the ISR actually ran use-sections (the race window was exercised)");

    for (Instance* p : retired) delete p;
}

} // namespace

int main() {
    std::printf("=== Csound ReloadGate (live patch switch handoff) ===\n");
    test_single_threaded_sequencing();
    test_concurrent_no_use_after_free();

    if (g_failures) { std::printf("\nFAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("\nAll ReloadGate tests passed.\n");
    return 0;
}
