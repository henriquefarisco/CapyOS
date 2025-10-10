#ifndef KEYBOARD_LAYOUT_H
#define KEYBOARD_LAYOUT_H

struct keyboard_layout {
    const char *name;
    const char *description;
    const char base[128];
    const char shift[128];
};

extern const struct keyboard_layout g_keyboard_layout_us;
extern const struct keyboard_layout g_keyboard_layout_br_abnt2;

#endif
