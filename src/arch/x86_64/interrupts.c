#include "arch/x86_64/framebuffer_console.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/panic.h"

#include <stddef.h>
#include <stdint.h>

#define DEBUGCON_PORT 0xE9
#define IDT_ENTRIES 256
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20
#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define IDT_GATE_INTERRUPT 0x8E

struct __attribute__((packed)) x64_gdt_entry {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t granularity;
  uint8_t base_high;
};

struct __attribute__((packed)) x64_idt_entry {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t ist;
  uint8_t type_attr;
  uint16_t offset_mid;
  uint32_t offset_high;
  uint32_t reserved;
};

struct __attribute__((packed)) x64_descriptor_ptr {
  uint16_t limit;
  uint64_t base;
};

struct x64_exception_frame {
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbp;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t rbx;
  uint64_t rax;
  uint64_t vector;
  uint64_t error_code;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
};

extern void *x64_exception_stub_table[32];
extern void *x64_irq_stub_table[16];
extern void x64_unhandled_vector_stub(void);

static struct x64_gdt_entry g_gdt[3];
static struct x64_descriptor_ptr g_gdtr;
static struct x64_idt_entry g_idt[IDT_ENTRIES];
static struct x64_descriptor_ptr g_idtr;
static void (*g_irq_handlers[16])(void);
static int g_platform_tables_active = 0;
static int g_platform_tables_bridge_active = 0;
static const char *g_platform_tables_status = "not-initialized";
static uint8_t g_master_mask = 0xFFu;
static uint8_t g_slave_mask = 0xFFu;

static const char *const g_exception_names[32] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack Fault",
    "#GP General Protection",
    "#PF Page Fault",
    "Reserved",
    "#MF x87 Floating Point",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating Point",
    "#VE Virtualization",
    "#CP Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "#HV Hypervisor Injection",
    "#VC VMM Communication",
    "#SX Security",
    "Reserved",
};

static inline void outb_local(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint64_t read_cr2_local(void) {
  uint64_t value = 0;
  __asm__ volatile("mov %%cr2, %0" : "=r"(value));
  return value;
}

void x64_interrupts_enable(void) { __asm__ volatile("sti" ::: "memory"); }

void x64_interrupts_disable(void) { __asm__ volatile("cli" ::: "memory"); }

static void diag_putc(char c) {
  if (c != '\r') {
    outb_local((uint16_t)DEBUGCON_PORT, (uint8_t)c);
  }
}

static void diag_write(const char *s) {
  if (!s) {
    return;
  }
  while (*s) {
    diag_putc(*s++);
  }
}

static void diag_hex64(uint64_t value) {
  static const char hex[] = "0123456789ABCDEF";
  for (int shift = 60; shift >= 0; shift -= 4) {
    diag_putc(hex[(value >> shift) & 0xFu]);
  }
}

static void diag_label_hex64(const char *label, uint64_t value) {
  diag_write(label);
  diag_hex64(value);
  diag_putc('\n');
}

static uint64_t interrupted_rsp_from_frame(
    const struct x64_exception_frame *frame) {
  if (!frame) {
    return 0;
  }
  /* Current boot/runtime faults arrive from CPL0, so the CPU frame ends at
   * RFLAGS and the interrupted RSP is immediately above it. */
  return (uint64_t)(uintptr_t)(&frame->rflags + 1);
}

static __attribute__((noreturn)) void diag_halt_forever(void) {
  __asm__ volatile("cli");
  for (;;) {
    __asm__ volatile("hlt");
  }
}

static void x64_gdt_set(int index, uint32_t base, uint32_t limit,
                        uint8_t access, uint8_t granularity) {
  struct x64_gdt_entry *entry = &g_gdt[index];
  entry->limit_low = (uint16_t)(limit & 0xFFFFu);
  entry->base_low = (uint16_t)(base & 0xFFFFu);
  entry->base_mid = (uint8_t)((base >> 16) & 0xFFu);
  entry->access = access;
  entry->granularity =
      (uint8_t)(((limit >> 16) & 0x0Fu) | (granularity & 0xF0u));
  entry->base_high = (uint8_t)((base >> 24) & 0xFFu);
}

static void x64_load_gdt(const struct x64_descriptor_ptr *gdtr) {
  __asm__ volatile("lgdt (%0)\n\t"
                   "movw $" "0x10" ", %%ax\n\t"
                   "movw %%ax, %%ds\n\t"
                   "movw %%ax, %%es\n\t"
                   "movw %%ax, %%ss\n\t"
                   "movw %%ax, %%fs\n\t"
                   "movw %%ax, %%gs\n\t"
                   "pushq $" "0x08" "\n\t"
                   "leaq 1f(%%rip), %%rax\n\t"
                   "pushq %%rax\n\t"
                   "lretq\n\t"
                   "1:\n\t"
                   :
                   : "r"(gdtr)
                   : "rax", "memory");
}

static void x64_idt_set_gate(uint8_t vector, void (*handler)(void),
                             uint8_t type_attr) {
  uintptr_t address = (uintptr_t)handler;
  struct x64_idt_entry *entry = &g_idt[vector];
  entry->offset_low = (uint16_t)(address & 0xFFFFu);
  entry->selector = KERNEL_CODE_SELECTOR;
  entry->ist = 0;
  entry->type_attr = type_attr;
  entry->offset_mid = (uint16_t)((address >> 16) & 0xFFFFu);
  entry->offset_high = (uint32_t)((address >> 32) & 0xFFFFFFFFu);
  entry->reserved = 0;
}

__attribute__((optimize("O0"))) void gdt_init(void) {
  x64_gdt_set(0, 0, 0, 0, 0);
  x64_gdt_set(1, 0, 0, 0x9Au, 0x20u);
  x64_gdt_set(2, 0, 0, 0x92u, 0x00u);

  g_gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1u);
  g_gdtr.base = (uint64_t)(uintptr_t)&g_gdt[0];
  x64_load_gdt(&g_gdtr);
}

/* Keep the early descriptor-table path scalar. Auto-vectorized XMM moves here
 * can fault before the kernel has taken full control of CPU feature state. */
__attribute__((optimize("O0"))) void idt_install(void) {
  for (uint32_t i = 0; i < IDT_ENTRIES; ++i) {
    x64_idt_set_gate((uint8_t)i, x64_unhandled_vector_stub,
                     IDT_GATE_INTERRUPT);
  }

  for (uint32_t i = 0; i < 32; ++i) {
    x64_idt_set_gate((uint8_t)i,
                     (void (*)(void))(uintptr_t)x64_exception_stub_table[i],
                     IDT_GATE_INTERRUPT);
  }

  for (uint32_t i = 0; i < 16; ++i) {
    x64_idt_set_gate((uint8_t)(32u + i),
                     (void (*)(void))(uintptr_t)x64_irq_stub_table[i],
                     IDT_GATE_INTERRUPT);
  }

  g_idtr.limit = (uint16_t)(sizeof(g_idt) - 1u);
  g_idtr.base = (uint64_t)(uintptr_t)&g_idt[0];
  __asm__ volatile("lidt (%0)" : : "r"(&g_idtr) : "memory");
}

void irq_install_handler(int irq, void (*handler)(void)) {
  if (irq < 0 || irq >= 16) {
    return;
  }
  g_irq_handlers[irq] = handler;
}

void irq_uninstall_handler(int irq) {
  if (irq < 0 || irq >= 16) {
    return;
  }
  g_irq_handlers[irq] = NULL;
}

void pic_remap(uint8_t master_offset, uint8_t slave_offset) {
  x64_interrupts_disable();
  outb_local(PIC1_CMD, 0x11u);
  outb_local(PIC2_CMD, 0x11u);
  outb_local(PIC1_DATA, master_offset);
  outb_local(PIC2_DATA, slave_offset);
  outb_local(PIC1_DATA, 0x04u);
  outb_local(PIC2_DATA, 0x02u);
  outb_local(PIC1_DATA, 0x01u);
  outb_local(PIC2_DATA, 0x01u);
  outb_local(PIC1_DATA, g_master_mask);
  outb_local(PIC2_DATA, g_slave_mask);
}

void pic_set_mask(uint8_t master_mask, uint8_t slave_mask) {
  g_master_mask = master_mask;
  g_slave_mask = slave_mask;
  outb_local(PIC1_DATA, master_mask);
  outb_local(PIC2_DATA, slave_mask);
}

void x64_irq_unmask(int irq) {
  if (irq < 0 || irq >= 16) {
    return;
  }
  if (irq < 8) {
    g_master_mask = (uint8_t)(g_master_mask & ~(uint8_t)(1u << irq));
  } else {
    g_slave_mask = (uint8_t)(g_slave_mask & ~(uint8_t)(1u << (irq - 8)));
    g_master_mask = (uint8_t)(g_master_mask & ~(uint8_t)(1u << 2));
  }
  pic_set_mask(g_master_mask, g_slave_mask);
}

void x64_irq_mask(int irq) {
  if (irq < 0 || irq >= 16) {
    return;
  }
  if (irq < 8) {
    g_master_mask = (uint8_t)(g_master_mask | (uint8_t)(1u << irq));
  } else {
    g_slave_mask = (uint8_t)(g_slave_mask | (uint8_t)(1u << (irq - 8)));
  }
  pic_set_mask(g_master_mask, g_slave_mask);
}

static void pic_send_eoi(uint64_t vector) {
  if (vector >= 40u && vector <= 47u) {
    outb_local(PIC2_CMD, PIC_EOI);
  }
  if (vector >= 32u && vector <= 47u) {
    outb_local(PIC1_CMD, PIC_EOI);
  }
}

__attribute__((optimize("O0"))) static void
report_fault(const struct x64_exception_frame *frame) {
  uint64_t vector = frame ? frame->vector : 0xFFFFFFFFFFFFFFFFULL;

  diag_write("\n[x64] Fatal fault\n");
  if (vector < 32u) {
    diag_write("[x64] Type: ");
    diag_write(g_exception_names[vector]);
    diag_putc('\n');
  } else if (vector >= 32u && vector <= 47u) {
    diag_write("[x64] Type: IRQ without handler\n");
  } else {
    diag_write("[x64] Type: unhandled interrupt vector\n");
  }

  diag_label_hex64("[x64] vector=0x", vector);
  if (frame) {
    uint64_t interrupted_rsp = interrupted_rsp_from_frame(frame);
    diag_label_hex64("[x64] err=0x", frame->error_code);
    diag_label_hex64("[x64] rip=0x", frame->rip);
    diag_label_hex64("[x64] cs=0x", frame->cs);
    diag_label_hex64("[x64] rflags=0x", frame->rflags);
    diag_label_hex64("[x64] rax=0x", frame->rax);
    diag_label_hex64("[x64] rbx=0x", frame->rbx);
    diag_label_hex64("[x64] rcx=0x", frame->rcx);
    diag_label_hex64("[x64] rdx=0x", frame->rdx);
    diag_label_hex64("[x64] rsi=0x", frame->rsi);
    diag_label_hex64("[x64] rdi=0x", frame->rdi);
    diag_label_hex64("[x64] rbp=0x", frame->rbp);
    diag_label_hex64("[x64] rsp=0x", interrupted_rsp);
    diag_label_hex64("[x64] r8 =0x", frame->r8);
    diag_label_hex64("[x64] r9 =0x", frame->r9);
    diag_label_hex64("[x64] r10=0x", frame->r10);
    diag_label_hex64("[x64] r11=0x", frame->r11);
    diag_label_hex64("[x64] r12=0x", frame->r12);
    diag_label_hex64("[x64] r13=0x", frame->r13);
    diag_label_hex64("[x64] r14=0x", frame->r14);
    diag_label_hex64("[x64] r15=0x", frame->r15);
    if (vector == 14u) {
      diag_label_hex64("[x64] cr2=0x", read_cr2_local());
    }
  }
}

__attribute__((optimize("O0"))) void
x64_exception_dispatch(struct x64_exception_frame *frame) {
  uint64_t vector = frame ? frame->vector : 0xFFFFFFFFFFFFFFFFULL;

  if (vector >= 32u && vector <= 47u) {
    int irq = (int)(vector - 32u);
    void (*handler)(void) = g_irq_handlers[irq];
    if (handler) {
      handler();
    }
    pic_send_eoi(vector);
    return;
  }

  report_fault(frame);

  /* Invoke structured panic handler for full dump + blue screen */
  if (frame) {
    struct panic_regs pregs;
    uint64_t interrupted_rsp = interrupted_rsp_from_frame(frame);
    pregs.rax = frame->rax; pregs.rbx = frame->rbx;
    pregs.rcx = frame->rcx; pregs.rdx = frame->rdx;
    pregs.rsi = frame->rsi; pregs.rdi = frame->rdi;
    pregs.rbp = frame->rbp;
    pregs.rsp = interrupted_rsp;
    pregs.r8 = frame->r8;   pregs.r9 = frame->r9;
    pregs.r10 = frame->r10; pregs.r11 = frame->r11;
    pregs.r12 = frame->r12; pregs.r13 = frame->r13;
    pregs.r14 = frame->r14; pregs.r15 = frame->r15;
    pregs.rip = frame->rip; pregs.rflags = frame->rflags;
    pregs.cr2 = (vector == 14u) ? read_cr2_local() : 0;
    pregs.cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(pregs.cr3));
    pregs.cs = (uint16_t)frame->cs; pregs.ss = 0;
    pregs.error_code = (uint32_t)frame->error_code;
    pregs.vector = (uint32_t)vector;
    panic_with_regs(vector < 32u ? g_exception_names[vector] : "unhandled vector", &pregs);
  }
  diag_halt_forever();
}

void x64_platform_tables_init(int native_runtime_ready) {
  if (g_platform_tables_active) {
    return;
  }

  if (!native_runtime_ready) {
    if (!g_platform_tables_bridge_active) {
      g_platform_tables_status = "deferred-firmware-runtime";
    }
    return;
  }

  if (g_platform_tables_bridge_active) {
    g_platform_tables_active = 1;
    g_platform_tables_status = "native-descriptors-active";
    return;
  }

  x64_interrupts_disable();
  gdt_init();
  idt_install();
  pic_remap(0x20u, 0x28u);
  pic_set_mask(0xFFu, 0xFFu);
  g_platform_tables_active = 1;
  g_platform_tables_status = "native-descriptors-active";
}

int x64_platform_tables_active(void) { return g_platform_tables_active; }

int x64_platform_tables_prepare_bridge(void) {
  if (g_platform_tables_active || g_platform_tables_bridge_active) {
    return 0;
  }

  x64_interrupts_disable();
  gdt_init();
  idt_install();
  pic_remap(0x20u, 0x28u);
  pic_set_mask(0xFFu, 0xFFu);
  g_platform_tables_bridge_active = 1;
  g_platform_tables_status = "bridge-descriptors-active";
  return 1;
}

int x64_platform_tables_bridge_active(void) {
  return g_platform_tables_bridge_active;
}

const char *x64_platform_tables_status(void) { return g_platform_tables_status; }
