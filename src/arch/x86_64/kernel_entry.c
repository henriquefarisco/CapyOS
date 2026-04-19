// Minimal x86_64 entry stub to unblock 64-bit linking.
// TODO: replace with long mode setup + jump to kernel_main64.

#include <stdint.h>

// Provide weak stubs so linker has an entry symbol.
__attribute__((weak)) void kernel_main(void) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}

__attribute__((noreturn)) void kernel_entry(void) {
    kernel_main();
    for (;;) {
        __asm__ volatile("hlt");
    }
}
