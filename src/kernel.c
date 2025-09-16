#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "io.h"
#include "vga.h"
#include "pit.h"

void kernel_main(void) {
    vga_init();
    vga_write("NoirOS 1 - Versao Singularity esta rodando!\n\n\n");
    vga_write("Ola Mundo!\n\n\n\n");
    vga_write("> ");

    gdt_init(); // Instala GDT própria
    idt_install();
    pic_remap(0x20, 0x28);
    pic_set_mask(0xFC, 0xFF);        // habilita IRQ0 e IRQ1 (bits 0 e 1 = 0), resto mascarado
    pit_init(100);               // 100 Hz (mode 2)
    keyboard_init();

    sti(); // habilita interrupções

    for (;;)
        hlt();
}
