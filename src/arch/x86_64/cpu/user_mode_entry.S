# enter_user_mode(uint64_t rip, uint64_t rsp)
#
# x86_64 SYSV ABI: rdi = rip, rsi = rsp.
#
# Drops the calling kernel thread into Ring 3 by pushing an IRET frame
# and executing iretq. The frame layout the CPU expects (on the stack
# at the moment of iretq, low address at the top):
#
#     [rsp + 0x00]  RIP        (entry point in user mode)
#     [rsp + 0x08]  CS         (USER_CS_RPL3 = 0x23)
#     [rsp + 0x10]  RFLAGS     (0x202: IF=1, reserved bit 1=1)
#     [rsp + 0x18]  RSP        (user stack pointer)
#     [rsp + 0x20]  SS         (USER_SS_RPL3 = 0x1B)
#
# The selectors and the GDT entries they reference are locked by
# tests/test_syscall_msr.c. The frame layout itself is locked by
# tests/test_enter_user_mode.c which decodes the same field offsets
# from a test-side mirror.
#
# This routine never returns. It is the kernel-to-user entry primitive
# that callers like process_enter_user_mode() use exactly once per
# process.
#
# Today the routine assumes the caller has already loaded the right
# CR3 (process address space) and that cpu_local_init() has run so the
# syscall path will find the per-CPU area. CR3 management is the
# caller's responsibility.

#include "arch/x86_64/syscall_msr.h"

.global enter_user_mode

.text
.align 16
enter_user_mode:
    # Defensive cli: we are about to drop privilege and any IRQ taken
    # while the IRET frame is half-built would be catastrophic. iretq
    # itself will pop RFLAGS, which restores IF=1 from our pushed
    # 0x202.
    cli

    # Build the IRET frame in reverse (CPU pops in the order shown
    # above).
    pushq $USER_SS_RPL3       # SS
    pushq %rsi                # user RSP
    pushq $0x202              # RFLAGS: IF=1, reserved bit 1 set
    pushq $USER_CS_RPL3       # CS
    pushq %rdi                # RIP

    iretq

.size enter_user_mode, .-enter_user_mode

# Mark stack as non-executable to satisfy modern binutils hardening.
.section .note.GNU-stack,"",@progbits
