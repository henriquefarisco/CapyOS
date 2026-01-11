#include <stdint.h>
#include "drivers/video/vga.h"

uintptr_t __stack_chk_guard = 0x595e9fbd;

void __stack_chk_fail(void) {
    vga_write("\n*** STACK SMASHING DETECTED ***\n");
    __asm__ volatile("cli");
    while (1) { __asm__ volatile("hlt"); }
}
