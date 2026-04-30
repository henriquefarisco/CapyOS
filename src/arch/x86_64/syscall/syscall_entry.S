# x86_64 syscall entry point for CapyOS
# Uses SYSCALL/SYSRET convention:
#   RAX = syscall number
#   RDI, RSI, RDX, R10, R8, R9 = arguments
#   RCX = saved RIP (by CPU)
#   R11 = saved RFLAGS (by CPU)
#
# All MSR addresses, STAR layout and segment selectors are sourced from
# the canonical header so a single edit is enough to keep the C side
# (syscall_init -> syscall_init_msr) and the asm side (syscall_entry)
# in lock-step. tests/test_syscall_msr.c fails the build if the values
# below ever diverge from architectural reality.

#include "arch/x86_64/cpu_local.h"
#include "arch/x86_64/syscall_msr.h"

.global syscall_entry
.global syscall_init_msr

# Kernel selectors:  GDT_KERNEL_CS_SELECTOR = 0x08, GDT_KERNEL_DS_SELECTOR = 0x10
# User selectors via SYSRET (STAR[63:48] = SYSCALL_STAR_USER_BASE = 0x10):
#   user SS (RPL=3) = base + 0x08 = 0x18 -> 0x1B
#   user CS (RPL=3) = base + 0x10 = 0x20 -> 0x23
# (The previous comment in this file had user CS and user SS swapped.)

syscall_init_msr:
    # Enable SCE (System Call Extensions) in EFER
    movl $IA32_EFER_MSR, %ecx
    rdmsr
    orl $EFER_SCE_BIT, %eax
    wrmsr

    # Set STAR: kernel segments and user segments
    movl $IA32_STAR_MSR, %ecx
    movl $SYSCALL_STAR_LOW, %eax
    movl $SYSCALL_STAR_HIGH, %edx
    wrmsr

    # Set LSTAR: syscall entry point
    movl $IA32_LSTAR_MSR, %ecx
    leaq syscall_entry(%rip), %rax
    movq %rax, %rdx
    shrq $32, %rdx
    wrmsr

    # Set SFMASK: clear IF and DF on syscall entry
    movl $IA32_FMASK_MSR, %ecx
    movl $SYSCALL_SFMASK_VALUE, %eax
    xorl %edx, %edx
    wrmsr

    ret

.align 16
syscall_entry:
    # We enter with:
    #   RCX = user RIP, R11 = user RFLAGS
    #   RSP = user RSP (must switch to kernel stack)

    # Save user RSP and switch to kernel stack via the per-CPU area
    # mapped by IA32_GS_BASE (see cpu_local_init in
    # src/arch/x86_64/cpu/cpu_local.c). Offsets come from the
    # header at include/arch/x86_64/cpu_local.h and are locked
    # by tests/test_cpu_local.c.
    # NOTE: do NOT start a comment line with `#<space>include` --
    # the C preprocessor (which runs on .S files before GAS)
    # treats it as a malformed `#include` directive.
    movq %rsp, %gs:CPU_LOCAL_USER_RSP_SCRATCH_OFFSET
    movq %gs:CPU_LOCAL_KERNEL_RSP_OFFSET, %rsp

    # Build syscall_frame on kernel stack
    pushq %r11                 # rflags
    pushq %rcx                 # rip (return address)
    pushq %gs:CPU_LOCAL_USER_RSP_SCRATCH_OFFSET   # user rsp
    pushq %r11                 # r11 (saved rflags duplicate)
    pushq %rcx                 # rcx (saved rip duplicate)
    pushq %r9
    pushq %r8
    pushq %r10                 # arg4 (r10 replaces rcx in syscall ABI)
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax                 # syscall number

    # Call C dispatcher: syscall_dispatch(frame)
    movq %rsp, %rdi
    call syscall_dispatch

    # Return value is in RAX, it stays there for userspace

    # Restore registers
    addq $8, %rsp              # skip saved rax (return value already in rax)
    popq %rdi
    popq %rsi
    popq %rdx
    popq %r10
    popq %r8
    popq %r9
    popq %rcx                  # rcx duplicate
    popq %r11                  # r11 duplicate
    popq %rsp                  # restore user RSP directly
    # RCX and R11 already have user RIP and RFLAGS from above pops

    sysretq

.size syscall_entry, .-syscall_entry

# Mark stack as non-executable to satisfy modern binutils hardening
.section .note.GNU-stack,"",@progbits
