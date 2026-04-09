#include <stdint.h>

extern void fbcon_print(const char *s);

uintptr_t __stack_chk_guard = (uintptr_t)0x595e9fbd3c2a1f07ULL;

void __stack_chk_fail(void) {
    fbcon_print("\n*** STACK SMASHING DETECTED ***\n");
    __asm__ volatile("cli");
    while (1) { __asm__ volatile("hlt"); }
}
