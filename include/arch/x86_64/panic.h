#ifndef ARCH_X86_64_PANIC_H
#define ARCH_X86_64_PANIC_H

#include <stdint.h>

struct panic_regs {
  uint64_t rax, rbx, rcx, rdx;
  uint64_t rsi, rdi, rbp, rsp;
  uint64_t r8, r9, r10, r11;
  uint64_t r12, r13, r14, r15;
  uint64_t rip, rflags;
  uint64_t cr2, cr3;
  uint16_t cs, ss;
  uint32_t error_code;
  uint32_t vector;
};

#define PANIC_STACK_DUMP_WORDS 32

void panic(const char *message) __attribute__((noreturn));
void panic_with_regs(const char *message,
                     const struct panic_regs *regs) __attribute__((noreturn));
void panic_dump_to_serial(const struct panic_regs *regs);
void panic_set_framebuffer(uint32_t *fb, uint32_t width, uint32_t height,
                           uint32_t pitch);

#endif /* ARCH_X86_64_PANIC_H */
