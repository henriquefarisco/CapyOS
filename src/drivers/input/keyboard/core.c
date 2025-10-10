#include <stddef.h>
#include <stdint.h>

#include "arch/x86/hw/io.h"
#include "arch/x86/cpu/isr.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_layout.h"
#include "drivers/console/tty.h"

static const struct keyboard_layout *g_layouts[] = {
    &g_keyboard_layout_us,
    &g_keyboard_layout_br_abnt2,
};

static const struct keyboard_layout *current_layout = NULL;
static int shift_on = 0;

static void keyboard_apply_layout(const struct keyboard_layout *layout)
{
    if (layout) {
        current_layout = layout;
    }
}

static void keyboard_irq(void)
{
    uint8_t sc = inb(0x60);

    if (sc == 0x2A || sc == 0x36) { shift_on = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_on = 0; return; }

    if (sc & 0x80) {
        return;
    }

    if (sc == 0x0E) { tty_handle_backspace(); return; }
    if (sc == 0x1C) { tty_handle_enter(); return; }
    if (sc == 0x56) { tty_handle_char(shift_on ? '>' : '<'); return; }

    if (!current_layout || sc >= 128) {
        return;
    }

    const char *mapping = shift_on ? current_layout->shift : current_layout->base;
    char ch = mapping[sc];
    if (ch) {
        tty_handle_char(ch);
    }
}

void keyboard_init(void)
{
    keyboard_apply_layout(g_layouts[0]);
    irq_install_handler(1, keyboard_irq);
}

size_t keyboard_layout_count(void)
{
    return sizeof(g_layouts) / sizeof(g_layouts[0]);
}

const char *keyboard_layout_name(size_t index)
{
    if (index >= keyboard_layout_count()) {
        return NULL;
    }
    return g_layouts[index]->name;
}

const char *keyboard_layout_description(size_t index)
{
    if (index >= keyboard_layout_count()) {
        return NULL;
    }
    return g_layouts[index]->description;
}

const char *keyboard_current_layout(void)
{
    return current_layout ? current_layout->name : NULL;
}

static int layout_name_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a++ != *b++) {
            return 0;
        }
    }
    return *a == *b;
}

int keyboard_set_layout_by_name(const char *name)
{
    if (!name) {
        return -1;
    }
    for (size_t i = 0; i < keyboard_layout_count(); ++i) {
        if (g_layouts[i] && g_layouts[i]->name && layout_name_equal(name, g_layouts[i]->name)) {
            keyboard_apply_layout(g_layouts[i]);
            return 0;
        }
    }
    return -1;
}
