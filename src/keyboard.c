#include <stddef.h>
#include <stdint.h>
#include "io.h"
#include "isr.h"
#include "keyboard.h"
#include "tty.h"

struct keyboard_layout_definition {
    const char *name;
    const char *description;
    const char base[128];
    const char shift[128];
};

static const struct keyboard_layout_definition g_layouts[] = {
    {
        .name = "us",
        .description = "Layout US (ANSI) padrao",
        .base = {
/*0x00*/ 0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
/*0x10*/ '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
/*0x20*/ 'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x',
/*0x30*/ 'c','v','b','n','m',',','.','/', 0,  0,  0,  ' ', 0,  0,  0,  0,
/*0x40*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x50*/ 0,0,0,0,0,0,'<',0,0,0,0,0,0,0,0,0,
/*0x60*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x70*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        },
        .shift = {
/*0x00*/ 0,  27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
/*0x10*/ '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
/*0x20*/ 'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|','Z','X',
/*0x30*/ 'C','V','B','N','M','<','>','?', 0,  0,  0,  ' ', 0,  0,  0,  0,
/*0x40*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x50*/ 0,0,0,0,0,0,'>',0,0,0,0,0,0,0,0,0,
/*0x60*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x70*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        }
    },
    {
        .name = "br-abnt2",
        .description = "Layout Brasileiro ABNT2",
        .base = {
/*0x00*/ 0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
/*0x10*/ '\t','q','w','e','r','t','y','u','i','o','p','\'','`','\n', 0,
/*0x20*/ 'a','s','d','f','g','h','j','k','l', (char)0xE7,'\'','`', 0,'\\','z','x',
/*0x30*/ 'c','v','b','n','m',',','.',';', 0,  0,  0,  ' ', 0,  0,  0,  0,
/*0x40*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x50*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x60*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x70*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        },
        .shift = {
/*0x00*/ 0,  27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
/*0x10*/ '\t','Q','W','E','R','T','Y','U','I','O','P','\"','~','\n', 0,
/*0x20*/ 'A','S','D','F','G','H','J','K','L', (char)0xC7,'\"','^', 0,'|','Z','X',
/*0x30*/ 'C','V','B','N','M','<','>','?', 0,  0,  0,  ' ', 0,  0,  0,  0,
/*0x40*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x50*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x60*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x70*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        }
    }
};

static const struct keyboard_layout_definition *current_layout = &g_layouts[0];

static int shift_on = 0;

static int layout_name_equal(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void keyboard_irq(void) {
    uint8_t sc = inb(0x60);

    // Press/release SHIFT (0x2A left, 0x36 right; releases: 0xAA, 0xB6)
    if (sc == 0x2A || sc == 0x36) { shift_on = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_on = 0; return; }

    // Ignora releases em geral
    if (sc & 0x80) return;

    // Backspace
    if (sc == 0x0E) { tty_handle_backspace(); return; }
    // Enter
    if (sc == 0x1C) { tty_handle_enter(); return; }

    if (sc >= 128) {
        return;
    }
    const char *mapping = shift_on ? current_layout->shift : current_layout->base;
    char ch = mapping[sc];
    if (ch) tty_handle_char(ch);
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq); // IRQ1
}

size_t keyboard_layout_count(void) {
    return sizeof(g_layouts)/sizeof(g_layouts[0]);
}

const char *keyboard_layout_name(size_t index) {
    if (index >= keyboard_layout_count()) {
        return NULL;
    }
    return g_layouts[index].name;
}

const char *keyboard_layout_description(size_t index) {
    if (index >= keyboard_layout_count()) {
        return NULL;
    }
    return g_layouts[index].description;
}

const char *keyboard_current_layout(void) {
    return current_layout->name;
}

int keyboard_set_layout_by_name(const char *name) {
    if (!name) {
        return -1;
    }
    for (size_t i = 0; i < keyboard_layout_count(); ++i) {
        if (layout_name_equal(name, g_layouts[i].name)) {
            current_layout = &g_layouts[i];
            return 0;
        }
    }
    return -1;
}
