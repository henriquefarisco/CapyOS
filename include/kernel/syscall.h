#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* Syscall numbers are defined in a separate, asm-friendly header so
 * that userland asm stubs (capylibc) can pull them in via cpp without
 * also pulling in <stdint.h> / struct declarations. See
 * include/kernel/syscall_numbers.h. */
#include "kernel/syscall_numbers.h"

struct syscall_frame {
  uint64_t rax;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t r10;
  uint64_t r8;
  uint64_t r9;
  uint64_t rcx;
  uint64_t r11;
  uint64_t rip;
  uint64_t rsp;
  uint64_t rflags;
};

typedef int64_t (*syscall_handler_fn)(struct syscall_frame *frame);

void syscall_init(void);
void syscall_register(uint32_t num, syscall_handler_fn handler);
int64_t syscall_dispatch(struct syscall_frame *frame);

#endif /* KERNEL_SYSCALL_H */
