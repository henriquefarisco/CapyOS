#include <stdint.h>
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "io.h"
#include "vga.h"

void kernel_main(void) {
    vga_init();
    vga_write("NoirOS 1 - Versao Singularity esta rodando!\n\n\n");
    vga_write("Ola Mundo!\n\n\n\n");
    vga_write("> ");

    idt_install();
    pic_remap(0x20, 0x28);
    pic_set_mask(0xFD, 0xFF);   // habilita apenas IRQ1 (teclado)
    keyboard_init();

    sti(); // habilita interrupções

    for (;;)
        hlt();
}
