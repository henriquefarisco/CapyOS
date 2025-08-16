#include <stdint.h>
#include "io.h"
#include "isr.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static irq_handler_t irq_routines[16] = {0};

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) irq_routines[irq] = handler;
}
void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) irq_routines[irq] = 0;
}

void pic_remap(uint8_t offset1, uint8_t offset2) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);     // ICW1: init + expect ICW4
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, offset1); // ICW2: base offset (ex: 0x20)
    outb(PIC2_DATA, offset2); // ICW2: base offset (ex: 0x28)
    outb(PIC1_DATA, 0x04);    // ICW3: PIC1 tem escravo em IRQ2 (bit 2)
    outb(PIC2_DATA, 0x02);    // ICW3: PIC2 ligado ao IR2 do mestre
    outb(PIC1_DATA, 0x01);    // ICW4: 8086/88 mode
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, a1);      // restaura máscaras
    outb(PIC2_DATA, a2);
}

void pic_set_mask(uint8_t master_mask, uint8_t slave_mask) {
    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

// Despachante comum: trata exceções e IRQs.
// Mantemos simples: em exceções, travamos com CLI+HLT; em IRQs, chamamos handler e damos EOI.
void isr_dispatch(uint32_t int_no, uint32_t err_code) {
    (void)err_code; // use se quiser logar

    if (int_no >= 32 && int_no <= 47) {
        int irq = (int)int_no - 32;

        if (irq >= 0 && irq < 16 && irq_routines[irq])
            irq_routines[irq]();

        // EOI: primeiro escravo se IRQ >= 8
        if (int_no >= 40) outb(PIC2_CMD, PIC_EOI);
        outb(PIC1_CMD, PIC_EOI);
        return;
    }

    // Exceção: para debug inicial, só congela de forma segura
    cli();
    for (;;)
        hlt();
}
