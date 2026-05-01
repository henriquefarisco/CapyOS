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

# int capy_fork(void); (M5 phase A.4)
#
# Returns the child PID in the parent, 0 in the child.
#
# The kernel side (sys_fork) hands the child a synthetic IRET frame
# whose RIP equals the parent's saved RIP (the instruction RIGHT
# AFTER `syscall`) and whose RSP equals the parent's user RSP at
# syscall time. SYSCALL semantics clobber RCX and R11 (caller-saved),
# and SysV-AMD64 callee-saved regs (RBX, RBP, R12-R15) are not
# observable via the kernel-side `struct syscall_frame`. To preserve
# them across fork from BOTH branches' point of view, this stub
# spills them onto the user stack BEFORE issuing `syscall` and
# restores them AFTER. Both branches then pop the same saved values
# (the parent reads its own page; the child reads its CoW copy of
# the same page, which is byte-identical until the next write).
#
# Stack effect at the moment SYSCALL traps:
#
#     [ RSP +  0 ] saved r15
#     [ RSP +  8 ] saved r14
#     [ RSP + 16 ] saved r13
#     [ RSP + 24 ] saved r12
#     [ RSP + 32 ] saved rbp
#     [ RSP + 40 ] saved rbx
#     [ RSP + 48 ] return address (saved by `call capy_fork`)
#
# The kernel records this RSP into the child's IRET frame, so when
# the child first runs in ring 3 it lands at "post-syscall" with
# RSP pointing at saved r15 -- ready to pop just like the parent.
.global capy_fork
capy_fork:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq $SYS_FORK, %rax
    syscall
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret
.size capy_fork, .-capy_fork

# int capy_exec(const char *path, const char **argv); (M5 phase B.5)
#
# Replaces the calling process's address space with the binary
# resolved from `path` against the in-kernel embedded-progs
# registry, then implicitly transfers control to the new image's
# `_start`. On success the kernel overrides the syscall return
# frame so this stub NEVER returns to its caller; the new image
# starts with a fresh user RSP and RAX=0.
#
# On failure (NULL path, lookup miss, ELF validation failure, AS
# allocation failure) the kernel leaves the syscall frame intact
# and returns -1 in RAX; this stub then unwinds normally and
# capy_exec returns -1 to its C caller.
#
# C ABI: %rdi = path, %rsi = argv -- both forwarded as-is to
# SYSCALL ABI slots arg0/arg1 (no shuffle). The kernel ignores
# argv this phase but the slot is reserved.
.global capy_exec
capy_exec:
    movq $SYS_EXEC, %rax
    syscall
    ret
.size capy_exec, .-capy_exec

# int capy_wait(unsigned int pid, int *status); (M5 phase C.3)
#
# Block the caller until the child process identified by `pid`
# becomes a zombie, then write its exit code through `status` and
# reap the slot. Returns the reaped pid on success, -1 on failure.
#
# C ABI: %rdi = pid (zero-extended), %rsi = status pointer.
# Both forward as-is to SYSCALL ABI arg0/arg1.
.global capy_wait
capy_wait:
    movq $SYS_WAIT, %rax
    syscall
    ret
.size capy_wait, .-capy_wait

# int capy_pipe(int fds[2]); (M5 phase D)
#
# Allocates a fresh kernel pipe and binds its read end to fds[0]
# and its write end to fds[1]. Returns 0 on success and -1 on
# failure (NULL fds, kernel pipe table full, FD table full).
#
# C ABI: %rdi = fds (int *) -- forwarded as-is to SYSCALL ABI arg0.
.global capy_pipe
capy_pipe:
    movq $SYS_PIPE, %rax
    syscall
    ret
.size capy_pipe, .-capy_pipe

# Mark stack as non-executable to satisfy modern binutils hardening.
.section .note.GNU-stack,"",@progbits
