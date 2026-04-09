#include <stdint.h>
#include "arch/x86/hw/io.h"
#include "arch/x86/cpu/isr.h"
#include "drivers/video/vga.h"   // (saída em texto VGA)

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static const char* exc_name[32] = {
  "#DE Divide Error", "#DB Debug", "NMI", "#BP Breakpoint", "#OF Overflow",
  "#BR BOUND", "#UD Invalid Opcode", "#NM Device Not Avail", "#DF Double Fault",
  "Coprocessor Segment", "#TS TSS", "#NP Segment Not Present", "#SS Stack Fault",
  "#GP General Protection", "#PF Page Fault", "Reserved", "#MF x87 FP",
  "#AC Alignment Check", "#MC Machine Check", "#XM SIMD FP", "#VE Virtualization",
  "Reserved","Reserved","Reserved","Reserved","Reserved","Reserved","Reserved",
  "Reserved","#SX Security","Reserved","Reserved"
};

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

/* imprime 32-bit em hex (sem stdlib) */
static void put_hex32(uint32_t x){
    const char* d="0123456789ABCDEF";
    vga_write("0x");
    for (int i=7;i>=0;i--){
        uint8_t nyb = (x >> (i*4)) & 0xF;
        vga_putc(d[nyb]);
    }
}

static int exception_has_errcode(uint32_t int_no) {
    switch (int_no) {
        case 8:  // #DF
        case 10: // #TS
        case 11: // #NP
        case 12: // #SS
        case 13: // #GP
        case 14: // #PF
        case 17: // #AC
        case 30: // #SX
            return 1;
        default:
            return 0;
    }
}

static void report_exception(uint32_t int_no, uint32_t err_code, struct isr_stack_state *stack){
    uint32_t *raw = (uint32_t *)stack;
    uint32_t eip_index = 8;
    if (exception_has_errcode(int_no)) {
        eip_index = 9;
    }
    uint32_t eip = raw[eip_index];
    uint32_t cs = raw[eip_index + 1];
    uint32_t eflags = raw[eip_index + 2];
    uint32_t useresp = 0;
    uint32_t ss = 0;
    int from_user = (cs & 0x3) != 0;
    if (from_user) {
        useresp = raw[eip_index + 3];
        ss = raw[eip_index + 4];
    }

    vga_write("\n=== EXCEPTION ===\n");
    vga_write("tipo: ");
    if (int_no < 32) vga_write(exc_name[int_no]); else vga_write("Unknown");
    vga_write("\nint_no: "); put_hex32(int_no);
    vga_write("\nerr_cd: "); put_hex32(err_code);
    vga_write("\neip:    "); put_hex32(eip);
    vga_write("\ncs:     "); put_hex32(cs);
    vga_write("\neflags: "); put_hex32(eflags);
    if (from_user) {
        vga_write("\nuseresp:"); put_hex32(useresp);
        vga_write("\nss:     "); put_hex32(ss);
    }
    vga_write("\nregs:   ");
    vga_write("EAX="); put_hex32(stack->eax); vga_write(" ");
    vga_write("ECX="); put_hex32(stack->ecx); vga_write(" ");
    vga_write("EDX="); put_hex32(stack->edx); vga_write(" ");
    vga_write("EBX="); put_hex32(stack->ebx); vga_write(" ");
    vga_write("ESP0="); put_hex32(stack->esp0); vga_write(" ");
    vga_write("EBP="); put_hex32(stack->ebp); vga_write(" ");
    vga_write("ESI="); put_hex32(stack->esi); vga_write(" ");
    vga_write("EDI="); put_hex32(stack->edi);

    if (int_no == 14){ // (#PF page fault)
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        vga_write("\ncr2:    "); put_hex32(cr2);
    }
    vga_write("\n=================\n");
    __asm__ volatile("cli");
    for(;;) __asm__ volatile("hlt");
}

// Despachante comum: trata exceções e IRQs.
// Mantemos simples: em exceções, travamos com CLI+HLT; em IRQs, chamamos handler e damos EOI.
void isr_dispatch(uint32_t int_no, uint32_t err_code, struct isr_stack_state *stack) {

    if (int_no >= 32 && int_no <= 47) {
        int irq = (int)int_no - 32;

        if (irq >= 0 && irq < 16 && irq_routines[irq])
            irq_routines[irq]();

        // EOI: primeiro escravo se IRQ >= 8
        if (int_no >= 40) outb(PIC2_CMD, PIC_EOI);
        outb(PIC1_CMD, PIC_EOI);
        return;
    }

    // Exceção: relato detalhado e trava
    report_exception(int_no, err_code, stack);
}
