# capylibc C runtime startup (crt0).
#
# Provides the `_start` symbol that the kernel ELF loader sets as the
# entry point for a freshly-loaded user binary. Today the kernel
# loads the program but does NOT yet build a user-mode argc/argv/envp
# stack frame (that comes in fork/exec, M4 phase 6); _start therefore
# treats main() as `int main(void)` and ignores any incoming
# argc/argv. When phase 6 arrives, _start will read [%rsp] = argc and
# [%rsp+8] = argv before calling main().
#
# When main returns, _start invokes capy_exit() with main's return
# value. capy_exit is __attribute__((noreturn)) so we never reach the
# trailing `hlt` loop; it exists only so a buggy kernel that lets
# SYSCALL return after SYS_EXIT does not silently fall off the end
# of the .text section into whatever happens to follow.

#include "kernel/syscall_numbers.h"

.text
.global _start
_start:
    # Anchor the frame so any future stack-walker emits a sane backtrace.
    xorq %rbp, %rbp

    # M4 phase 8f.5: forward RAX -> RDI so a kernel-injected "rank"
    # value (set via the synth IRET frame's RAX slot by
    # user_task_arm_for_first_dispatch_with_rax) reaches main()
    # as its first arg per the System V x86_64 ABI. Tasks that did
    # NOT come through the synth-frame path see RAX=0 (a fresh
    # task created via task_create has all GP regs zeroed; an
    # iretq-from-boot path leaves RAX as whatever it was at the
    # iretq site, but the existing kernel_boot_run_embedded_hello
    # zeroes argc/argv setup), so main() sees rank=0 by default
    # which keeps the existing single-task hello path unchanged.
    movq %rax, %rdi

    # call int main(int rank); future milestones will load real
    # argc/argv from the SysV process stack here once fork/exec
    # land. Until then `rank` is the only channel from the kernel
    # to user space at startup.
    call main

    # main's return value is in %eax; sign-extend into %rdi for the
    # syscall (the kernel takes the exit code as int64_t in arg0).
    movslq %eax, %rdi
    movq $SYS_EXIT, %rax
    syscall

    # Defensive halt loop in case the kernel ever lets SYS_EXIT
    # return; should never execute.
1:  hlt
    jmp 1b
.size _start, .-_start

.section .note.GNU-stack,"",@progbits
