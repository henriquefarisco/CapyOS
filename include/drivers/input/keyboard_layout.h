#ifndef KEYBOARD_LAYOUT_H
#define KEYBOARD_LAYOUT_H

#include <stdint.h>

/* Special key codes returned via keyboard_poll_event for non-printable keys. */
#define KEY_NONE        0x00
#define KEY_UP          0x80
#define KEY_DOWN        0x81
#define KEY_LEFT        0x82
#define KEY_RIGHT       0x83
#define KEY_HOME        0x84
#define KEY_END         0x85
#define KEY_PGUP        0x86
#define KEY_PGDN        0x87
#define KEY_INSERT      0x88
#define KEY_DELETE       0x89
#define KEY_F1          0x90
#define KEY_F2          0x91
#define KEY_F3          0x92
#define KEY_F4          0x93
#define KEY_F5          0x94
#define KEY_F6          0x95
#define KEY_F7          0x96
#define KEY_F8          0x97
#define KEY_F9          0x98
#define KEY_F10         0x99
#define KEY_F11         0x9A
#define KEY_F12         0x9B

struct keyboard_layout {
    const char *name;
    const char *description;
    const char base[128];
    const char shift[128];
    const char altgr[128];
    /* Bit 0: base mapping is dead (accent), Bit 1: shift mapping is dead
     * Bit 2: altgr mapping is dead */
    const uint8_t dead[128];
};

extern const struct keyboard_layout g_keyboard_layout_us;
extern const struct keyboard_layout g_keyboard_layout_br_abnt2;

#endif
