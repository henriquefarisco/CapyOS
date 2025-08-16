#include <stdint.h>
#include "io.h"
#include "isr.h"
#include "keyboard.h"
#include "vga.h"

/* Tabela de scancodes (set 1) -> ASCII (sem dead keys)
   Só mapeamos o essencial por simplicidade. */
static const char keymap[128] = {
/*0x00*/ 0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
/*0x10*/ '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
/*0x20*/ 'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x',
/*0x30*/ 'c','v','b','n','m',',','.','/', 0,  0,  0,  ' ', 0,  0,  0,  0,
/*0x40*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x50*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x60*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x70*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char keymap_shift[128] = {
/*0x00*/ 0,  27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
/*0x10*/ '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
/*0x20*/ 'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|','Z','X',
/*0x30*/ 'C','V','B','N','M','<','>','?', 0,  0,  0,  ' ', 0,  0,  0,  0,
/*0x40*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x50*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x60*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*0x70*/ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static int shift_on = 0;

static void keyboard_irq(void) {
    uint8_t sc = inb(0x60);

    // Press/release SHIFT (0x2A left, 0x36 right; releases: 0xAA, 0xB6)
    if (sc == 0x2A || sc == 0x36) { shift_on = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_on = 0; return; }

    // Ignora releases em geral
    if (sc & 0x80) return;

    // Backspace
    if (sc == 0x0E) { vga_backspace(); return; }
    // Enter
    if (sc == 0x1C) { vga_newline(); return; }

    char ch = shift_on ? keymap_shift[sc] : keymap[sc];
    if (ch) vga_putc(ch);
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq); // IRQ1
}
