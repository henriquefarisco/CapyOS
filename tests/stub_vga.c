// Host-side stub for VGA output used in unit tests.
#include <stdio.h>

void vga_write(const char *s) {
    (void)s;
}

void vga_putc(char c) {
    (void)c;
}

void vga_clear(void) {}
