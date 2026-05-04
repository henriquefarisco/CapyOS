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

/* 2026-05-02: sys_read / sys_write are exposed (non-static) so that
 * host tests can drive them directly with a synthetic frame and a
 * faked process_current(). The kernel still uses them only via the
 * syscall_table dispatcher; nothing else in the kernel calls them
 * directly. The priority contract these tests lock is documented
 * inline in src/kernel/syscall.c: the process FD table wins over
 * legacy fd 0 (stdin_buf) / fd 1/2 (debugcon) defaults so that
 * processes which install explicit pipes at those indices (the
 * browser engine in particular) see pipe semantics. */
int64_t sys_read(struct syscall_frame *frame);
int64_t sys_write(struct syscall_frame *frame);

#endif /* KERNEL_SYSCALL_H */
