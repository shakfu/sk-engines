// VTOR inject for the BOOT_QSPI build (the csound engine; ENGINE_SOURCES pulls this in). The
// spotykach startup skips SystemInit under BOOT_APP (it trusts the bootloader to set VTOR), and the
// v2 bootloader may not set it for a QSPI app. This high-priority global constructor runs during
// __libc_init_array - before main() and before any interrupt is enabled - and points the vector
// table at the QSPI app base, so SysTick and the audio DMA IRQ reach the app. Compiled ONLY into a
// QSPI build; the normal SRAM firmware never includes this file, so the working configuration is
// untouched. (First proven with a BOOT_QSPI radio build before the csound engine used it.)

#include "stm32h7xx.h"

namespace {
struct VtorInit {
    VtorInit() { SCB->VTOR = 0x90040000; __DSB(); __ISB(); }
};
// init_priority 101 = run among the earliest constructors, before any engine/platform ctor.
VtorInit _spk_qspi_vtor_init __attribute__((init_priority(101)));
} // namespace
