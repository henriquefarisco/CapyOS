#include "arch/x86_64/panic.h"
#include <stddef.h>

#define COM1_PORT 0x3F8
#define DEBUGCON_PORT 0xE9

static uint32_t *panic_fb = NULL;
static uint32_t panic_fb_width = 0;
static uint32_t panic_fb_height = 0;
static uint32_t panic_fb_pitch = 0;

static inline void panic_outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void panic_serial_char(char c) {
  for (int i = 0; i < 100000; i++) {
    uint8_t status;
    __asm__ volatile("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)(COM1_PORT + 5)));
    if (status & 0x20) break;
  }
  panic_outb(COM1_PORT, (uint8_t)c);
  panic_outb(DEBUGCON_PORT, (uint8_t)c);
}

static void panic_serial_str(const char *s) {
  while (*s) panic_serial_char(*s++);
}

static void panic_serial_hex64(uint64_t v) {
  panic_serial_str("0x");
  for (int i = 60; i >= 0; i -= 4) {
    uint8_t nib = (v >> i) & 0xF;
    panic_serial_char((char)(nib < 10 ? '0' + nib : 'A' + nib - 10));
  }
}

static void panic_serial_dec(uint32_t v) {
  char buf[12];
  int pos = 0;
  if (v == 0) { panic_serial_char('0'); return; }
  while (v > 0) { buf[pos++] = '0' + (v % 10); v /= 10; }
  for (int i = pos - 1; i >= 0; i--) panic_serial_char(buf[i]);
}

static const char *exception_names[] = {
  "Division Error", "Debug", "NMI", "Breakpoint",
  "Overflow", "Bound Range", "Invalid Opcode", "No Math Coprocessor",
  "Double Fault", "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
  "Stack-Segment Fault", "General Protection", "Page Fault", "Reserved",
  "x87 FP Error", "Alignment Check", "Machine Check", "SIMD FP Error",
  "Virtualization", "Control Protection", "Reserved", "Reserved",
  "Reserved", "Reserved", "Reserved", "Reserved",
  "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved"
};

void panic_set_framebuffer(uint32_t *fb, uint32_t width, uint32_t height,
                           uint32_t pitch) {
  panic_fb = fb;
  panic_fb_width = width;
  panic_fb_height = height;
  panic_fb_pitch = pitch;
}

static void panic_fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                uint32_t color) {
  if (!panic_fb) return;
  for (uint32_t row = y; row < y + h && row < panic_fb_height; row++) {
    uint32_t *line = (uint32_t *)((uint8_t *)panic_fb + row * panic_fb_pitch);
    for (uint32_t col = x; col < x + w && col < panic_fb_width; col++) {
      line[col] = color;
    }
  }
}

void panic_dump_to_serial(const struct panic_regs *regs) {
  panic_serial_str("\r\n=== CapyOS PANIC ===\r\n");
  if (regs) {
    if (regs->vector < 32) {
      panic_serial_str("Exception: ");
      panic_serial_str(exception_names[regs->vector]);
      panic_serial_str(" (#");
      panic_serial_dec(regs->vector);
      panic_serial_str(")\r\n");
    }
    panic_serial_str("Error code: "); panic_serial_hex64(regs->error_code); panic_serial_str("\r\n");
    panic_serial_str("RIP: "); panic_serial_hex64(regs->rip); panic_serial_str("\r\n");
    panic_serial_str("RSP: "); panic_serial_hex64(regs->rsp); panic_serial_str("\r\n");
    panic_serial_str("RBP: "); panic_serial_hex64(regs->rbp); panic_serial_str("\r\n");
    panic_serial_str("RAX: "); panic_serial_hex64(regs->rax);
    panic_serial_str(" RBX: "); panic_serial_hex64(regs->rbx); panic_serial_str("\r\n");
    panic_serial_str("RCX: "); panic_serial_hex64(regs->rcx);
    panic_serial_str(" RDX: "); panic_serial_hex64(regs->rdx); panic_serial_str("\r\n");
    panic_serial_str("RSI: "); panic_serial_hex64(regs->rsi);
    panic_serial_str(" RDI: "); panic_serial_hex64(regs->rdi); panic_serial_str("\r\n");
    panic_serial_str("R8:  "); panic_serial_hex64(regs->r8);
    panic_serial_str(" R9:  "); panic_serial_hex64(regs->r9); panic_serial_str("\r\n");
    panic_serial_str("R10: "); panic_serial_hex64(regs->r10);
    panic_serial_str(" R11: "); panic_serial_hex64(regs->r11); panic_serial_str("\r\n");
    panic_serial_str("R12: "); panic_serial_hex64(regs->r12);
    panic_serial_str(" R13: "); panic_serial_hex64(regs->r13); panic_serial_str("\r\n");
    panic_serial_str("R14: "); panic_serial_hex64(regs->r14);
    panic_serial_str(" R15: "); panic_serial_hex64(regs->r15); panic_serial_str("\r\n");
    panic_serial_str("RFLAGS: "); panic_serial_hex64(regs->rflags); panic_serial_str("\r\n");
    panic_serial_str("CR2: "); panic_serial_hex64(regs->cr2);
    panic_serial_str(" CR3: "); panic_serial_hex64(regs->cr3); panic_serial_str("\r\n");
    panic_serial_str("CS: "); panic_serial_hex64(regs->cs);
    panic_serial_str(" SS: "); panic_serial_hex64(regs->ss); panic_serial_str("\r\n");

    panic_serial_str("\r\nStack dump (from RSP):\r\n");
    uint64_t *stack = (uint64_t *)regs->rsp;
    for (int i = 0; i < PANIC_STACK_DUMP_WORDS; i++) {
      panic_serial_str("  [RSP+");
      panic_serial_dec((uint32_t)(i * 8));
      panic_serial_str("] = ");
      panic_serial_hex64(stack[i]);
      panic_serial_str("\r\n");
    }
  }
  panic_serial_str("=== END PANIC ===\r\n");
}

void panic_with_regs(const char *message, const struct panic_regs *regs) {
  __asm__ volatile("cli");

  panic_serial_str("\r\n!!! KERNEL PANIC: ");
  panic_serial_str(message ? message : "(no message)");
  panic_serial_str(" !!!\r\n");

  panic_dump_to_serial(regs);

  if (panic_fb) {
    panic_fb_fill_rect(0, 0, panic_fb_width, panic_fb_height, 0x000044AA);
  }

  for (;;) __asm__ volatile("hlt");
}

void panic(const char *message) {
  struct panic_regs regs;
  __asm__ volatile(
    "movq %%rax, %0\n" "movq %%rbx, %1\n" "movq %%rcx, %2\n" "movq %%rdx, %3\n"
    "movq %%rsi, %4\n" "movq %%rdi, %5\n" "movq %%rbp, %6\n" "movq %%rsp, %7\n"
    "movq %%r8,  %8\n" "movq %%r9,  %9\n" "movq %%r10, %10\n" "movq %%r11, %11\n"
    "movq %%r12, %12\n" "movq %%r13, %13\n" "movq %%r14, %14\n" "movq %%r15, %15\n"
    : "=m"(regs.rax), "=m"(regs.rbx), "=m"(regs.rcx), "=m"(regs.rdx),
      "=m"(regs.rsi), "=m"(regs.rdi), "=m"(regs.rbp), "=m"(regs.rsp),
      "=m"(regs.r8), "=m"(regs.r9), "=m"(regs.r10), "=m"(regs.r11),
      "=m"(regs.r12), "=m"(regs.r13), "=m"(regs.r14), "=m"(regs.r15)
  );
  __asm__ volatile("pushfq; popq %0" : "=m"(regs.rflags));
  __asm__ volatile("movq %%cr2, %%rax; movq %%rax, %0" : "=m"(regs.cr2) :: "rax");
  __asm__ volatile("movq %%cr3, %%rax; movq %%rax, %0" : "=m"(regs.cr3) :: "rax");
  regs.rip = (uint64_t)__builtin_return_address(0);
  regs.vector = 0xFF;
  regs.error_code = 0;
  regs.cs = 0;
  regs.ss = 0;

  panic_with_regs(message, &regs);
}
