#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "io.h"
#include "vga.h"
#include "pit.h"
#include "kmem.h"

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

    kinit(); // inicializa o heap (bump = 0)

    // --- TESTE DO ALLOCATOR ---
    char* p1 = (char*)kalloc(16);
    if (p1) { p1[0]='O'; p1[1]='K'; p1[2]='\0'; vga_write("Alloc 1: "); vga_write(p1); vga_write("\n"); }
    else    { vga_write("Alloc 1: FALHOU\n"); }

    char* p2 = (char*)kalloc(16);
    if (p2) { p2[0]='O'; p2[1]='K'; p2[2]='2'; p2[3]='\0'; vga_write("Alloc 2: "); vga_write(p2); vga_write("\n"); }
    else    { vga_write("Alloc 2: FALHOU\n"); }

    // pede algo exagerado para forçar falha (KHEAP_SIZE padrão = 256 KiB)
    void* big = kalloc(1024*1024); // 1 MiB
    if (!big) vga_write("Alloc grande: FALHOU (esperado)\n");
    else      vga_write("Alloc grande: OK (aumente KHEAP_SIZE)\n");
    // --- FIM DO TESTE ---

    keyboard_init();

    sti(); // habilita interrupções

    for (;;)
        hlt();
}
