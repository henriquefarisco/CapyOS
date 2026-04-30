# capylibc syscall stubs.
#
# Each `capy_*` symbol is a thin wrapper that:
#   1. Loads the syscall number into %rax (from the canonical
#      header kernel/syscall_numbers.h).
#   2. Re-shuffles the C calling convention (rdi, rsi, rdx, rcx, r8,
#      r9) into the SYSCALL ABI (rdi, rsi, rdx, r10, r8, r9). Only
#      arg index 3 needs to move (rcx -> r10) because SYSCALL itself
#      clobbers %rcx with the saved RIP.
#   3. Executes `syscall`, which traps into the kernel's
#      syscall_entry (src/arch/x86_64/syscall/syscall_entry.S).
#   4. Returns whatever the kernel placed in %rax.
#
# The stubs are __attribute__((naked))-style: no prologue/epilogue,
# no red zone use beyond what SYSCALL guarantees. None of these
# functions are called recursively or take callbacks, so the simple
# layout is safe.
#
# tests/test_capylibc_abi.c locks the syscall numbers used here
# against the kernel's source of truth.

#include "kernel/syscall_numbers.h"

.text

# void capy_exit(int status) __attribute__((noreturn));
# C ABI: %edi = status. We sign-extend through %rdi (kernel reads it
# as int64_t). SYS_EXIT is noreturn on the kernel side, but defend
# against future regressions with an infinite hlt loop.
.global capy_exit
capy_exit:
    movq $SYS_EXIT, %rax
    syscall
1:  hlt
    jmp 1b
.size capy_exit, .-capy_exit

# int capy_getpid(void);
.global capy_getpid
capy_getpid:
    movq $SYS_GETPID, %rax
    syscall
    ret
.size capy_getpid, .-capy_getpid

# int capy_getppid(void);
.global capy_getppid
capy_getppid:
    movq $SYS_GETPPID, %rax
    syscall
    ret
.size capy_getppid, .-capy_getppid

# long capy_write(int fd, const void *buf, size_t len);
# C ABI: %rdi=fd, %rsi=buf, %rdx=len -> identical to SYSCALL ABI for
# the first three args, no shuffle needed.
.global capy_write
capy_write:
    movq $SYS_WRITE, %rax
    syscall
    ret
.size capy_write, .-capy_write

# long capy_read(int fd, void *buf, size_t len);
.global capy_read
capy_read:
    movq $SYS_READ, %rax
    syscall
    ret
.size capy_read, .-capy_read

# void capy_yield(void);
.global capy_yield
capy_yield:
    movq $SYS_YIELD, %rax
    syscall
    ret
.size capy_yield, .-capy_yield

# void capy_sleep(unsigned long ticks);
.global capy_sleep
capy_sleep:
    movq $SYS_SLEEP, %rax
    syscall
    ret
.size capy_sleep, .-capy_sleep

# long capy_time(void);
.global capy_time
capy_time:
    movq $SYS_TIME, %rax
    syscall
    ret
.size capy_time, .-capy_time

# Mark stack as non-executable to satisfy modern binutils hardening.
.section .note.GNU-stack,"",@progbits
