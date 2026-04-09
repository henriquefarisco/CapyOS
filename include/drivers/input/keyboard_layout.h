#ifndef KEYBOARD_LAYOUT_H
#define KEYBOARD_LAYOUT_H

#include <stdint.h>

struct keyboard_layout {
    const char *name;
    const char *description;
    const char base[128];
    const char shift[128];
    // Bit 0: base mapping is dead (accent), Bit 1: shift mapping is dead
    const uint8_t dead[128];
};

extern const struct keyboard_layout g_keyboard_layout_us;
extern const struct keyboard_layout g_keyboard_layout_br_abnt2;

#endif
